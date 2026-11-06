use std::path::{Path, PathBuf};

use crate::{Error, Result};

/// The packs of a folder, as the engine's `findPacks` finds them.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct FoundPacks {
    /// Pack folders, sorted by path.
    pub packs: Vec<PathBuf>,
    /// `.pak` archives no folder hides, sorted by path. The tools read folders only: these are
    /// reported, not mounted.
    pub archives: Vec<PathBuf>,
    /// `.pak` archives hidden by the folder with their name, which is the newer copy.
    pub hidden: Vec<PathBuf>,
}

/// The folder itself when it has a `pack.yaml`; otherwise each immediate subfolder that has one
/// and each immediate `.pak` file (docs/architecture/vfs.md, "Qué se puede preguntar").
pub fn find_packs(folder: &Path) -> Result<FoundPacks> {
    if folder.join("pack.yaml").is_file() {
        return Ok(FoundPacks {
            packs: vec![folder.to_path_buf()],
            ..FoundPacks::default()
        });
    }
    let entries = std::fs::read_dir(folder).map_err(|error| Error::io(folder, error))?;
    let mut found = FoundPacks::default();
    for entry in entries {
        let path = entry.map_err(|error| Error::io(folder, error))?.path();
        if path.is_dir() && path.join("pack.yaml").is_file() {
            found.packs.push(path);
        } else if path.is_file() && path.extension().is_some_and(|ext| ext == "pak") {
            found.archives.push(path);
        }
    }
    let (hidden, archives): (Vec<_>, Vec<_>) = found.archives.into_iter().partition(|archive| {
        found
            .packs
            .iter()
            .any(|pack| pack.file_name() == archive.file_stem())
    });
    found.archives = archives;
    found.hidden = hidden;
    found.packs.sort();
    found.archives.sort();
    found.hidden.sort();
    Ok(found)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_folder_beats_the_archive_with_its_name() {
        let dir = tempfile::tempdir().unwrap();
        for pack in ["b", "a"] {
            std::fs::create_dir(dir.path().join(pack)).unwrap();
            std::fs::write(dir.path().join(pack).join("pack.yaml"), "").unwrap();
        }
        std::fs::create_dir(dir.path().join("not_a_pack")).unwrap();
        std::fs::write(dir.path().join("a.pak"), "").unwrap();
        std::fs::write(dir.path().join("c.pak"), "").unwrap();
        let found = find_packs(dir.path()).unwrap();
        assert_eq!(
            found.packs,
            vec![dir.path().join("a"), dir.path().join("b")]
        );
        assert_eq!(found.archives, vec![dir.path().join("c.pak")]);
        assert_eq!(found.hidden, vec![dir.path().join("a.pak")]);
        assert_eq!(
            find_packs(&dir.path().join("a")).unwrap().packs,
            vec![dir.path().join("a")]
        );
    }
}
