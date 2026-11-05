//! Turning what the player picked (a .zip or a folder) into a checked add-on folder in staging,
//! before anything is installed: size limits, no path that leaves the folder, no links, and a
//! valid addon.json (the folder itself or its only subfolder may hold it).

use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

use super::manifest::{Manifest, MANIFEST_FILE};
use crate::failure::{code, Failure, Result};

/// The biggest .zip the launcher opens.
pub const MAX_ZIP_BYTES: u64 = 20 * 1024 * 1024;
/// The most an add-on may hold once unpacked.
pub const MAX_UNPACKED_BYTES: u64 = 40 * 1024 * 1024;
pub const MAX_FILES: usize = 1000;
const MAX_DEPTH: usize = 12;

fn too_big(what: &str) -> Failure {
    Failure::new(code::ADDON_TOO_BIG, what.to_string())
}

fn unsafe_path(name: &str) -> Failure {
    Failure::new(code::ADDON_UNSAFE, name.to_string())
}

fn copy_limited(reader: &mut impl Read, destination: &Path, budget: &mut u64) -> Result<()> {
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
        *budget = budget.checked_sub(read as u64).ok_or_else(|| too_big("unpacked"))?;
        out.write_all(&buffer[..read])?;
    }
}

fn unzip(archive: &Path, target: &Path) -> Result<()> {
    if fs::metadata(archive)?.len() > MAX_ZIP_BYTES {
        return Err(too_big("zip"));
    }
    let bad = |_: zip::result::ZipError| Failure::new(code::ADDON_INVALID, format!("{MANIFEST_FILE}:not-zip"));
    let mut zip = zip::ZipArchive::new(File::open(archive)?).map_err(bad)?;
    if zip.len() > MAX_FILES {
        return Err(too_big("files"));
    }
    let mut budget = MAX_UNPACKED_BYTES;
    for i in 0..zip.len() {
        let mut entry = zip.by_index(i).map_err(bad)?;
        let name = entry.name().to_string();
        let Some(relative) = entry.enclosed_name() else {
            return Err(unsafe_path(&name));
        };
        if name.contains('\\') || relative.components().count() > MAX_DEPTH {
            return Err(unsafe_path(&name));
        }
        if entry.is_symlink() {
            return Err(unsafe_path(&name));
        }
        if entry.is_dir() {
            fs::create_dir_all(target.join(relative))?;
            continue;
        }
        // The size the archive claims is checked before reading, the real one while reading.
        if entry.size() > budget {
            return Err(too_big("unpacked"));
        }
        copy_limited(&mut entry, &target.join(relative), &mut budget)?;
    }
    Ok(())
}

fn copy_folder(from: &Path, to: &Path, budget: &mut u64, files: &mut usize, depth: usize) -> Result<()> {
    if depth > MAX_DEPTH {
        return Err(unsafe_path(&from.display().to_string()));
    }
    fs::create_dir_all(to)?;
    for entry in fs::read_dir(from)? {
        let entry = entry?;
        let kind = entry.file_type()?;
        let name = entry.file_name();
        if kind.is_symlink() {
            return Err(unsafe_path(&name.to_string_lossy()));
        }
        *files += 1;
        if *files > MAX_FILES {
            return Err(too_big("files"));
        }
        if kind.is_dir() {
            copy_folder(&entry.path(), &to.join(&name), budget, files, depth + 1)?;
        } else if kind.is_file() {
            copy_limited(&mut File::open(entry.path())?, &to.join(&name), budget)?;
        }
    }
    Ok(())
}

/// The folder in `unpacked` that holds addon.json: itself, or its only subfolder (a zip of a
/// folder).
fn add_on_root(unpacked: &Path) -> Result<PathBuf> {
    if unpacked.join(MANIFEST_FILE).is_file() {
        return Ok(unpacked.to_path_buf());
    }
    let entries: Vec<_> = fs::read_dir(unpacked)?.flatten().collect();
    if let [only] = entries.as_slice() {
        if only.file_type()?.is_dir() && only.path().join(MANIFEST_FILE).is_file() {
            return Ok(only.path());
        }
    }
    Err(Failure::new(code::ADDON_INVALID, format!("{MANIFEST_FILE}:missing")))
}

/// Copies or unpacks `source` into `staging` (which must not exist) and checks it. Returns the
/// add-on's folder inside `staging` and its manifest. On failure `staging` is removed.
pub fn stage(source: &Path, staging: &Path) -> Result<(PathBuf, Manifest)> {
    let result = (|| {
        let meta = fs::symlink_metadata(source).map_err(|_| Failure::new(code::MISSING_FILE, source.display().to_string()))?;
        if meta.is_dir() {
            copy_folder(source, staging, &mut MAX_UNPACKED_BYTES.clone(), &mut 0, 0)?;
        } else if source.extension().is_some_and(|e| e.eq_ignore_ascii_case("zip")) {
            fs::create_dir_all(staging)?;
            unzip(source, staging)?;
        } else if source.file_name().is_some_and(|n| n == MANIFEST_FILE) {
            // The player picked the addon.json itself: its folder is the add-on.
            let folder = source.parent().unwrap_or(Path::new("."));
            copy_folder(folder, staging, &mut MAX_UNPACKED_BYTES.clone(), &mut 0, 0)?;
        } else {
            return Err(Failure::new(code::ADDON_INVALID, format!("{MANIFEST_FILE}:not-an-add-on")));
        }
        let root = add_on_root(staging)?;
        let manifest = Manifest::read(&root)?;
        Ok((root, manifest))
    })();
    if result.is_err() {
        let _ = fs::remove_dir_all(staging);
    }
    result
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use zip::write::SimpleFileOptions;

    pub const MANIFEST: &str = r#"{"id":"org.example.notes","name":"Notas","version":"1.0.0","author":"Equipo",
        "api":1,"entry":"index.html","slots":["tabs"],"permissions":{"storage":true}}"#;

    pub fn zip_of(path: &Path, files: &[(&str, &[u8])]) {
        let mut zip = zip::ZipWriter::new(File::create(path).unwrap());
        for (name, data) in files {
            zip.start_file(*name, SimpleFileOptions::default()).unwrap();
            zip.write_all(data).unwrap();
        }
        zip.finish().unwrap();
    }

    #[test]
    fn a_zip_and_a_zip_of_a_folder_are_staged() {
        let dir = tempfile::tempdir().unwrap();
        let flat = dir.path().join("flat.zip");
        zip_of(&flat, &[("addon.json", MANIFEST.as_bytes()), ("index.html", b"<p>hi</p>"), ("img/icon.svg", b"<svg/>")]);
        let (root, manifest) = stage(&flat, &dir.path().join("s1")).unwrap();
        assert_eq!(manifest.id, "org.example.notes");
        assert!(root.join("img/icon.svg").is_file());

        let nested = dir.path().join("nested.zip");
        zip_of(&nested, &[("notes/addon.json", MANIFEST.as_bytes()), ("notes/index.html", b"x")]);
        let (root, _) = stage(&nested, &dir.path().join("s2")).unwrap();
        assert!(root.ends_with("notes"));
    }

    #[test]
    fn a_folder_or_its_manifest_is_staged() {
        let dir = tempfile::tempdir().unwrap();
        let folder = dir.path().join("src");
        fs::create_dir_all(&folder).unwrap();
        fs::write(folder.join("addon.json"), MANIFEST).unwrap();
        fs::write(folder.join("index.html"), "x").unwrap();
        assert!(stage(&folder, &dir.path().join("s1")).is_ok());
        assert!(stage(&folder.join("addon.json"), &dir.path().join("s2")).is_ok());
    }

    #[test]
    fn unsafe_and_oversized_packages_are_refused() {
        let dir = tempfile::tempdir().unwrap();
        let code_of = |source: &Path, n: &str| {
            let staging = dir.path().join(n);
            let failure = stage(source, &staging).unwrap_err();
            assert!(!staging.exists(), "the staging folder is removed after a failure");
            failure.code
        };

        let escape = dir.path().join("escape.zip");
        zip_of(&escape, &[("addon.json", MANIFEST.as_bytes()), ("index.html", b"x"), ("../../evil.sh", b"rm")]);
        assert_eq!(code_of(&escape, "a"), code::ADDON_UNSAFE);
        assert!(!dir.path().join("evil.sh").exists());

        let absolute = dir.path().join("absolute.zip");
        zip_of(&absolute, &[("/tmp/evil", b"x"), ("addon.json", MANIFEST.as_bytes())]);
        assert_eq!(code_of(&absolute, "b"), code::ADDON_UNSAFE);

        let backslash = dir.path().join("backslash.zip");
        zip_of(&backslash, &[("..\\evil", b"x")]);
        assert_eq!(code_of(&backslash, "c"), code::ADDON_UNSAFE);

        let big = dir.path().join("big.zip");
        let file = File::create(&big).unwrap();
        file.set_len(MAX_ZIP_BYTES + 1).unwrap();
        assert_eq!(code_of(&big, "d"), code::ADDON_TOO_BIG);

        // A small zip that unpacks to more than the limit (a "zip bomb").
        let bomb = dir.path().join("bomb.zip");
        let zeros = vec![0u8; (MAX_UNPACKED_BYTES + 1) as usize];
        zip_of(&bomb, &[("addon.json", MANIFEST.as_bytes()), ("zeros.bin", &zeros)]);
        assert!(fs::metadata(&bomb).unwrap().len() < MAX_ZIP_BYTES);
        assert_eq!(code_of(&bomb, "e"), code::ADDON_TOO_BIG);

        let many = dir.path().join("many.zip");
        let names: Vec<String> = (0..=MAX_FILES).map(|i| format!("f{i}.txt")).collect();
        let files: Vec<(&str, &[u8])> = names.iter().map(|n| (n.as_str(), &b"x"[..])).collect();
        zip_of(&many, &files);
        assert_eq!(code_of(&many, "f"), code::ADDON_TOO_BIG);

        let no_manifest = dir.path().join("none.zip");
        zip_of(&no_manifest, &[("index.html", b"x")]);
        assert_eq!(code_of(&no_manifest, "g"), code::ADDON_INVALID);

        let no_entry = dir.path().join("noentry.zip");
        zip_of(&no_entry, &[("addon.json", MANIFEST.as_bytes())]);
        let failure = stage(&no_entry, &dir.path().join("h")).unwrap_err();
        assert_eq!(failure.detail, "entry:entry-missing");

        let not_zip = dir.path().join("broken.zip");
        fs::write(&not_zip, "not a zip").unwrap();
        assert_eq!(code_of(&not_zip, "i"), code::ADDON_INVALID);

        let other = dir.path().join("game.cue");
        fs::write(&other, "FILE").unwrap();
        assert_eq!(code_of(&other, "j"), code::ADDON_INVALID);
    }

    #[cfg(unix)]
    #[test]
    fn links_in_a_folder_are_refused() {
        let dir = tempfile::tempdir().unwrap();
        let folder = dir.path().join("src");
        fs::create_dir_all(&folder).unwrap();
        fs::write(folder.join("addon.json"), MANIFEST).unwrap();
        fs::write(folder.join("index.html"), "x").unwrap();
        std::os::unix::fs::symlink("/etc/passwd", folder.join("passwd")).unwrap();
        assert_eq!(stage(&folder, &dir.path().join("s")).unwrap_err().code, code::ADDON_UNSAFE);
    }
}
