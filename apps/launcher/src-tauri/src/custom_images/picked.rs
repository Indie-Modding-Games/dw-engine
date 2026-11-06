//! What the player picked to load images from: a folder or a .zip, read as a list of files named by
//! their path inside it. Nothing is ever written by those names: each file is matched to an image
//! the game has (Catalogue::target_of) and copied there.

use std::fs::{self, File};
use std::io::Read;
use std::path::{Path, PathBuf};

use crate::failure::{code, Failure, Result};

/// The largest image read: an 8x background in full colour is far below it.
pub const MAX_FILE_BYTES: u64 = 256 * 1024 * 1024;
/// How deep a folder is walked and how many files are looked at.
const MAX_DEPTH: usize = 16;
const MAX_FILES: usize = 20_000;
const ZIP_EXTENSION: &str = "zip";

pub enum Picked {
    Folder(PathBuf),
    Zip(zip::ZipArchive<File>),
}

fn not_a_source(path: &Path) -> Failure {
    Failure::new(code::IMAGES_NOT_A_SOURCE, path.display().to_string())
}

fn too_big(name: &str) -> Failure {
    Failure::new(code::IMAGES_TOO_BIG, name)
}

impl Picked {
    pub fn open(path: &Path) -> Result<Self> {
        if path.is_dir() {
            return Ok(Self::Folder(path.to_path_buf()));
        }
        let is_zip = path.extension().is_some_and(|e| e.eq_ignore_ascii_case(ZIP_EXTENSION));
        if !is_zip || !path.is_file() {
            return Err(not_a_source(path));
        }
        let zip = zip::ZipArchive::new(File::open(path)?).map_err(|_| not_a_source(path))?;
        Ok(Self::Zip(zip))
    }

    /// Every file, by its path inside with '/' between folders.
    pub fn names(&mut self) -> Result<Vec<String>> {
        let mut names = Vec::new();
        match self {
            Self::Folder(root) => walk(root, "", 0, &mut names)?,
            Self::Zip(zip) => {
                for i in 0..zip.len().min(MAX_FILES) {
                    let entry = zip.by_index(i).map_err(|e| Failure::new(code::IO, e.to_string()))?;
                    if entry.is_file() && entry.enclosed_name().is_some() {
                        names.push(entry.name().replace('\\', "/"));
                    }
                }
            }
        }
        Ok(names)
    }

    /// A file's first bytes: enough for its PNG header.
    pub fn header(&mut self, name: &str) -> Result<Vec<u8>> {
        let (reader, _) = self.open_file(name)?;
        let mut bytes = Vec::new();
        reader.take(super::png::HEADER_BYTES as u64).read_to_end(&mut bytes)?;
        Ok(bytes)
    }

    /// A whole file, up to MAX_FILE_BYTES.
    pub fn read(&mut self, name: &str) -> Result<Vec<u8>> {
        let (reader, size) = self.open_file(name)?;
        if size > MAX_FILE_BYTES {
            return Err(too_big(name));
        }
        let mut bytes = Vec::new();
        reader.take(MAX_FILE_BYTES).read_to_end(&mut bytes)?;
        Ok(bytes)
    }

    fn open_file(&mut self, name: &str) -> Result<(Box<dyn Read + '_>, u64)> {
        match self {
            Self::Folder(root) => {
                let file = File::open(root.join(name))?;
                let size = file.metadata()?.len();
                Ok((Box::new(file), size))
            }
            Self::Zip(zip) => {
                let entry = zip.by_name(name).map_err(|e| Failure::new(code::IO, e.to_string()))?;
                let size = entry.size();
                Ok((Box::new(entry), size))
            }
        }
    }
}

fn walk(folder: &Path, prefix: &str, depth: usize, names: &mut Vec<String>) -> Result<()> {
    if depth > MAX_DEPTH {
        return Ok(());
    }
    let mut entries: Vec<_> = fs::read_dir(folder)?.collect::<std::io::Result<_>>()?;
    entries.sort_by_key(|e| e.file_name());
    for entry in entries {
        if names.len() >= MAX_FILES {
            break;
        }
        let kind = entry.file_type()?;
        let name = format!("{prefix}{}", entry.file_name().to_string_lossy());
        if kind.is_dir() {
            walk(&entry.path(), &format!("{name}/"), depth + 1, names)?;
        } else if kind.is_file() {
            names.push(name);
        }
    }
    Ok(())
}
