#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 OpenTS contributors
#
# See LICENSE.md for applicable additional terms and warranty disclaimers.

"""Pre-seed the desktop shell's offline block cache from a directory of archives.

For each archive the tool writes the pair the Tauri shell reads back: a
``<key>.dat`` holding the whole archive and a ``<key>.idx`` recording the length,
the block size, and an all-ones presence bitmap. A fully present bitmap turns
every engine read into a hit, so a seeded install fetches nothing.

The ``<key>`` and the on-disk format must match ``src-tauri/src/cache.rs``. See
README.md for the slot derivation and the format layout.
"""

import argparse
import fnmatch
import hashlib
import os
import struct
import sys
import tempfile

IDX_MAGIC = b"OTSIDX01"
DEFAULT_BLOCKSIZE = 65536


def sanitize_slot(location: str) -> str:
    """Applies BlockIndexClass::Store_Slot: non-printable ASCII becomes '?',
    truncated to 512 bytes. The engine keys the store under this string."""
    out = []
    for ch in location:
        code = ord(ch)
        out.append(ch if 0x20 <= code <= 0x7E else "?")
    slot = "".join(out)
    return slot[:512]


def key_for(slot: str) -> str:
    """Lowercase SHA-256 hex of the slot's UTF-8 bytes; the store's filename."""
    return hashlib.sha256(slot.encode("utf-8")).hexdigest()


def make_bitmap(blocks: int) -> bytes:
    """A bitmap with every block below `blocks` present (LSB-first per byte)."""
    bitmap = bytearray((blocks + 7) // 8)
    for index in range(blocks):
        bitmap[index // 8] |= 1 << (index % 8)
    return bytes(bitmap)


def write_index(path: str, length: int, blocksize: int) -> None:
    blocks = (length + blocksize - 1) // blocksize if length else 0
    header = IDX_MAGIC + struct.pack("<QIQ", length, blocksize, blocks)
    with open(path, "wb") as handle:
        handle.write(header)
        handle.write(make_bitmap(blocks))


def read_index(path: str):
    """Parses a `.idx`, returning (length, blocksize, blocks, bitmap)."""
    with open(path, "rb") as handle:
        raw = handle.read()
    if len(raw) < 28 or raw[:8] != IDX_MAGIC:
        raise ValueError("not an OpenTS cache index")
    length, blocksize, blocks = struct.unpack("<QIQ", raw[8:28])
    need = (blocks + 7) // 8
    if len(raw) < 28 + need:
        raise ValueError("index bitmap truncated")
    return length, blocksize, blocks, raw[28:28 + need]


def seed_one(archive: str, location: str, cache_dir: str, blocksize: int) -> str:
    slot = sanitize_slot(location)
    key = key_for(slot)
    dat = os.path.join(cache_dir, key + ".dat")
    idx = os.path.join(cache_dir, key + ".idx")

    length = os.path.getsize(archive)
    with open(archive, "rb") as src, open(dat, "wb") as dst:
        while True:
            chunk = src.read(1 << 20)
            if not chunk:
                break
            dst.write(chunk)

    write_index(idx, length, blocksize)
    return key


def run_seed(args) -> int:
    os.makedirs(args.cache_dir, exist_ok=True)

    names = sorted(
        name for name in os.listdir(args.source_dir)
        if os.path.isfile(os.path.join(args.source_dir, name))
        and any(fnmatch.fnmatch(name, pattern) for pattern in args.pattern)
    )

    if not names:
        print("no archives matched", ", ".join(args.pattern), file=sys.stderr)
        return 1

    for name in names:
        archive = os.path.join(args.source_dir, name)
        location = args.base + name
        key = seed_one(archive, location, args.cache_dir, args.blocksize)
        print(f"{name} -> {location}\n    slot key {key}")

    print(f"seeded {len(names)} archive(s) into {args.cache_dir}")
    return 0


def run_self_test(_args) -> int:
    """Seeds a dummy file, then reads it back through the same format logic."""
    with tempfile.TemporaryDirectory() as work:
        blocksize = 64
        payload = bytes((i * 7 + 3) & 0xFF for i in range(150))
        archive = os.path.join(work, "DUMMY.MIX")
        with open(archive, "wb") as handle:
            handle.write(payload)

        cache_dir = os.path.join(work, "iso-cache")
        os.makedirs(cache_dir)
        location = "https://play-ts.net/data/DUMMY.MIX"
        key = seed_one(archive, location, cache_dir, blocksize)

        expected = key_for(sanitize_slot(location))
        assert key == expected, "key mismatch"

        length, stored_bs, blocks, bitmap = read_index(os.path.join(cache_dir, key + ".idx"))
        assert length == len(payload), "length mismatch"
        assert stored_bs == blocksize, "blocksize mismatch"
        assert blocks == (len(payload) + blocksize - 1) // blocksize, "block count mismatch"

        for index in range(blocks):
            present = (bitmap[index // 8] >> (index % 8)) & 1
            assert present == 1, f"block {index} not marked present"

        with open(os.path.join(cache_dir, key + ".dat"), "rb") as handle:
            assert handle.read() == payload, "data mismatch"

    print("self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    seed = sub.add_parser("seed", help="seed a cache directory from archives")
    seed.add_argument("source_dir", help="directory holding the archive files")
    seed.add_argument("cache_dir", help="target iso-cache directory to fill")
    seed.add_argument(
        "--base",
        required=True,
        help="URL prefix the engine requests archives from; joined with each "
        "filename to form the location the slot is derived from. Include the "
        "full path and a trailing slash, e.g. https://play-ts.net/data/",
    )
    seed.add_argument("--blocksize", type=int, default=DEFAULT_BLOCKSIZE)
    seed.add_argument(
        "--pattern",
        action="append",
        default=None,
        help="filename glob to seed; repeatable. Defaults to *.MIX",
    )
    seed.set_defaults(func=run_seed)

    check = sub.add_parser("self-test", help="seed and read back a dummy file")
    check.set_defaults(func=run_self_test)

    args = parser.parse_args()
    if getattr(args, "pattern", None) is None and args.command == "seed":
        args.pattern = ["*.MIX"]
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
