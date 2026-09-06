"""End-to-end: captures a menu profile and a first-mission profile for one asset tree.

Drives the harness twice against a build and the asset tree to profile -- once reaching
the main menu, once through a scenario to the first playing frame -- with the profile-capture
switch on both times, so each capture holds only what that session actually read rather than
what the ordinary prefetch heuristic guesses ahead of it. The two captures are then folded
into a menu profile and a first-mission profile (the delta beyond menu), each tied to the
asset tree's own manifest hash.

Usage:
    python3 tools/harness/pgo_pipeline.py --bin build-wasm/bin --assets build/web \
        --scenario GDI1A.MAP --out profiles/

`--assets` is the OpenTS-Assets web tree to profile, and is what the harness serves beside the
build; it defaults the way the harness's own `--assets` does. The manifest the captures are
read against is read back out of that tree, so a profile is tied to exactly what was served.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pgo_profile import build_profile, subtract_profile, _manifest_hash  # noqa: E402


def _run_harness(harness_py, bin_dir, assets_dir, do_steps, report_path, scenario=None,
        timeout=180, skip_intro=True):
    cmd = [sys.executable, harness_py, "run", "--bin", bin_dir, "--query", "pgocapture=1"]

    if assets_dir:
        cmd += ["--assets", assets_dir]

    if skip_intro:
        cmd += ["--query", "nointro=1"]

    if scenario:
        cmd += ["--scenario", scenario]

    for step in do_steps:
        cmd += ["--do", step]

    # A failing run writes its screenshot beside the report rather than into the caller's
    # working directory.
    cmd += ["--out", os.path.dirname(report_path), "--report", report_path]

    # A run's own teardown prints a harmless connection-reset traceback as the browser
    # closes the socket out from under the server; the report file is what actually says
    # whether the run succeeded, not the process's exit code.
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)

    if not os.path.isfile(report_path):
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError("harness run wrote no report: %s" % " ".join(cmd))

    with open(report_path) as handle:
        report = json.load(handle)

    if not report.get("ok"):
        sys.stderr.write(result.stdout)
        failure = report.get("failure") or {}
        raise RuntimeError("harness run failed at %r: %s (on screen: %s)" % (
            failure.get("step"), failure.get("error"), failure.get("screen")))

    return report


def _total_bytes(profile):
    return sum(stop - start for entry in profile["entries"] for start, stop in entry["ranges"])


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", required=True,
        help="the WASM build's bin directory")
    parser.add_argument("--assets",
        help="the OpenTS-Assets web tree to profile (default: what the harness would serve)")
    parser.add_argument("--scenario", required=True,
        help="the mission to capture the first-mission profile against")
    parser.add_argument("--out", required=True,
        help="directory to write menu.json and first-mission.json into")
    args = parser.parse_args()

    harness_py = os.path.join(os.path.dirname(os.path.abspath(__file__)), "harness.py")
    os.makedirs(args.out, exist_ok=True)

    assets_dir = args.assets or os.environ.get("OPENTS_ASSETS") or \
        os.path.expanduser("~/OpenTS-Assets/build/web")

    with tempfile.TemporaryDirectory() as tmp:
        menu_report_path = os.path.join(tmp, "menu-report.json")
        mission_report_path = os.path.join(tmp, "mission-report.json")

        # The way a visitor actually reaches the menu, rather than the shortest way a
        # harness can: the startup films play and are dismissed, and the side is chosen off
        # the screen that offers Tiberian Sun against Firestorm. That screen and the menu
        # behind it read art -- GMENU.MIX above all -- that "?nointro=1" never opens, so a
        # profile captured the short way leaves the real one fetching it a piece at a time.
        #
        # The menu also goes on loading well after it is first up, so this settles for long
        # enough to see the end of that rather than stopping at the first frame of it.
        print("capturing menu...", file=sys.stderr)
        menu_report = _run_harness(harness_py, args.bin, assets_dir, [
            "to-menu tibsun",
            "sleep 20",
            # Opening the campaign list reads the scenario descriptions and the dialog's own
            # art, which the menu behind it never touches. It is dismissed rather than
            # accepted: what a campaign then loads belongs to the first-mission profile.
            "click @NSEL_START_NEW_GAME", "wait dialog", "sleep 12",
            "click text:Cancel", "wait menu", "sleep 8",
        ], menu_report_path, timeout=360, skip_intro=False)

        # A mission with a briefing holds its restatement screen up inside the load until it
        # is dismissed, and the map behind it is what the profile is for.
        print("capturing first mission (%s)..." % args.scenario, file=sys.stderr)
        mission_report = _run_harness(harness_py, args.bin, assets_dir, [
            "wait scenario 60",
            "wait any:game|ui:text:Resume Mission 120",
            "try click text:Resume Mission",
            "wait game 120",
            "sleep 25",
        ], mission_report_path, scenario=args.scenario, timeout=400)

        # Read straight from the tree the run itself was served from, so this is exactly
        # the manifest the capture was actually read against, not a guess.
        manifest_hash = _manifest_hash(menu_report["requests"])
        if manifest_hash is None:
            raise RuntimeError("menu capture named no /assets/<hash>.json request")

        manifest_path = os.path.join(assets_dir, "assets", manifest_hash + ".json")
        with open(manifest_path) as handle:
            manifest = json.load(handle)

    menu_profile = build_profile("menu", [menu_report], manifest)
    mission_profile = subtract_profile(
        build_profile("first-mission", [mission_report], manifest), menu_profile)

    menu_out = os.path.join(args.out, "menu.json")
    mission_out = os.path.join(args.out, "first-mission.json")

    for path, profile in ((menu_out, menu_profile), (mission_out, mission_profile)):
        with open(path, "w") as handle:
            json.dump(profile, handle, indent=2)
            handle.write("\n")
        print("%s: %d entries, %d bytes" % (path, len(profile["entries"]), _total_bytes(profile)),
            file=sys.stderr)


if __name__ == "__main__":
    main()
