#!/usr/bin/env python3
"""Publish native installers into the tree the browser page offers them from.

The page reads downloads.json, which names one build per operating system and
architecture. Each installer is stored under its own content hash so it can be
cached forever, and downloads.json is the one mutable file, exactly as
assets.json is for a release.

A build is published from whichever machine can produce it -- a macOS disk image
from a Mac, an AppImage from a Linux container -- so this merges into whatever
the tree already holds rather than replacing it. Publishing a build for an
operating system and architecture already present replaces that one alone.

Standard library only. Run with --check to verify a tree without changing it.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


FORMAT = "opents-web-downloads-v1"
POINTER_NAME = "downloads.json"
FILE_DIRECTORY = "downloads/files"
HASH_NAME_LENGTH = 16

# What the extension says about the platform, which is the only part of a Tauri
# bundle name that is not a convention it could change.
BY_EXTENSION = {
    ".dmg": "macos",
    ".appimage": "linux",
    ".deb": "linux",
    ".rpm": "linux",
    ".exe": "windows",
    ".msi": "windows",
}

ARCH_TOKENS = {
    "aarch64": "aarch64", "arm64": "aarch64",
    "x86_64": "x86_64", "amd64": "x86_64", "x64": "x86_64", "i686": "i686",
}

VERSION = re.compile(r"(?<![0-9])(\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?)(?![0-9])")


def fail(message):
    raise SystemExit("publish: %s" % message)


def digest_of(path):
    reader = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            reader.update(block)
    return reader.hexdigest()


def describe(path, options):
    """What the page needs to know about one installer, read from its name."""

    name = path.name
    extension = path.suffix.lower()
    system = options.os or BY_EXTENSION.get(extension)

    if system is None:
        fail("%s: nothing identifies the platform; pass --os" % name)

    arch = options.arch

    if arch is None:
        for token in re.split(r"[._\-]", name.lower()):
            if token in ARCH_TOKENS:
                arch = ARCH_TOKENS[token]
                break

    if arch is None:
        fail("%s: nothing identifies the architecture; pass --arch" % name)

    version = options.version

    if version is None:
        found = VERSION.search(name)
        version = found.group(1) if found else None

    if version is None:
        fail("%s: nothing identifies the version; pass --version" % name)

    return system, arch, version


def is_signed(path, options):
    """Whether the page should present the build as one the OS will accept.

    Only a stapled macOS bundle can be established here, and only on a Mac. Every
    other answer is what the caller states, defaulting to unsigned, because a
    page that promises a signature the file does not carry is worse than one that
    warns about a warning the visitor then does not see.
    """

    if options.signed:
        return True
    if options.unsigned:
        return False
    if path.suffix.lower() != ".dmg" or not shutil.which("xcrun"):
        return False

    done = subprocess.run(["xcrun", "stapler", "validate", str(path)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return done.returncode == 0


def read_pointer(tree):
    path = tree / POINTER_NAME

    if not path.is_file():
        return {"format": FORMAT, "builds": []}

    try:
        pointer = json.loads(path.read_text("utf-8"))
    except ValueError as error:
        fail("%s is not readable JSON: %s" % (path, error))

    if pointer.get("format") != FORMAT:
        fail("%s has an unsupported format" % path)
    if not isinstance(pointer.get("builds"), list):
        fail("%s names no builds list" % path)

    return pointer


def write_pointer(tree, builds):
    builds.sort(key=lambda build: (build["os"], build["arch"]))
    pointer = {"format": FORMAT, "builds": builds}
    encoded = json.dumps(pointer, indent=2, sort_keys=True) + "\n"
    temporary = tree / (POINTER_NAME + ".new")
    temporary.write_text(encoded, "utf-8")
    temporary.replace(tree / POINTER_NAME)
    return pointer


def publish(options):
    tree = Path(options.tree).expanduser()
    files = tree / FILE_DIRECTORY
    files.mkdir(parents=True, exist_ok=True)

    pointer = read_pointer(tree)
    builds = {(build["os"], build["arch"]): build for build in pointer["builds"]}
    superseded = {build["path"] for build in pointer["builds"]}

    for name in options.installer:
        source = Path(name).expanduser()

        if not source.is_file():
            fail("%s is not a file" % source)

        system, arch, version = describe(source, options)
        digest = digest_of(source)
        stem, extension = source.stem, source.suffix
        relative = "%s/%s.%s%s" % (FILE_DIRECTORY, stem, digest[:HASH_NAME_LENGTH], extension)
        destination = tree / relative

        if not destination.exists():
            shutil.copyfile(source, destination)
            os.chmod(destination, 0o644)

        builds[(system, arch)] = {
            "os": system,
            "arch": arch,
            "version": version,
            "name": source.name,
            "path": relative,
            "size": source.stat().st_size,
            "sha256": digest,
            "signed": is_signed(source, options),
        }

        print("%-8s %-8s %-8s %s (%s bytes)"
              % (system, arch, version, relative, f"{source.stat().st_size:,}"))

    published = write_pointer(tree, list(builds.values()))

    # A tree holds only what it offers, so an installer the pointer no longer
    # names is taken away rather than left to accumulate one release per build.
    kept = {build["path"] for build in published["builds"]}

    for stale in sorted(superseded - kept):
        path = tree / stale
        if path.is_file():
            path.unlink()
            print("removed %s" % stale)

    return published


def check(options):
    """Verify every build the pointer names is present and unaltered."""

    tree = Path(options.tree).expanduser()
    pointer = read_pointer(tree)
    faults = []

    for build in pointer["builds"]:
        path = tree / build["path"]

        if not path.is_file():
            faults.append("%s/%s: %s is missing" % (build["os"], build["arch"], build["path"]))
            continue
        if path.stat().st_size != build["size"]:
            faults.append("%s/%s: %s has the wrong size" % (build["os"], build["arch"], build["path"]))
            continue
        if digest_of(path) != build["sha256"]:
            faults.append("%s/%s: %s does not match its hash"
                          % (build["os"], build["arch"], build["path"]))

    names = {(build["os"], build["arch"]) for build in pointer["builds"]}
    actual = {
        os.path.relpath(os.path.join(base, name), tree).replace(os.sep, "/")
        for base, _, filenames in os.walk(tree / FILE_DIRECTORY)
        for name in filenames
    }
    unexpected = sorted(actual - {build["path"] for build in pointer["builds"]})

    if unexpected:
        faults.append("files the pointer does not name: %r" % unexpected)

    for fault in faults:
        print("fault: %s" % fault, file=sys.stderr)

    if faults:
        raise SystemExit(1)

    print("%d builds published: %s" % (
        len(pointer["builds"]),
        ", ".join("%s/%s %s" % (system, arch, "signed" if next(
            build for build in pointer["builds"]
            if (build["os"], build["arch"]) == (system, arch)).get("signed") else "unsigned")
            for system, arch in sorted(names))))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("installer", nargs="*",
        help="the installer files to publish")
    parser.add_argument("--tree", required=True,
        help="the directory served at the site root, which holds downloads.json")
    parser.add_argument("--check", action="store_true",
        help="verify the tree and change nothing")
    parser.add_argument("--os", default=None,
        help="override the platform read from the file name")
    parser.add_argument("--arch", default=None,
        help="override the architecture read from the file name")
    parser.add_argument("--version", default=None,
        help="override the version read from the file name")
    parser.add_argument("--signed", action="store_true",
        help="state that the operating system will accept the build without a warning")
    parser.add_argument("--unsigned", action="store_true",
        help="state that it will not, which the page shows the visitor")
    options = parser.parse_args()

    if options.signed and options.unsigned:
        fail("--signed and --unsigned contradict each other")

    if options.check:
        return check(options)

    if not options.installer:
        fail("name at least one installer, or pass --check")

    publish(options)
    return 0


if __name__ == "__main__":
    sys.exit(main())
