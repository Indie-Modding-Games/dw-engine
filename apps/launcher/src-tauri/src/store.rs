//! The installed game: each version unpacked in <data>/game/versions/<version>/ and a state file
//! (state.json) naming the current one and the one before it. An update unpacks next to the
//! current version and then replaces the state file in one rename, so the game is always either
//! the old or the new one; the previous version stays for a rollback and older ones are removed.

use serde::{Deserialize, Serialize};
use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Component, Path, PathBuf};

use crate::failure::{code, Failure, Result};

const GAME_FOLDER: &str = "game";
const VERSIONS_FOLDER: &str = "versions";
const STATE_FILE: &str = "state.json";
/// The file of a package with the ExtractorVersion of its dwx (scripts/package_linux.sh).
pub const EXTRACTOR_VERSION_FILE: &str = "EXTRACTOR_VERSION";
/// A development override: a folder with an unpacked package (bin/dwe-game, bin/dwx, packs/)
/// used instead of the installed versions.
pub const GAME_DIR_ENV: &str = "DWE_LAUNCHER_GAME_DIR";

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct InstalledVersion {
    pub version: String,
    #[serde(default)]
    pub commit: String,
    pub extractor_version: Option<i32>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
pub struct State {
    pub current: Option<InstalledVersion>,
    pub previous: Option<InstalledVersion>,
}

/// A game the launcher can run: its folder and what it is.
#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Game {
    pub dir: PathBuf,
    pub version: String,
    pub commit: String,
    pub extractor_version: Option<i32>,
}

impl Game {
    pub fn program(&self, name: &str) -> PathBuf {
        self.dir.join("bin").join(format!("{name}{}", std::env::consts::EXE_SUFFIX))
    }
    pub fn game_program(&self) -> PathBuf {
        self.program("dwe-game")
    }
    pub fn dwx(&self) -> PathBuf {
        self.program("dwx")
    }
}

pub struct GameStore {
    root: PathBuf,
}

/// Writes a file whole or not at all: a temporary file next to it, flushed, renamed over it.
pub fn write_atomically(path: &Path, bytes: &[u8]) -> Result<()> {
    let parent = path.parent().unwrap_or(Path::new("."));
    fs::create_dir_all(parent)?;
    let temporary = path.with_extension("tmp");
    {
        let mut file = File::create(&temporary)?;
        file.write_all(bytes)?;
        file.sync_all()?;
    }
    fs::rename(&temporary, path)?;
    #[cfg(unix)]
    if let Ok(folder) = File::open(parent) {
        let _ = folder.sync_all();
    }
    Ok(())
}

fn read_extractor_version(dir: &Path) -> Option<i32> {
    fs::read_to_string(dir.join(EXTRACTOR_VERSION_FILE)).ok()?.trim().parse().ok()
}

impl GameStore {
    pub fn new(data: &Path) -> Self {
        Self { root: data.join(GAME_FOLDER) }
    }

    fn versions(&self) -> PathBuf {
        self.root.join(VERSIONS_FOLDER)
    }

    pub fn version_dir(&self, version: &str) -> PathBuf {
        self.versions().join(version)
    }

    pub fn state(&self) -> State {
        fs::read_to_string(self.root.join(STATE_FILE))
            .ok()
            .and_then(|text| serde_json::from_str(&text).ok())
            .unwrap_or_default()
    }

    fn write_state(&self, state: &State) -> Result<()> {
        let text = serde_json::to_vec_pretty(state).expect("state serializes");
        write_atomically(&self.root.join(STATE_FILE), &text)
    }

    fn game(&self, installed: &InstalledVersion) -> Game {
        let dir = self.version_dir(&installed.version);
        Game {
            extractor_version: read_extractor_version(&dir).or(installed.extractor_version),
            dir,
            version: installed.version.clone(),
            commit: installed.commit.clone(),
        }
    }

    /// The game to run: the development override, else the current installed version (when its
    /// folder still has the game).
    pub fn current(&self) -> Option<Game> {
        if let Some(dir) = std::env::var_os(GAME_DIR_ENV).map(PathBuf::from) {
            let commit = fs::read_to_string(dir.join("VERSION")).unwrap_or_default().trim().to_string();
            return Some(Game { extractor_version: read_extractor_version(&dir), dir, version: "dev".into(), commit });
        }
        let game = self.game(self.state().current.as_ref()?);
        game.game_program().is_file().then_some(game)
    }

    pub fn has_previous(&self) -> bool {
        self.state().previous.is_some_and(|p| self.version_dir(&p.version).is_dir())
    }

    /// Unpacks a package (a .tar.xz with one top folder, as scripts/package_linux.sh writes it)
    /// and makes it the current version; the current one becomes the previous.
    pub fn install(&self, archive: &Path, installed: InstalledVersion) -> Result<Game> {
        fs::create_dir_all(self.versions())?;
        let staging = self.versions().join(format!(".staging-{}", installed.version));
        let _ = fs::remove_dir_all(&staging);
        let unpacked = unpack_tar_xz(archive, &staging);
        if let Err(error) = unpacked {
            let _ = fs::remove_dir_all(&staging);
            return Err(error);
        }
        let candidate = Game { dir: staging.clone(), version: installed.version.clone(), commit: String::new(), extractor_version: None };
        for program in [candidate.game_program(), candidate.dwx()] {
            if !program.is_file() {
                let _ = fs::remove_dir_all(&staging);
                return Err(Failure::new(code::BAD_MANIFEST, format!("the package has no {}", program.display())));
            }
        }
        let target = self.version_dir(&installed.version);
        let old = self.state();
        if target.exists() {
            // The same version again (a repaired install): keep the old folder until the swap.
            let aside = self.versions().join(format!(".old-{}", installed.version));
            let _ = fs::remove_dir_all(&aside);
            fs::rename(&target, &aside)?;
        }
        fs::rename(&staging, &target)?;
        let mut installed = installed;
        if installed.extractor_version.is_none() {
            installed.extractor_version = read_extractor_version(&target);
        }
        let previous = match old.current {
            Some(current) if current.version != installed.version => Some(current),
            _ => old.previous,
        };
        self.write_state(&State { current: Some(installed.clone()), previous })?;
        self.prune();
        Ok(self.game(&installed))
    }

    /// Goes back to the previous version; the current one becomes the previous, so a rollback can
    /// be undone.
    pub fn rollback(&self) -> Result<Game> {
        let state = self.state();
        let previous = state
            .previous
            .filter(|p| self.version_dir(&p.version).is_dir())
            .ok_or_else(|| Failure::new(code::NO_PREVIOUS, ""))?;
        self.write_state(&State { current: Some(previous.clone()), previous: state.current })?;
        Ok(self.game(&previous))
    }

    /// Removes every version folder that is neither the current nor the previous one.
    fn prune(&self) {
        let state = self.state();
        let keep: Vec<String> = [state.current, state.previous].into_iter().flatten().map(|v| v.version).collect();
        if let Ok(entries) = fs::read_dir(self.versions()) {
            for entry in entries.flatten() {
                let name = entry.file_name().to_string_lossy().into_owned();
                if !keep.contains(&name) {
                    let _ = fs::remove_dir_all(entry.path());
                }
            }
        }
    }
}

/// Whether an archive entry's path stays inside the folder it is unpacked into.
pub fn is_safe_relative(path: &Path) -> bool {
    !path.as_os_str().is_empty() && path.components().all(|c| matches!(c, Component::Normal(_) | Component::CurDir))
}

/// Unpacks a .tar.xz, dropping its top folder. Only plain files and folders: links and anything
/// that would land outside the target are refused.
pub fn unpack_tar_xz(archive: &Path, target: &Path) -> Result<()> {
    let file = File::open(archive)?;
    let mut tar = tar::Archive::new(xz2::read::XzDecoder::new(file));
    fs::create_dir_all(target)?;
    for entry in tar.entries()? {
        let mut entry = entry?;
        let path = entry.path()?.into_owned();
        if !is_safe_relative(&path) {
            return Err(Failure::new(code::BAD_MANIFEST, format!("unsafe path {}", path.display())));
        }
        let relative: PathBuf = path.components().skip(1).collect();
        if relative.as_os_str().is_empty() {
            continue;
        }
        let destination = target.join(&relative);
        let kind = entry.header().entry_type();
        if kind.is_dir() {
            fs::create_dir_all(&destination)?;
        } else if kind.is_file() {
            if let Some(parent) = destination.parent() {
                fs::create_dir_all(parent)?;
            }
            let mut out = File::create(&destination)?;
            let mut buffer = vec![0u8; 1 << 16];
            loop {
                let read = entry.read(&mut buffer)?;
                if read == 0 {
                    break;
                }
                out.write_all(&buffer[..read])?;
            }
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                let mode = entry.header().mode().unwrap_or(0o644) & 0o755;
                fs::set_permissions(&destination, fs::Permissions::from_mode(mode))?;
            }
        } else {
            return Err(Failure::new(code::BAD_MANIFEST, format!("{} is not a file or folder", path.display())));
        }
    }
    Ok(())
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// A package as scripts/package_linux.sh lays it out, with stub programs.
    pub(crate) fn make_package(dir: &Path, name: &str, marker: &str, extractor: Option<i32>) -> PathBuf {
        let archive = dir.join(format!("{name}.tar.xz"));
        let file = File::create(&archive).unwrap();
        let mut builder = tar::Builder::new(xz2::write::XzEncoder::new(file, 1));
        let mut add = |path: &str, body: &[u8], mode: u32| {
            let mut header = tar::Header::new_gnu();
            header.set_size(body.len() as u64);
            header.set_mode(mode);
            header.set_cksum();
            builder.append_data(&mut header, format!("{name}/{path}"), body).unwrap();
        };
        let exe = std::env::consts::EXE_SUFFIX;
        add(&format!("bin/dwe-game{exe}"), marker.as_bytes(), 0o755);
        add(&format!("bin/dwx{exe}"), b"dwx", 0o755);
        add("packs/game/pack.yaml", b"id: game\n", 0o644);
        add("VERSION", marker.as_bytes(), 0o644);
        if let Some(version) = extractor {
            add(EXTRACTOR_VERSION_FILE, format!("{version}\n").as_bytes(), 0o644);
        }
        builder.into_inner().unwrap().finish().unwrap();
        archive
    }

    fn version(v: &str) -> InstalledVersion {
        InstalledVersion { version: v.into(), commit: format!("c{v}"), extractor_version: None }
    }

    #[test]
    fn install_swap_and_rollback() {
        let dir = tempfile::tempdir().unwrap();
        let data = dir.path().join("data");
        let store = GameStore::new(&data);
        assert!(store.current().is_none());

        let one = make_package(dir.path(), "dwe-1", "one", Some(11));
        let game = store.install(&one, version("0.1.0-beta.1")).unwrap();
        assert_eq!(game.extractor_version, Some(11));
        assert_eq!(fs::read_to_string(game.game_program()).unwrap(), "one");
        assert!(!store.has_previous());
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert_eq!(fs::metadata(game.game_program()).unwrap().permissions().mode() & 0o111, 0o111);
        }

        let two = make_package(dir.path(), "dwe-2", "two", Some(12));
        let game = store.install(&two, version("0.1.0-beta.2")).unwrap();
        assert_eq!(store.current().unwrap(), game);
        assert_eq!(fs::read_to_string(game.game_program()).unwrap(), "two");
        assert!(store.has_previous());

        let three = make_package(dir.path(), "dwe-3", "three", None);
        store.install(&three, version("0.1.0-beta.3")).unwrap();
        // Only the current and the previous versions stay.
        assert!(!store.version_dir("0.1.0-beta.1").exists());
        assert!(store.version_dir("0.1.0-beta.2").exists());

        let back = store.rollback().unwrap();
        assert_eq!(back.version, "0.1.0-beta.2");
        assert_eq!(store.current().unwrap().version, "0.1.0-beta.2");
        // The rollback can be undone.
        assert_eq!(store.rollback().unwrap().version, "0.1.0-beta.3");
    }

    #[test]
    fn a_broken_package_leaves_the_current_version() {
        let dir = tempfile::tempdir().unwrap();
        let data = dir.path().join("data");
        let store = GameStore::new(&data);
        let one = make_package(dir.path(), "dwe-1", "one", None);
        store.install(&one, version("0.1.0-beta.1")).unwrap();

        let broken = dir.path().join("broken.tar.xz");
        fs::write(&broken, b"not an archive").unwrap();
        assert!(store.install(&broken, version("0.1.0-beta.2")).is_err());
        assert_eq!(store.current().unwrap().version, "0.1.0-beta.1");
        assert!(!store.versions().join(".staging-0.1.0-beta.2").exists());
        assert_eq!(store.rollback().unwrap_err().code, code::NO_PREVIOUS);
    }

    #[test]
    fn unsafe_paths_are_refused() {
        assert!(is_safe_relative(Path::new("a/b")));
        assert!(!is_safe_relative(Path::new("../a")));
        assert!(!is_safe_relative(Path::new("/etc/passwd")));
        assert!(!is_safe_relative(Path::new("a/../../b")));
    }

    #[test]
    fn atomic_writes_replace_whole_files() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("x/state.json");
        write_atomically(&file, b"one").unwrap();
        write_atomically(&file, b"two").unwrap();
        assert_eq!(fs::read(&file).unwrap(), b"two");
        assert!(!file.with_extension("tmp").exists());
    }
}
