"""Turns one or more harness --report captures into a fetch profile.

A profile names the byte ranges of the manifest's own archives and films a session
actually read reaching some point in the game -- the main menu, or the first mission --
tied to the exact manifest they were read against, since a name can point at different
bytes in a different build. Entries are keyed by the manifest's own "name" field (what
Win32_Hint_File and the rest of the engine already resolve by), not by the content-hashed
URL a request happened to use, so a client applies a profile the same way it already
answers any other archive lookup.

Usage:
    python3 tools/harness/pgo_profile.py --kind menu --manifest path/to/<hash>.json \\
        --out menu.json report1.json report2.json

Multiple reports are unioned into one profile, which is how a menu profile covering more
than one way to reach the menu is built: capture each separately with the harness, then
union them here.
"""

import argparse
import json
import re
import sys


_MANIFEST_PATH = re.compile(r"^/assets/([0-9a-f]{64,})\.json$")

# Only a request under a directory the manifest names is one a client can turn back into a
# hint: the manifest itself has to be fetched by ordinary means before any name in it can be
# resolved at all, so hinting its own fetch is not something a profile can act on, and
# assets.json is never cached regardless. The directories come from the manifest rather than
# a constant, so a release that moves them stays readable here.

# What the store holds and what a read is answered in, per BLOCK_SIZE in code/httpsource.h.
# A profile names whole blocks so that what it fetches is what a later read asks after.
BLOCK_SIZE = 64 * 1024


def _manifest_hash(requests):
    """The manifest hash every ranged request in this capture was actually read against."""

    for request in requests:
        match = _MANIFEST_PATH.match(request["path"])
        if match:
            return match.group(1)

    return None


def _merge_ranges(ranges):
    """The capture's ranges as whole blocks, overlaps merged, each named once.

    Rounded out to block boundaries because that is the granularity everything downstream
    works in: the store holds whole blocks and answers a later read by asking whether it
    holds each block the read covers, and the prefetch widens whatever it is given to the
    blocks around it anyway. Merging what that rounding makes adjacent is what keeps a
    block the capture happened to touch from several ranges from being fetched once per
    range.
    """

    blocks = set()

    for start, stop in ranges:
        if stop <= start:
            continue
        for index in range(start // BLOCK_SIZE, (stop - 1) // BLOCK_SIZE + 1):
            blocks.add(index)

    merged = []

    for index in sorted(blocks):
        start = index * BLOCK_SIZE
        stop = start + BLOCK_SIZE

        if merged and start == merged[-1][1]:
            merged[-1] = (merged[-1][0], stop)
        else:
            merged.append((start, stop))

    return merged


def _path_to_name(manifest):
    """The manifest's own path -> name mapping, the reverse of the lookup a name resolves
    through to reach a URL. A record with no "path" (none currently) has nothing here to
    ask for and is silently unreachable, the same as it would be in any other lookup."""

    mapping = {}

    for group in manifest.values():
        if not isinstance(group, list):
            continue
        for record in group:
            if isinstance(record, dict) and "path" in record and "name" in record:
                mapping["/" + record["path"]] = record["name"]

    return mapping


def build_profile(kind, reports, manifest):
    """`manifest` is the parsed manifest.json every report was captured against -- what
    turns a request's content-hashed path back into the plain name a client resolves by."""

    manifest_hash = None
    path_to_name = _path_to_name(manifest)
    file_prefixes = {path.rsplit("/", 1)[0] + "/" for path in path_to_name if "/" in path}
    entries = {}
    unresolved = set()

    for report in reports:
        requests = report["requests"]
        found = _manifest_hash(requests)

        if found is None:
            raise ValueError("a capture named no /assets/<hash>.json request -- "
                "was the run pointed at a real asset tree?")

        if manifest_hash is None:
            manifest_hash = found
        elif manifest_hash != found:
            raise ValueError("captures disagree on which manifest they were read against "
                "(%s vs %s) -- they must all be against the same asset build" %
                (manifest_hash, found))

        for request in requests:
            path = request["path"]

            if not any(path.startswith(prefix) for prefix in file_prefixes):
                continue
            if request["status"] not in (200, 206):
                continue

            name = path_to_name.get(path)
            if name is None:
                unresolved.add(path)
                continue

            # Only archives are read through the block reader the store sits under. A film
            # or a music track is handed to the page's own <video>/<audio> element by URL
            # (code/mp4.cpp, code/musicbackend.cpp), which fetches it whole and by its own
            # means, so naming one here buys nothing and costs fetching it twice.
            if not name.upper().endswith(".MIX"):
                continue

            ranges = entries.setdefault(name, [])
            ranges.append((request["start"], request["start"] + request["length"]))

    if unresolved:
        raise ValueError("%d request path(s) named in the capture are not in the manifest "
            "given (wrong manifest for these reports?): %s" %
            (len(unresolved), ", ".join(sorted(unresolved)[:5])))

    ordered_entries = []
    for name in sorted(entries):
        merged = _merge_ranges(entries[name])
        ordered_entries.append({
            "name": name,
            "ranges": [[start, stop] for start, stop in merged],
        })

    return {
        "format": "opents-fetch-profile-v1",
        "kind": kind,
        "entries": ordered_entries,
    }


def _subtract_ranges(ranges, covered):
    """The blocks `ranges` names that `covered` does not, merged back into spans.

    Block granularity for the same reason _merge_ranges uses it: what an earlier stage
    banked, it banked a block at a time, so a block that stage already holds is one this
    stage has no reason to name however the two captures happened to divide their reads.
    """

    held = set()
    for start, stop in covered:
        held.update(range(start // BLOCK_SIZE, (stop - 1) // BLOCK_SIZE + 1))

    wanted = set()
    for start, stop in ranges:
        wanted.update(range(start // BLOCK_SIZE, (stop - 1) // BLOCK_SIZE + 1))

    return _merge_ranges([(index * BLOCK_SIZE, (index + 1) * BLOCK_SIZE)
        for index in sorted(wanted - held)])


def subtract_profile(profile, baseline):
    """A profile carrying only what `profile` names beyond what `baseline` already does.

    This is how a later stage's profile (first-mission) is kept from re-naming bytes an
    earlier stage's profile (menu) already covers -- a client that fetched the menu
    profile first has no reason to fetch those ranges again just because this stage's
    capture happened to touch them too.
    """

    covered = {entry["name"]: [tuple(r) for r in entry["ranges"]] for entry in baseline["entries"]}

    ordered_entries = []
    for entry in profile["entries"]:
        ranges = [tuple(r) for r in entry["ranges"]]
        if entry["name"] in covered:
            ranges = _subtract_ranges(ranges, covered[entry["name"]])
        if ranges:
            ordered_entries.append({"name": entry["name"], "ranges": [list(r) for r in ranges]})

    return {**profile, "entries": ordered_entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--kind", required=True, choices=["menu", "first-mission"],
        help="what reaching this point in the game means")
    parser.add_argument("--manifest", required=True,
        help="the manifest.json every report was captured against")
    parser.add_argument("--out", required=True, help="where to write the profile")
    parser.add_argument("--subtract", metavar="PROFILE",
        help="an earlier-stage profile whose ranges should be left out of this one")
    parser.add_argument("reports", nargs="+", help="one or more harness --report captures")
    args = parser.parse_args()

    with open(args.manifest) as handle:
        manifest = json.load(handle)

    reports = []
    for path in args.reports:
        with open(path) as handle:
            reports.append(json.load(handle))

    profile = build_profile(args.kind, reports, manifest)

    if args.subtract:
        with open(args.subtract) as handle:
            baseline = json.load(handle)
        profile = subtract_profile(profile, baseline)

    with open(args.out, "w") as handle:
        json.dump(profile, handle, indent=2)
        handle.write("\n")

    total_bytes = sum(stop - start for entry in profile["entries"] for start, stop in entry["ranges"])
    print("%s: %d entries, %d bytes" % (
        args.out, len(profile["entries"]), total_bytes), file=sys.stderr)


if __name__ == "__main__":
    main()
