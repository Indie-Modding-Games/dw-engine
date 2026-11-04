//! The file the player picks: a disc image dwx reads (.cue with its .bin, .bin, .iso), an archive
//! holding one (.zip, .7z, unpacked to a temporary folder first), or a format dwx does not read,
//! which is named instead of pretended.

use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

use crate::failure::{code, Failure, Result};
use crate::store::is_safe_relative;

/// The images dwx reads (apps/dwx: dwx identify and extract).
pub const IMAGE_EXTENSIONS: [&str; 3] = ["cue", "bin", "iso"];
/// Archives the launcher unpacks.
pub const ARCHIVE_EXTENSIONS: [&str; 2] = ["zip", "7z"];
/// Formats players often have that dwx does not read: the page names the format and says to
/// convert it to .cue/.bin first.
pub const UNSUPPORTED_EXTENSIONS: [&str; 12] =
    ["ecm", "pbp", "rar", "chd", "mdf", "mds", "ccd", "img", "nrg", "cdi", "cso", "gz"];
/// No disc of the game is this big: an archive that would unpack to more is refused.
const MAX_UNPACKED_BYTES: u64 = 2 * 1024 * 1024 * 1024;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Pick {
    Image(PathBuf),
    Zip(PathBuf),
    SevenZip(PathBuf),
}

fn extension(path: &Path) -> String {
    path.extension().map(|e| e.to_string_lossy().to_ascii_lowercase()).unwrap_or_default()
}

pub fn classify(path: &Path) -> Result<Pick> {
    if !path.is_file() {
        return Err(Failure::new(code::MISSING_FILE, path.display().to_string()));
    }
    let ext = extension(path);
    match ext.as_str() {
        e if IMAGE_EXTENSIONS.contains(&e) => Ok(Pick::Image(path.to_path_buf())),
        "zip" => Ok(Pick::Zip(path.to_path_buf())),
        "7z" => Ok(Pick::SevenZip(path.to_path_buf())),
        _ => Err(Failure::new(code::UNSUPPORTED_FORMAT, if ext.is_empty() { "?".into() } else { ext })),
    }
}

/// The files a CUE sheet names (FILE "<name>" ...) that are not next to it.
pub fn missing_cue_files(image: &Path) -> Vec<String> {
    if extension(image) != "cue" {
        return Vec::new();
    }
    let Ok(sheet) = fs::read_to_string(image) else {
        return Vec::new();
    };
    let mut missing = Vec::new();
    for line in sheet.lines() {
        let text = line.trim();
        let Some(rest) = text.strip_prefix("FILE ") else { continue };
        let name = if let Some(open) = rest.find('"') {
            let after = &rest[open + 1..];
            after[..after.find('"').unwrap_or(after.len())].to_string()
        } else {
            rest.split(' ').next().unwrap_or_default().to_string()
        };
        if !name.is_empty() && !image.parent().unwrap_or(Path::new(".")).join(&name).exists() {
            missing.push(name);
        }
    }
    missing
}

/// The disc image in an unpacked archive: a .cue first, then an .iso, then a .bin.
pub fn find_image(folder: &Path) -> Option<PathBuf> {
    let mut files = Vec::new();
    let mut pending = vec![(folder.to_path_buf(), 0)];
    while let Some((dir, depth)) = pending.pop() {
        for entry in fs::read_dir(&dir).ok()?.flatten() {
            let path = entry.path();
            if path.is_dir() && depth < 3 {
                pending.push((path, depth + 1));
            } else if path.is_file() {
                files.push(path);
            }
        }
    }
    files.sort();
    for wanted in ["cue", "iso", "bin"] {
        if let Some(found) = files.iter().find(|f| extension(f) == wanted) {
            return Some(found.clone());
        }
    }
    None
}

fn copy_limited(reader: &mut (impl Read + ?Sized), destination: &Path, budget: &mut u64) -> Result<()> {
    if let Some(parent) = destination.parent() {
        fs::create_dir_all(parent)?;
    }
    let mut out = File::create(destination)?;
    let mut buffer = vec![0u8; 1 << 16];
    loop {
        let read = reader.read(&mut buffer)?;
        if read == 0 {
            return Ok(());
        }
        *budget = budget
            .checked_sub(read as u64)
            .ok_or_else(|| Failure::new(code::NO_IMAGE_IN_ARCHIVE, "too big for a disc"))?;
        out.write_all(&buffer[..read])?;
    }
}

/// Unpacks an archive into target (which must not exist yet or be empty) and returns the disc
/// image in it.
pub fn unpack(pick: &Pick, target: &Path) -> Result<PathBuf> {
    fs::create_dir_all(target)?;
    let mut budget = MAX_UNPACKED_BYTES;
    match pick {
        Pick::Image(path) => return Ok(path.clone()),
        Pick::Zip(path) => {
            let mut archive = zip::ZipArchive::new(File::open(path)?)
                .map_err(|e| Failure::new(code::NO_IMAGE_IN_ARCHIVE, e.to_string()))?;
            for i in 0..archive.len() {
                let mut entry = archive.by_index(i).map_err(|e| Failure::new(code::NO_IMAGE_IN_ARCHIVE, e.to_string()))?;
                let Some(relative) = entry.enclosed_name() else {
                    return Err(Failure::new(code::NO_IMAGE_IN_ARCHIVE, format!("unsafe path {}", entry.name())));
                };
                if entry.is_dir() {
                    continue;
                }
                copy_limited(&mut entry, &target.join(relative), &mut budget)?;
            }
        }
        Pick::SevenZip(path) => {
            let mut reader = sevenz_rust2::ArchiveReader::open(path, sevenz_rust2::Password::empty())
                .map_err(|e| Failure::new(code::NO_IMAGE_IN_ARCHIVE, e.to_string()))?;
            let mut failure: Option<Failure> = None;
            reader
                .for_each_entries(|entry, data| {
                    if entry.is_directory() {
                        return Ok(true);
                    }
                    let relative = PathBuf::from(entry.name().replace('\\', "/"));
                    if !is_safe_relative(&relative) {
                        failure = Some(Failure::new(code::NO_IMAGE_IN_ARCHIVE, format!("unsafe path {}", entry.name())));
                        return Ok(false);
                    }
                    if let Err(error) = copy_limited(data, &target.join(relative), &mut budget) {
                        failure = Some(error);
                        return Ok(false);
                    }
                    Ok(true)
                })
                .map_err(|e| Failure::new(code::NO_IMAGE_IN_ARCHIVE, e.to_string()))?;
            if let Some(failure) = failure {
                return Err(failure);
            }
        }
    }
    find_image(target).ok_or_else(|| Failure::new(code::NO_IMAGE_IN_ARCHIVE, ""))
}

impl From<zip::result::ZipError> for Failure {
    fn from(error: zip::result::ZipError) -> Self {
        Failure::new(code::NO_IMAGE_IN_ARCHIVE, error.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn formats_are_classified() {
        let dir = tempfile::tempdir().unwrap();
        for name in ["a.CUE", "b.bin", "c.iso", "d.zip", "e.7z", "f.ecm", "g.pbp", "h.rar", "i"] {
            fs::write(dir.path().join(name), b"x").unwrap();
        }
        let at = |n: &str| classify(&dir.path().join(n));
        assert!(matches!(at("a.CUE"), Ok(Pick::Image(_))));
        assert!(matches!(at("c.iso"), Ok(Pick::Image(_))));
        assert!(matches!(at("d.zip"), Ok(Pick::Zip(_))));
        assert!(matches!(at("e.7z"), Ok(Pick::SevenZip(_))));
        for (name, ext) in [("f.ecm", "ecm"), ("g.pbp", "pbp"), ("h.rar", "rar"), ("i", "?")] {
            assert_eq!(at(name).unwrap_err(), Failure::new(code::UNSUPPORTED_FORMAT, ext));
        }
        assert_eq!(at("missing.cue").unwrap_err().code, code::MISSING_FILE);
    }

    #[test]
    fn cue_sheets_name_their_missing_tracks() {
        let dir = tempfile::tempdir().unwrap();
        let cue = dir.path().join("game.cue");
        fs::write(&cue, "FILE \"game (track 1).bin\" BINARY\n  TRACK 01 MODE2/2352\nFILE other.bin BINARY\n").unwrap();
        assert_eq!(missing_cue_files(&cue), vec!["game (track 1).bin", "other.bin"]);
        fs::write(dir.path().join("game (track 1).bin"), b"").unwrap();
        fs::write(dir.path().join("other.bin"), b"").unwrap();
        assert!(missing_cue_files(&cue).is_empty());
    }

    #[test]
    fn a_zip_is_unpacked_to_its_cue() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("disc.zip");
        {
            let mut writer = zip::ZipWriter::new(File::create(&path).unwrap());
            let options = zip::write::SimpleFileOptions::default();
            writer.start_file("Game/game.bin", options).unwrap();
            writer.write_all(b"sectors").unwrap();
            writer.start_file("Game/game.cue", options).unwrap();
            writer.write_all(b"FILE \"game.bin\" BINARY\n").unwrap();
            writer.start_file("readme.txt", options).unwrap();
            writer.finish().unwrap();
        }
        let target = dir.path().join("out");
        let image = unpack(&classify(&path).unwrap(), &target).unwrap();
        assert_eq!(image, target.join("Game/game.cue"));
        assert!(missing_cue_files(&image).is_empty());
    }

    #[test]
    fn a_zip_without_an_image_or_with_unsafe_paths_is_refused() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("notes.zip");
        {
            let mut writer = zip::ZipWriter::new(File::create(&path).unwrap());
            writer.start_file("notes.txt", zip::write::SimpleFileOptions::default()).unwrap();
            writer.finish().unwrap();
        }
        assert_eq!(unpack(&Pick::Zip(path), &dir.path().join("a")).unwrap_err().code, code::NO_IMAGE_IN_ARCHIVE);

        let evil = dir.path().join("evil.zip");
        {
            let mut writer = zip::ZipWriter::new(File::create(&evil).unwrap());
            writer.start_file("../escape.cue", zip::write::SimpleFileOptions::default()).unwrap();
            writer.finish().unwrap();
        }
        assert!(unpack(&Pick::Zip(evil), &dir.path().join("b")).is_err());
        assert!(!dir.path().join("escape.cue").exists());
    }

    #[test]
    fn a_7z_is_unpacked() {
        let dir = tempfile::tempdir().unwrap();
        let source = dir.path().join("src");
        fs::create_dir_all(&source).unwrap();
        fs::write(source.join("game.iso"), b"iso").unwrap();
        let path = dir.path().join("disc.7z");
        sevenz_rust2::compress_to_path(&source, &path).unwrap();
        let image = unpack(&classify(&path).unwrap(), &dir.path().join("out")).unwrap();
        assert_eq!(image.file_name().unwrap(), "game.iso");
        assert_eq!(fs::read(image).unwrap(), b"iso");
    }
}
