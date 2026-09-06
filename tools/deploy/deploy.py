#!/usr/bin/env python3
"""The one way to put a browser build in front of players.

Two deployments exist and they differ only at the end.  ``local`` builds the
images and brings the compose stack up on 8765, which is where a change is
looked at first.  ``remote`` builds the same images, streams them to the host
over SSH, and restarts the stack already running there.  Everything before that
-- the asset tree a deployment serves and the signing key the engine tree is
signed with -- is the same work, so it is done once, here.

Fetch profiles are not this script's business.  A release names its own in
assets.json, published with it by OpenTS-Assets through its tools/tsprofile.py
and this repository's tools/harness/pgo_pipeline.py.  What happens here is a
check that the tree about to be served actually carries them, because a release
missing them still loads, just more slowly, and nothing else would say so.

Standard library only, like the harness it drives.  Run ``deploy.py local
--dry-run`` to see what it would do.
"""

import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import urllib.error
import urllib.request


ROOT = Path(__file__).resolve().parents[2]
ENGINE_IMAGE = "opents-engine"
RELAY_IMAGE = "opents-relay"
SIGNING_KEY = ROOT / ".sigkey"
DEFAULT_ASSETS = Path.home() / "OpenTS-Assets" / "build" / "web"
DEFAULT_REMOTE_DIR = "opents"

# The documents a shared cache is allowed to hold for a day, and so the ones a release has
# to drop from the edge before it can be said to have shipped. Everything else a page loads
# is named for its own content and never needs dropping.
PURGE_PATHS = ("/", "/index.html", "/sw.js", "/assets.json", "/downloads.json",
               "/relay.json", "/manifest.webmanifest", "/engine.json")
DEFAULT_DOWNLOADS = ROOT / "downloads"
PROFILE_KINDS = ("menu", "first-mission")


def load_env():
    """Reads KEY=VALUE lines from the repository's .env into the environment,
    without overriding anything already set there.

    This is where the Cloudflare purge token lives. The file is ignored by git
    and must stay that way; nothing here ever prints a value from it.
    """
    path = ROOT / ".env"

    if not path.is_file():
        return

    for line in path.read_text().splitlines():
        line = line.strip()

        if not line or line.startswith("#") or "=" not in line:
            continue

        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


def say(message):
    print(message, flush=True)


def run(command, dry_run=False, capture=False, **kwargs):
    """Run a command, reporting it first.  A failure ends the deployment."""

    printable = " ".join(shlex.quote(str(part)) for part in command)
    say("  $ %s" % printable)

    if dry_run:
        return ""

    if capture:
        done = subprocess.run(command, check=False, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
        if done.returncode != 0:
            raise SystemExit("failed: %s\n%s" % (printable, done.stderr.strip()))
        return done.stdout

    if subprocess.run(command, check=False, **kwargs).returncode != 0:
        raise SystemExit("failed: %s" % printable)

    return ""


def release_pointer(assets):
    """The assets.json an asset tree is currently publishing."""

    path = Path(assets) / "assets.json"

    if not path.is_file():
        raise SystemExit("no assets.json under %s; point --assets at an "
                         "OpenTS-Assets build/web tree" % assets)

    return json.loads(path.read_text("utf-8"))


def check_profiles(assets, pointer, strict):
    """Report whether the release carries usable fetch profiles.

    A profile that is absent or that holds no ranges loads the same way -- as no
    profile at all -- and the engine reports neither, so the difference is only
    visible here.
    """

    named = pointer.get("profiles", {})
    faults = []
    banked = 0

    for kind in PROFILE_KINDS:
        if kind not in named:
            faults.append("%s missing" % kind)
            continue

        path = Path(assets).joinpath(*named[kind].split("/"))

        try:
            profile = json.loads(path.read_text("utf-8"))
        except (ValueError, OSError):
            faults.append("%s unreadable" % kind)
            continue

        if not profile.get("entries"):
            faults.append("%s has no ranges" % kind)
        else:
            banked += sum(stop - start
                          for entry in profile["entries"]
                          for start, stop in entry["ranges"])

    if not faults:
        say("profiles: both published (%s bytes banked)" % f"{banked:,}")
        return

    message = "profiles: %s" % ", ".join(faults)

    if strict:
        raise SystemExit("%s\nrebuild the release with profiles, or drop "
                         "--require-profiles to deploy without them" % message)

    say(message)
    say("profiles: the release loads correctly, just more slowly; rebuild it "
        "with OpenTS-Assets to capture them")


def check_downloads(downloads):
    """Report the native installers this deployment offers.

    A build the pointer names but does not hold is a 404 behind a link the page
    shows anyway, which nothing else would surface.
    """

    pointer = Path(downloads) / "downloads.json"

    if not pointer.is_file():
        say("downloads: none published, so the page offers no desktop app")
        return

    try:
        builds = json.loads(pointer.read_text("utf-8")).get("builds", [])
    except ValueError:
        raise SystemExit("%s is not readable JSON" % pointer)

    faults = []
    named = []

    for build in builds:
        path = Path(downloads).joinpath(*build["path"].split("/"))
        where = "%s/%s" % (build["os"], build["arch"])

        if not path.is_file():
            faults.append("%s is missing its file" % where)
        elif path.stat().st_size != build["size"]:
            faults.append("%s has the wrong size" % where)
        else:
            named.append(where + ("" if build.get("signed") else " (unsigned)"))

    if faults:
        raise SystemExit("downloads: %s" % ", ".join(faults))

    say("downloads: %s" % (", ".join(named) if named else "none published"))


def purge_edge(options):
    """Drops the mutable pointers from Cloudflare, so the release just installed
    is the one served rather than the one the edge is still holding.

    A deployment that cannot purge has not shipped: the pointers carry a day of
    shared-cache lifetime, so the previous release would go on being served with
    no way to correct it. That is a failed deploy, not a warning.
    """
    if not options.public_url:
        say("purge: skipped; no --public-url, so nothing names what to drop")
        say("        the previous release stays at the edge until it expires")
        return

    token = os.environ.get("CLOUDFLARE_TOKEN")
    zone = os.environ.get("CLOUDFLARE_ZONE_ID")

    base = options.public_url.rstrip("/")
    files = [base + path for path in PURGE_PATHS]

    # A token carrying only Zone.Cache Purge cannot read the zone list, so the
    # zone is named rather than looked up.
    if not (token and zone):
        message = ("cannot purge %s: set CLOUDFLARE_TOKEN and CLOUDFLARE_ZONE_ID "
                   "in .env, or pass --no-purge to accept a release the edge "
                   "will not show for a day" % options.public_url)

        if not options.dry_run:
            raise SystemExit(message)

        say("purge: %s" % message)

    say("purge: asking Cloudflare to drop %d pointers from the edge" % len(files))

    if options.dry_run:
        say("  $ POST api.cloudflare.com/client/v4/zones/<zone>/purge_cache")
        for url in files:
            say("        files[]: %s" % url)
        return

    request = urllib.request.Request(
        "https://api.cloudflare.com/client/v4/zones/%s/purge_cache" % zone,
        data=json.dumps({"files": files}).encode(),
        headers={"Authorization": "Bearer %s" % token,
                 "Content-Type": "application/json"},
        method="POST")

    try:
        with urllib.request.urlopen(request, timeout=30) as answer:
            body = json.loads(answer.read().decode())
    except urllib.error.URLError as error:
        raise SystemExit("the purge did not reach Cloudflare: %s" % error)

    if not body.get("success"):
        raise SystemExit("Cloudflare refused the purge: %s"
                         % json.dumps(body.get("errors", body)))

    say("purge: the edge is holding none of them now")


def build_images(options):
    """Build both images, signing the engine tree when the key is here."""

    engine = ["docker", "build", "-t", ENGINE_IMAGE]

    if SIGNING_KEY.is_file():
        engine += ["--secret", "id=engine_signing_key,src=%s" % SIGNING_KEY]
        say("engine: signing the update tree with %s" % SIGNING_KEY.name)
    else:
        say("engine: no %s, so the update tree is left empty and a shell falls back "
            "to its bundled copy" % SIGNING_KEY.name)

    if options.platform:
        engine += ["--platform", options.platform]

    # The image build keeps both engine trees in cache mounts, so an ordinary build makes
    # only what changed. What reaches players is built from nothing instead: an incremental
    # build is only as trustworthy as the cache it stood on, and that is not a thing to
    # find out about afterwards.
    if options.clean:
        say("engine: building from nothing, as a release is")
        engine += ["--build-arg", "OPENTS_CLEAN_BUILD=1"]
    else:
        say("engine: reusing the build cache; pass --clean to build from nothing")

    run(engine + [str(ROOT)], dry_run=options.dry_run, cwd=ROOT)

    relay = ["docker", "build", "-t", RELAY_IMAGE]

    if options.platform:
        relay += ["--platform", options.platform]

    run(relay + [str(ROOT / "tools" / "relay")], dry_run=options.dry_run, cwd=ROOT)


def deploy_local(options):
    assets = Path(options.assets).expanduser()

    say("local: serving %s on port %s" % (assets, options.port))
    check_profiles(assets, release_pointer(assets), options.require_profiles)
    check_downloads(Path(options.downloads).expanduser())
    build_images(options)

    environment = dict(os.environ)
    environment["OPENTS_ASSETS"] = str(assets)
    environment["OPENTS_DOWNLOADS"] = str(Path(options.downloads).expanduser())
    environment["OPENTS_PORT"] = str(options.port)

    say("local: bringing the stack up")
    run(["docker", "compose", "up", "-d", "--no-build"],
        dry_run=options.dry_run, cwd=ROOT, env=environment)
    say("local: http://localhost:%s" % options.port)


def deploy_remote(options):
    if not options.host:
        raise SystemExit("no host to deploy to; pass --host or set OPENTS_DEPLOY_HOST")

    assets = Path(options.assets).expanduser()

    say("remote: %s:%s" % (options.host, options.remote_dir))
    check_profiles(assets, release_pointer(assets), options.require_profiles)
    check_downloads(Path(options.downloads).expanduser())
    build_images(options)

    say("remote: streaming both images over SSH")
    save = subprocess.Popen(["docker", "save", ENGINE_IMAGE, RELAY_IMAGE],
                            stdout=subprocess.PIPE)

    if options.dry_run:
        save.terminate()
        say("  $ docker save %s %s | ssh %s docker load"
            % (ENGINE_IMAGE, RELAY_IMAGE, options.host))
    else:
        load = subprocess.run(["ssh", options.host, "docker load"],
                              stdin=save.stdout, check=False)
        save.stdout.close()
        save.wait()

        if save.returncode != 0 or load.returncode != 0:
            raise SystemExit("the image did not reach %s" % options.host)

    say("remote: restarting the stack")
    run(["ssh", options.host,
         "cd %s && docker compose up -d --no-build" % shlex.quote(options.remote_dir)],
        dry_run=options.dry_run)

    if options.no_purge:
        say("purge: not asked for; the edge serves the previous release "
            "until its pointers expire")
    else:
        purge_edge(options)


def parser():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--assets", default=os.environ.get("OPENTS_ASSETS", DEFAULT_ASSETS),
                        help="the OpenTS-Assets build/web tree to serve "
                             "(default: $OPENTS_ASSETS, else %s)" % DEFAULT_ASSETS)
    common.add_argument("--downloads", default=os.environ.get("OPENTS_DOWNLOADS", DEFAULT_DOWNLOADS),
                        help="the tree of native installers the page offers "
                             "(default: $OPENTS_DOWNLOADS, else %s)" % DEFAULT_DOWNLOADS)
    common.add_argument("--require-profiles", action="store_true",
                        help="refuse to deploy a release whose fetch profiles are "
                             "missing or do not match it")
    common.add_argument("--platform", default=None,
                        help="build for this platform rather than this machine's, "
                             "as linux/amd64")
    common.add_argument("--public-url",
                        default=os.environ.get("OPENTS_PUBLIC_URL"),
                        help="the origin the deployment is served as, whose "
                             "pointers are dropped from the edge once it is "
                             "installed (default: $OPENTS_PUBLIC_URL)")
    common.add_argument("--no-purge", action="store_true",
                        help="install without dropping the old pointers, "
                             "accepting that the edge serves the previous "
                             "release until they expire")
    # Neither carries a default of its own: a subparser cannot hold one, because parents=
    # shares the action itself between local and remote and the last default set wins for
    # both. main() settles it from the command instead.
    common.add_argument("--clean", dest="clean", action="store_true", default=None,
                        help="build the engine from nothing rather than from the build "
                             "cache; the default for a remote deployment")
    common.add_argument("--cached", dest="clean", action="store_false", default=None,
                        help="reuse the build cache even for a remote deployment")
    common.add_argument("--dry-run", action="store_true",
                        help="report every command without running one")

    result = argparse.ArgumentParser(
        description="Deploy the browser build locally or to a remote host.")
    commands = result.add_subparsers(dest="command", required=True)

    local = commands.add_parser("local", parents=[common],
                                help="build and serve on this machine")
    local.add_argument("--port", default=os.environ.get("OPENTS_PORT", "8765"),
                       help="the port to publish (default: 8765)")
    local.set_defaults(handler=deploy_local)

    remote = commands.add_parser("remote", parents=[common],
                                 help="build here, then load and restart on the host")
    remote.add_argument("--host", default=os.environ.get("OPENTS_DEPLOY_HOST"),
                        help="the SSH host to deploy to (default: $OPENTS_DEPLOY_HOST)")
    remote.add_argument("--remote-dir",
                        default=os.environ.get("OPENTS_DEPLOY_DIR",
                                               DEFAULT_REMOTE_DIR),
                        help="where the compose stack lives there, relative to the "
                             "login directory unless absolute "
                             "(default: $OPENTS_DEPLOY_DIR, else %s)"
                             % DEFAULT_REMOTE_DIR)
    remote.set_defaults(handler=deploy_remote)

    return result


def main():
    load_env()
    options = parser().parse_args()

    # What reaches players is built from nothing; what is being looked at is not.
    if options.clean is None:
        options.clean = (options.command == "remote")
    options.handler(options)
    say("done")


if __name__ == "__main__":
    main()
