//! Reads one file of a .pak archive (docs/formats/pak.md), as the engine does
//! (packages/engine/assets/src/pak.cpp): an install keeps the base pack as content/base.pak, and
//! the launcher's background is one of its images.

use sha1::{Digest, Sha1};
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

const MAGIC: [u8; 8] = [0x89, b'P', b'A', b'K', 0x0D, 0x0A, 0x1A, 0x0A];
const HEADER_BYTES: usize = 64;
const ENTRY_BYTES: usize = 64;
/// Versions 1 (nothing compressed) and 2 (zstd per entry).
const VERSIONS: [u32; 2] = [1, 2];
const STORED: u32 = 0;
const ZSTD: u32 = 1;
const COMPRESSION_MASK: u32 = 0xFF;
/// More than any pack has, so a damaged header cannot ask for gigabytes of table.
const MAX_ENTRIES: u64 = 1 << 20;
const MAX_NAMES: u64 = 64 << 20;
/// The largest file the launcher reads from an archive: an image for its background.
pub const MAX_FILE: u64 = 64 << 20;

fn u32_at(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap())
}

fn u64_at(bytes: &[u8], at: usize) -> u64 {
    u64::from_le_bytes(bytes[at..at + 8].try_into().unwrap())
}

fn read_at(file: &mut File, offset: u64, size: u64) -> Option<Vec<u8>> {
    let mut bytes = vec![0; usize::try_from(size).ok()?];
    file.seek(SeekFrom::Start(offset)).ok()?;
    file.read_exact(&mut bytes).ok()?;
    Some(bytes)
}

/// The bytes of the file `name` (its path in the pack, such as map/a/background_day.png) in the
/// archive: decompressed and checked against its SHA-1. Nothing when the archive cannot be read,
/// has no such file, or the file is larger than MAX_FILE.
pub fn read_file(archive: &Path, name: &str) -> Option<Vec<u8>> {
    let mut file = File::open(archive).ok()?;
    let header = read_at(&mut file, 0, HEADER_BYTES as u64)?;
    if header[..8] != MAGIC || !VERSIONS.contains(&u32_at(&header, 0x08)) {
        return None;
    }
    let count = u64::from(u32_at(&header, 0x10));
    let (table, names_at, names_size) = (u64_at(&header, 0x18), u64_at(&header, 0x20), u64_at(&header, 0x28));
    if count > MAX_ENTRIES || names_size > MAX_NAMES {
        return None;
    }
    let entries = read_at(&mut file, table, count * ENTRY_BYTES as u64)?;
    let names = read_at(&mut file, names_at, names_size)?;
    let entry = entries.as_chunks::<ENTRY_BYTES>().0.iter().find(|entry| {
        let (at, length) = (u32_at(&entry[..], 0x18) as usize, u32_at(&entry[..], 0x1C) as usize);
        names.get(at..at.saturating_add(length)) == Some(name.as_bytes())
    })?;
    let (offset, stored, size) = (u64_at(entry, 0x00), u64_at(entry, 0x08), u64_at(entry, 0x10));
    if size > MAX_FILE || stored > MAX_FILE {
        return None;
    }
    let bytes = read_at(&mut file, offset, stored)?;
    let bytes = match u32_at(entry, 0x20) & COMPRESSION_MASK {
        STORED if stored == size => bytes,
        ZSTD => zstd::bulk::decompress(&bytes, usize::try_from(size).ok()?).ok()?,
        _ => return None,
    };
    (bytes.len() as u64 == size && Sha1::digest(&bytes).as_slice() == &entry[0x24..0x38]).then_some(bytes)
}

#[cfg(test)]
pub mod tests {
    use super::*;

    /// A version 2 archive of the files given, the first stored and the rest compressed, with the
    /// layout dwx pack writes (the manifest left empty).
    pub fn archive(files: &[(&str, &[u8])]) -> Vec<u8> {
        let mut data: Vec<u8> = Vec::new();
        let mut table: Vec<u8> = Vec::new();
        let mut names: Vec<u8> = Vec::new();
        for (index, (name, bytes)) in files.iter().enumerate() {
            let offset = HEADER_BYTES + data.len();
            let (stored, compression) =
                if index == 0 { (bytes.to_vec(), STORED) } else { (zstd::bulk::compress(bytes, 3).unwrap(), ZSTD) };
            let mut entry = Vec::new();
            entry.extend((offset as u64).to_le_bytes());
            entry.extend((stored.len() as u64).to_le_bytes());
            entry.extend((bytes.len() as u64).to_le_bytes());
            entry.extend((names.len() as u32).to_le_bytes());
            entry.extend((name.len() as u32).to_le_bytes());
            entry.extend(compression.to_le_bytes());
            entry.extend(Sha1::digest(bytes));
            entry.extend(0u64.to_le_bytes());
            table.extend(entry);
            names.extend(name.as_bytes());
            data.extend(stored);
            while !data.len().is_multiple_of(16) {
                data.push(0);
            }
        }
        let table_at = (HEADER_BYTES + data.len()) as u64;
        let names_at = table_at + table.len() as u64;
        let mut out = MAGIC.to_vec();
        out.extend(2u32.to_le_bytes());
        out.extend(0u32.to_le_bytes());
        out.extend((files.len() as u32).to_le_bytes());
        out.extend(16u32.to_le_bytes());
        out.extend(table_at.to_le_bytes());
        out.extend(names_at.to_le_bytes());
        out.extend((names.len() as u64).to_le_bytes());
        out.extend((names_at + names.len() as u64).to_le_bytes());
        out.extend(0u64.to_le_bytes());
        out.extend(data);
        out.extend(table);
        out.extend(names);
        out
    }

    #[test]
    fn files_are_read_stored_or_compressed_and_checked() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("base.pak");
        let picture = vec![7u8; 5000];
        std::fs::write(&path, archive(&[("a/stored.png", b"plain"), ("b/packed.png", &picture)])).unwrap();
        assert_eq!(read_file(&path, "a/stored.png").as_deref(), Some(&b"plain"[..]));
        assert_eq!(read_file(&path, "b/packed.png"), Some(picture));
        assert_eq!(read_file(&path, "c/missing.png"), None);
        assert_eq!(read_file(&dir.path().join("none.pak"), "a/stored.png"), None);
        // A damaged byte fails the SHA-1.
        let mut damaged = std::fs::read(&path).unwrap();
        damaged[HEADER_BYTES] ^= 1;
        std::fs::write(&path, damaged).unwrap();
        assert_eq!(read_file(&path, "a/stored.png"), None);
    }
}
