#!/usr/bin/env python3
"""Restore native filenames from a local OpenTS web asset manifest."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil


def inside(root, name):
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f"Path escapes asset tree: {name}")
    return path


def prepare(source, output):
    pointer = json.loads((source / "assets.json").read_text())
    if pointer["format"] != "opents-web-assets-pointer-v1":
        raise ValueError("Unsupported asset pointer format")
    raw = inside(source, pointer["manifest"]).read_bytes()
    if hashlib.sha256(raw).hexdigest() != pointer["sha256"]:
        raise ValueError("Asset manifest checksum mismatch")
    manifest = json.loads(raw)
    files = [item for item in manifest["files"] if (item["name"] or "").upper().endswith(".MIX")]
    names = [item["name"].upper() for item in files]
    for required in ("TIBSUN.MIX", "SIDECD01.MIX", "MAPS01.MIX"):
        if required not in names:
            raise ValueError(f"Missing required archive: {required}")
    if len(set(names)) != len(names):
        raise ValueError("Duplicate archive names")
    for item in files:
        if Path(item["name"]).name != item["name"]:
            raise ValueError("Archive name must be a filename")
        path = inside(source, item["path"])
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if path.stat().st_size != item["size"] or digest != item["sha256"]:
            raise ValueError(f"Archive checksum mismatch: {path}")
    output.mkdir(parents=True, exist_ok=True)
    for item in files:
        shutil.copyfile(inside(source, item["path"]), output / item["name"].upper())
    print(f"Prepared {len(files)} verified archives in {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    prepare(args.source, args.output)
