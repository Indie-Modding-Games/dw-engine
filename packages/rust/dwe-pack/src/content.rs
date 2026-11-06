use std::path::{Path, PathBuf};

use crate::{Error, MountedPack, PackLocation, PackOrigin, Result, Vfs, find::find_packs};

/// The folder name of a content folder's extra packs, and the folder name of the team's game pack.
const PACKS_FOLDER: &str = "packs";
const GAME_PACK_NAME: &str = "game";

/// The folders the game mounts (content/game_content.hpp): the content folder and its `packs/`,
/// the team's packs when those have no game pack, and the mods folders.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct GameFolders {
    pub content: PathBuf,
    /// The team's packs (the source tree's `content/packs`), mounted when the content folder has
    /// no game pack.
    pub team_packs: Option<PathBuf>,
    pub mods: Vec<PathBuf>,
}

/// A mod that mounting left out, and why.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SkippedMod {
    pub path: PathBuf,
    pub reason: String,
}

/// The mounted content: the stack and what was left out.
#[derive(Debug)]
pub struct GameContent {
    pub vfs: Vfs,
    pub skipped_mods: Vec<SkippedMod>,
    /// `.pak` archives found but not read: the tools read pack folders only.
    pub archives: Vec<PathBuf>,
}

impl GameContent {
    /// Mounts the game's own packs, which must all mount, and then each mod that can: a mod
    /// with a broken manifest, a missing dependency or an engine layer is left out with its
    /// reason, as the game does.
    pub fn mount(folders: &GameFolders) -> Result<Self> {
        let mut archives = Vec::new();
        let mut game_packs = Vec::new();
        let mut add = |folder: &Path, packs: &mut Vec<PathBuf>| -> Result<()> {
            let found = find_packs(folder)?;
            archives.extend(found.archives);
            packs.extend(found.packs);
            Ok(())
        };
        add(&folders.content, &mut game_packs)?;
        let extra = folders.content.join(PACKS_FOLDER);
        if extra.is_dir() {
            add(&extra, &mut game_packs)?;
        }
        let has_game_pack = game_packs
            .iter()
            .any(|pack| pack.file_name().is_some_and(|name| name == GAME_PACK_NAME));
        if !has_game_pack && let Some(team) = &folders.team_packs {
            add(team, &mut game_packs)?;
        }
        if game_packs.is_empty() {
            return Err(Error::invalid(format!(
                "{} has no pack (a folder with a pack.yaml)",
                folders.content.display()
            )));
        }
        let packs = game_packs
            .iter()
            .map(|root| {
                MountedPack::read(&PackLocation {
                    root: root.clone(),
                    origin: PackOrigin::Engine,
                })
            })
            .collect::<Result<Vec<_>>>()?;
        // The game's own packs must all mount.
        let mut vfs = Vfs::from_packs(packs.clone())?;

        let mut skipped_mods = Vec::new();
        let mut candidates = Vec::new();
        for folder in &folders.mods {
            match find_packs(folder) {
                Ok(found) => {
                    archives.extend(found.archives);
                    candidates.extend(found.packs);
                }
                Err(error) => skipped_mods.push(SkippedMod {
                    path: folder.clone(),
                    reason: error.to_string(),
                }),
            }
        }
        let mut mods = Vec::new();
        for root in candidates {
            let location = PackLocation {
                root: root.clone(),
                origin: PackOrigin::Mod,
            };
            match MountedPack::read(&location) {
                Ok(pack) if pack.manifest.layer.is_engine_layer() => {
                    skipped_mods.push(SkippedMod {
                        path: root,
                        reason: format!(
                            "mod '{}' declares the layer {}, which only the engine's packs use",
                            pack.id(),
                            pack.manifest.layer.name()
                        ),
                    })
                }
                Ok(pack) => mods.push(pack),
                Err(error) => skipped_mods.push(SkippedMod {
                    path: root,
                    reason: error.to_string(),
                }),
            }
        }
        // A mod that breaks the stack (a missing dependency, a repeated id) is left out on its
        // own; the others still mount. Repeat until every remaining mod mounts.
        loop {
            let mut stack = packs.clone();
            stack.extend(mods.iter().cloned());
            match Vfs::from_packs(stack) {
                Ok(mounted) => {
                    vfs = mounted;
                    break;
                }
                Err(error) => {
                    let Some(culprit) = mods.iter().position(|candidate| {
                        let mut alone = packs.clone();
                        alone.push(candidate.clone());
                        Vfs::from_packs(alone).is_err()
                    }) else {
                        // Mods that only fail together: leave them all out.
                        skipped_mods.extend(mods.drain(..).map(|pack| SkippedMod {
                            path: pack.root,
                            reason: error.to_string(),
                        }));
                        break;
                    };
                    let pack = mods.remove(culprit);
                    let mut alone = packs.clone();
                    alone.push(pack.clone());
                    let reason = Vfs::from_packs(alone)
                        .err()
                        .map_or_else(|| error.to_string(), |e| e.to_string());
                    skipped_mods.push(SkippedMod {
                        path: pack.root,
                        reason,
                    });
                }
            }
        }
        archives.sort();
        Ok(Self {
            vfs,
            skipped_mods,
            archives,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pack(root: &Path, folder: &str, manifest: &str) -> PathBuf {
        let path = root.join(folder);
        std::fs::create_dir_all(&path).unwrap();
        std::fs::write(path.join("pack.yaml"), manifest).unwrap();
        path
    }

    #[test]
    fn mounts_content_its_packs_the_team_packs_and_good_mods() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        pack(
            root,
            "content/base",
            "id: base\nversion: 1.0.0\nlayer: base\n",
        );
        pack(
            root,
            "content/packs/extra",
            "id: extra\nversion: 1.0.0\nlayer: remaster\n",
        );
        pack(
            root,
            "team/game",
            "id: game\nversion: 1.0.0\nlayer: base\npriority: 1\n",
        );
        pack(root, "mods/good", "id: good\nversion: 1.0.0\nlayer: mods\n");
        pack(
            root,
            "mods/sneaky",
            "id: sneaky\nversion: 1.0.0\nlayer: base\n",
        );
        pack(
            root,
            "mods/needy",
            "id: needy\nversion: 1.0.0\nlayer: mods\ndependencies:\n  - id: nowhere\n    min_version: 1.0.0\n",
        );
        let content = GameContent::mount(&GameFolders {
            content: root.join("content"),
            team_packs: Some(root.join("team")),
            mods: vec![root.join("mods")],
        })
        .unwrap();
        let ids: Vec<_> = content.vfs.packs().iter().map(MountedPack::id).collect();
        assert_eq!(ids, vec!["base", "game", "extra", "good"]);
        let skipped: Vec<_> = content
            .skipped_mods
            .iter()
            .map(|skipped| skipped.path.file_name().unwrap().to_str().unwrap())
            .collect();
        assert_eq!(skipped, vec!["sneaky", "needy"]);
        assert_eq!(
            content.vfs.packs()[0].origin,
            PackOrigin::Engine,
            "the content's packs are the engine's"
        );
        assert_eq!(content.vfs.packs()[3].origin, PackOrigin::Mod);
    }

    #[test]
    fn the_team_packs_stay_out_when_the_content_has_a_game_pack() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        pack(
            root,
            "content/base",
            "id: base\nversion: 1.0.0\nlayer: base\n",
        );
        pack(
            root,
            "content/game",
            "id: game\nversion: 1.0.0\nlayer: base\n",
        );
        pack(root, "team/game", "id: game\nversion: 1.0.0\nlayer: base\n");
        let content = GameContent::mount(&GameFolders {
            content: root.join("content"),
            team_packs: Some(root.join("team")),
            mods: Vec::new(),
        })
        .unwrap();
        assert_eq!(content.vfs.packs().len(), 2);
        assert!(
            GameContent::mount(&GameFolders {
                content: root.join("team/empty_nowhere"),
                ..GameFolders::default()
            })
            .is_err()
        );
    }
}
