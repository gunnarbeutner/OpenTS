/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// A native block cache for the desktop shell. It mirrors the block store the
// WebAssembly engine otherwise keeps in browser storage, but as host files an
// offline installer can pre-seed. Each archive image is one slot, held under a
// `<key>.dat` sparse image and a `<key>.idx` metadata sidecar in the app's
// local-data cache directory. The `.idx` bitmap is the source of truth for
// block presence, so a fully seeded store makes every read a hit and the engine
// fetches nothing.

use std::{
    fs,
    io::{Read, Seek, SeekFrom, Write},
    path::PathBuf,
    sync::Mutex,
};

use tauri::{ipc::Response, AppHandle, Manager};

// One process serves reads and writes one at a time; the engine issues them
// serially, and this keeps a stray concurrent invoke off the same files.
static LOCK: Mutex<()> = Mutex::new(());

// What cache_room reports when free space is not consulted: enough that the
// engine never sizes itself down. It is advisory and never authoritative.
const ROOM_CONSTANT: f64 = 8.0 * 1024.0 * 1024.0 * 1024.0;

// First bytes of a `.idx` file. The seeder writes the same tag.
const IDX_MAGIC: &[u8; 8] = b"OTSIDX01";

/// The parsed `.idx` sidecar: the image length, the block size it was cut with,
/// and one presence bit per block. `blocks` is the block count the bitmap sizes
/// to, which is `ceil(length / blocksize)`.
struct Index {
    length: u64,
    blocksize: u32,
    blocks: u64,
    bitmap: Vec<u8>,
}

impl Index {
    fn new(blocksize: u32) -> Self {
        Index { length: 0, blocksize, blocks: 0, bitmap: Vec::new() }
    }

    /// Parses a `.idx` file, returning None when it is absent or malformed.
    fn read(path: &PathBuf) -> Option<Index> {
        let raw = fs::read(path).ok()?;
        if raw.len() < 28 || &raw[0..8] != IDX_MAGIC {
            return None;
        }

        let length = u64::from_le_bytes(raw[8..16].try_into().ok()?);
        let blocksize = u32::from_le_bytes(raw[16..20].try_into().ok()?);
        let blocks = u64::from_le_bytes(raw[20..28].try_into().ok()?);

        if blocksize == 0 {
            return None;
        }

        let need = ((blocks + 7) / 8) as usize;
        if raw.len() < 28 + need {
            return None;
        }

        Some(Index { length, blocksize, blocks, bitmap: raw[28..28 + need].to_vec() })
    }

    fn write(&self, path: &PathBuf) -> Result<(), String> {
        let mut raw = Vec::with_capacity(28 + self.bitmap.len());
        raw.extend_from_slice(IDX_MAGIC);
        raw.extend_from_slice(&self.length.to_le_bytes());
        raw.extend_from_slice(&self.blocksize.to_le_bytes());
        raw.extend_from_slice(&self.blocks.to_le_bytes());
        raw.extend_from_slice(&self.bitmap);
        fs::write(path, &raw).map_err(|e| e.to_string())
    }

    fn holds(&self, index: u64) -> bool {
        if index >= self.blocks {
            return false;
        }
        let byte = (index / 8) as usize;
        byte < self.bitmap.len() && (self.bitmap[byte] & (1u8 << (index % 8))) != 0
    }

    /// Grows the bitmap so it can address `blocks` blocks.
    fn reserve(&mut self, blocks: u64) {
        if blocks > self.blocks {
            self.blocks = blocks;
        }
        let need = ((self.blocks + 7) / 8) as usize;
        if self.bitmap.len() < need {
            self.bitmap.resize(need, 0);
        }
    }

    fn mark(&mut self, index: u64) {
        self.reserve(index + 1);
        let byte = (index / 8) as usize;
        self.bitmap[byte] |= 1u8 << (index % 8);
    }
}

/// The cache directory, created if it does not yet exist.
fn cache_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let dir = app.path().app_local_data_dir().map_err(|e| e.to_string())?.join("iso-cache");
    fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    Ok(dir)
}

/// The `<key>.dat` and `<key>.idx` paths for a slot.
fn store_paths(app: &AppHandle, slot: &str) -> Result<(PathBuf, PathBuf), String> {
    let dir = cache_dir(app)?;
    let key = sha256_hex(slot.as_bytes());
    Ok((dir.join(format!("{key}.dat")), dir.join(format!("{key}.idx"))))
}

fn remove_store(dat: &PathBuf, idx: &PathBuf) {
    let _ = fs::remove_file(dat);
    let _ = fs::remove_file(idx);
}

#[tauri::command]
pub fn cache_probe(app: AppHandle, slot: String, length: f64) -> bool {
    let _guard = LOCK.lock().unwrap();

    let (dat, idx) = match store_paths(&app, &slot) {
        Ok(paths) => paths,
        Err(_) => return false,
    };

    match Index::read(&idx) {
        Some(index) if index.length == length as u64 => true,
        Some(_) => {
            // A store whose length no longer matches is stale; drop it so the
            // caller re-seeds or re-fetches.
            remove_store(&dat, &idx);
            false
        }
        None => false,
    }
}

#[tauri::command]
pub fn cache_read(
    app: AppHandle,
    slot: String,
    offset: f64,
    length: u32,
    blocksize: u32,
) -> Response {
    let _guard = LOCK.lock().unwrap();

    let miss = Response::new(Vec::<u8>::new());

    if length == 0 || blocksize == 0 {
        return miss;
    }

    let (dat, idx) = match store_paths(&app, &slot) {
        Ok(paths) => paths,
        Err(_) => return miss,
    };

    let index = match Index::read(&idx) {
        Some(index) if index.blocksize == blocksize => index,
        _ => return miss,
    };

    let offset = offset as u64;
    let bs = blocksize as u64;
    let first = offset / bs;
    let last = (offset + length as u64 - 1) / bs;

    for block in first..=last {
        if !index.holds(block) {
            return miss;
        }
    }

    let mut file = match fs::File::open(&dat) {
        Ok(file) => file,
        Err(_) => return miss,
    };

    let mut buffer = vec![0u8; length as usize];
    if file.seek(SeekFrom::Start(offset)).is_err() || file.read_exact(&mut buffer).is_err() {
        return miss;
    }

    Response::new(buffer)
}

#[tauri::command]
pub fn cache_write(
    app: AppHandle,
    slot: String,
    offset: f64,
    blocksize: u32,
    data: Vec<u8>,
) -> Result<(), String> {
    let _guard = LOCK.lock().unwrap();

    if blocksize == 0 {
        return Err("blocksize must be non-zero".into());
    }
    if data.is_empty() {
        return Ok(());
    }

    let (dat, idx) = store_paths(&app, &slot)?;

    // A store cut with a different block size cannot share this bitmap; start it
    // over. The engine's block size is a fixed constant, so this is defensive.
    let mut index = match Index::read(&idx) {
        Some(index) if index.blocksize == blocksize => index,
        Some(_) => {
            remove_store(&dat, &idx);
            Index::new(blocksize)
        }
        None => Index::new(blocksize),
    };

    let offset = offset as u64;
    let bs = blocksize as u64;
    let end = offset + data.len() as u64;

    if end > index.length {
        index.length = end;
    }
    index.reserve((index.length + bs - 1) / bs);

    let existed = dat.exists();
    let mut file = fs::OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .open(&dat)
        .map_err(|e| e.to_string())?;

    if !existed {
        mark_sparse(&file);
    }

    file.seek(SeekFrom::Start(offset)).map_err(|e| e.to_string())?;
    file.write_all(&data).map_err(|e| e.to_string())?;

    // Keep the image its full known length so the final short block reads back.
    file.set_len(index.length).map_err(|e| e.to_string())?;

    let first = offset / bs;
    let last = (end - 1) / bs;
    for block in first..=last {
        index.mark(block);
    }

    index.write(&idx)
}

#[tauri::command]
pub fn cache_room() -> f64 {
    ROOM_CONSTANT
}

#[tauri::command]
pub fn cache_clear(app: AppHandle, slot: String) -> Result<(), String> {
    let _guard = LOCK.lock().unwrap();

    let (dat, idx) = store_paths(&app, &slot)?;
    remove_store(&dat, &idx);
    Ok(())
}

/// Marks a file sparse so unwritten block ranges take no disk. Windows only;
/// elsewhere the file is dense, which the cache tolerates.
#[cfg(windows)]
fn mark_sparse(file: &fs::File) {
    use std::os::windows::io::AsRawHandle;

    // FSCTL_SET_SPARSE. A null input buffer turns the flag on.
    const FSCTL_SET_SPARSE: u32 = 0x0009_00C4;

    extern "system" {
        fn DeviceIoControl(
            device: *mut core::ffi::c_void,
            control: u32,
            in_buffer: *mut core::ffi::c_void,
            in_size: u32,
            out_buffer: *mut core::ffi::c_void,
            out_size: u32,
            returned: *mut u32,
            overlapped: *mut core::ffi::c_void,
        ) -> i32;
    }

    let mut returned: u32 = 0;
    unsafe {
        DeviceIoControl(
            file.as_raw_handle() as *mut core::ffi::c_void,
            FSCTL_SET_SPARSE,
            core::ptr::null_mut(),
            0,
            core::ptr::null_mut(),
            0,
            &mut returned,
            core::ptr::null_mut(),
        );
    }
}

#[cfg(not(windows))]
fn mark_sparse(_file: &fs::File) {}

/// Lowercase SHA-256 hex of the bytes. Matches `hashlib.sha256(...).hexdigest()`
/// so the seeder and this shell name the same slot the same file.
fn sha256_hex(input: &[u8]) -> String {
    let hash = sha256(input);
    let mut out = String::with_capacity(64);
    for byte in hash {
        out.push_str(&format!("{byte:02x}"));
    }
    out
}

fn sha256(input: &[u8]) -> [u8; 32] {
    const K: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2,
    ];

    let mut h: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
        0x5be0cd19,
    ];

    let mut message = input.to_vec();
    let bit_len = (input.len() as u64).wrapping_mul(8);
    message.push(0x80);
    while message.len() % 64 != 56 {
        message.push(0);
    }
    message.extend_from_slice(&bit_len.to_be_bytes());

    let mut w = [0u32; 64];
    for chunk in message.chunks_exact(64) {
        for (i, word) in w.iter_mut().take(16).enumerate() {
            *word = u32::from_be_bytes(chunk[i * 4..i * 4 + 4].try_into().unwrap());
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16].wrapping_add(s0).wrapping_add(w[i - 7]).wrapping_add(s1);
        }

        let mut v = h;
        for i in 0..64 {
            let s1 = v[4].rotate_right(6) ^ v[4].rotate_right(11) ^ v[4].rotate_right(25);
            let ch = (v[4] & v[5]) ^ ((!v[4]) & v[6]);
            let t1 = v[7]
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(K[i])
                .wrapping_add(w[i]);
            let s0 = v[0].rotate_right(2) ^ v[0].rotate_right(13) ^ v[0].rotate_right(22);
            let maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            let t2 = s0.wrapping_add(maj);

            v[7] = v[6];
            v[6] = v[5];
            v[5] = v[4];
            v[4] = v[3].wrapping_add(t1);
            v[3] = v[2];
            v[2] = v[1];
            v[1] = v[0];
            v[0] = t1.wrapping_add(t2);
        }

        for (dst, src) in h.iter_mut().zip(v.iter()) {
            *dst = dst.wrapping_add(*src);
        }
    }

    let mut out = [0u8; 32];
    for (i, word) in h.iter().enumerate() {
        out[i * 4..i * 4 + 4].copy_from_slice(&word.to_be_bytes());
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sha256_matches_known_vectors() {
        assert_eq!(
            sha256_hex(b""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
        assert_eq!(
            sha256_hex(b"abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
    }

    #[test]
    fn bitmap_round_trips() {
        let mut index = Index::new(64);
        index.length = 200;
        index.reserve((index.length + 63) / 64);
        index.mark(0);
        index.mark(3);
        assert!(index.holds(0));
        assert!(!index.holds(1));
        assert!(index.holds(3));
        assert!(!index.holds(4));
    }
}
