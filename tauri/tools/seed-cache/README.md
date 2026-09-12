# seed-cache

Pre-populates the desktop shell's offline block cache so a fresh, network-free
install plays without fetching. It writes the same `<key>.dat` / `<key>.idx`
pair the shell reads in `src-tauri/src/cache.rs`.

The tool is standard-library Python 3; it takes files as arguments and bundles
no game assets.

## Use

```
python seed_cache.py seed <source_dir> <cache_dir> --base <url-prefix> [--blocksize N] [--pattern GLOB]...
python seed_cache.py self-test
```

- `source_dir` holds the archive files (for example the `*.MIX` set).
- `cache_dir` is the shell's `iso-cache` directory. At runtime the shell resolves
  it as Tauri's `app_local_data_dir()` joined with `iso-cache/`. The installer
  seeds that same path.
- `--base` is the URL prefix the engine fetches each archive from. It is joined
  to the filename by plain concatenation, so include the full path and a
  trailing slash, e.g. `https://play-ts.net/data/`.
- `--blocksize` defaults to 65536, the engine's `BLOCK_UNIT_SIZE`. Leave it
  unless the engine constant changes.
- `--pattern` is a repeatable filename glob; it defaults to `*.MIX`.

`self-test` seeds a small generated file into a temporary directory and reads it
back through the same format code, checking the key, the length, the block
count, an all-ones bitmap, and the stored bytes. It needs no game data.

## Slot derivation

The engine identifies a cached archive by its **slot**, and the shell names the
store files by hashing that slot. Both must match this tool exactly.

- **Location.** The absolute URL the engine requested the archive from (its
  `Location`, never a redirect target). This tool forms it as `base + filename`.
  The `--base` value must reproduce exactly what the engine resolves, including
  scheme, host, path, and case.
- **Slot.** `BlockIndexClass::Store_Slot` in `code/httpsource.cpp`: the location
  string with every byte outside printable ASCII (`0x20`–`0x7E`) replaced by
  `?`, truncated to 512 bytes. URLs are normally already printable ASCII.
- **Key.** Lowercase SHA-256 hex of the slot's UTF-8 bytes. `<key>.dat` and
  `<key>.idx` use this 64-character name.

Assumption: the engine's block store keys on the location alone (length is not
part of the slot, only of the signature), so a seeded store matches whatever
length the archive actually is, and the shell's `cache_probe` accepts it when
the recorded length equals the length the engine reports. If a future engine
change alters `Store_Slot`, update `sanitize_slot` here and `sha256_hex` input
in `cache.rs` together.

## On-disk format

Per slot, in the cache directory:

- `<key>.dat` — the archive bytes, block `N` at byte offset `N * blocksize`. The
  shell keeps this sparse on Windows; a full seed writes the whole file.
- `<key>.idx` — little-endian metadata:

  | offset | size | field |
  | --- | --- | --- |
  | 0 | 8 | magic `OTSIDX01` |
  | 8 | 8 | image length, bytes (u64) |
  | 16 | 4 | block size, bytes (u32) |
  | 20 | 8 | block count = `ceil(length / blocksize)` (u64) |
  | 28 | `ceil(block_count / 8)` | presence bitmap |

  In the bitmap, block `i` is bit `i % 8` of byte `i / 8`, least significant bit
  first. A seed sets every bit below the block count.
