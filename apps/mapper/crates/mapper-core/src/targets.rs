//! Where the editor may save: the packs of the stack it can write (docs/modulos/apps/mapper.md,
//! "Guardar"), a new mod pack it creates, and the settings it remembers between runs.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use dwe_pack::{Layer, MountedPack, PackOrigin};
use serde::{Deserialize, Serialize};

use crate::session::Content;

/// The id of the team's game pack.
const TEAM_PACK: &str = "game";

/// What kind of pack a target is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TargetKind {
    /// A mod or user pack (mods folders, `--mods`).
    Mod,
    /// The team's game pack of a source checkout (`content/packs/game`).
    Team,
}

/// A pack the editor may write.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SaveTarget {
    pub id: String,
    pub root: PathBuf,
    pub kind: TargetKind,
}

/// Whether a folder lies in a git checkout (a `.git` in it or above it).
pub fn in_checkout(folder: &Path) -> bool {
    folder
        .ancestors()
        .any(|folder| folder.join(".git").exists())
}

/// Whether the editor may write a pack: a mod (never one that declares an engine layer, which the
/// mount leaves out anyway), or the team's game pack when it is a source checkout. Never the
/// base, an extracted or generated pack, or any other pack of the engine.
pub fn target_of(pack: &MountedPack) -> Option<SaveTarget> {
    let kind = match pack.origin {
        PackOrigin::Mod if !pack.manifest.layer.is_engine_layer() => TargetKind::Mod,
        PackOrigin::Engine
            if pack.id() == TEAM_PACK
                && pack.manifest.layer != Layer::Base
                && in_checkout(&pack.root) =>
        {
            TargetKind::Team
        }
        _ => return None,
    };
    Some(SaveTarget {
        id: pack.id().to_owned(),
        root: pack.root.clone(),
        kind,
    })
}

/// The packs of the content the editor may write, from the top of the stack down.
pub fn writable_targets(content: &Content) -> Vec<SaveTarget> {
    content
        .vfs()
        .packs()
        .iter()
        .rev()
        .filter_map(target_of)
        .collect()
}

/// Whether a pack id is valid: 1 to 64 of `a-z`, `0-9` and `_`.
pub fn is_pack_id(id: &str) -> bool {
    (1..=64).contains(&id.len())
        && id
            .bytes()
            .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_')
}

/// Creates a mod pack for map edits in a mods folder: `<folder>/<id>/pack.yaml` on the `mods`
/// layer, priority 0, declaring the schemas of what the editor writes. Returns its folder.
pub fn create_mod_pack(folder: &Path, id: &str) -> Result<PathBuf, String> {
    if !is_pack_id(id) {
        return Err(format!("{}: {id}", crate::text::BAD_PACK_ID));
    }
    let root = folder.join(id);
    if root.exists() {
        return Err(format!("{}: {}", crate::text::PACK_EXISTS, root.display()));
    }
    let manifest = format!(
        "id: {id}\nversion: 0.1.0\nlayer: mods\npriority: 0\nname: \"{id}\"\n\
         description: \"{}\"\nschemas:\n  map:\n  - \"map/*/data\"\n  - \"map/*/*/data\"\n  \
         map_patch:\n  - \"map/*/patch\"\n  - \"map/*/*/patch\"\n",
        crate::text::NEW_PACK_DESCRIPTION
    );
    std::fs::create_dir_all(&root).map_err(|error| error.to_string())?;
    std::fs::write(root.join("pack.yaml"), manifest).map_err(|error| error.to_string())?;
    Ok(root)
}

/// What the editor remembers between runs.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    /// The folder of the pack saved into last.
    pub last_target: Option<PathBuf>,
    /// Whether a save asks a connected game to read the map again.
    pub reload_on_save: Option<bool>,
    /// The dwx program that makes `field/script_warps` again when a save changes map changes.
    #[serde(default)]
    pub dwx: Option<PathBuf>,
    /// Where the user put maps of the world graph, by content folder and map id.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub world_layouts: BTreeMap<String, BTreeMap<String, [i32; 2]>>,
}

impl Settings {
    /// The settings file: `dwe-mapper/settings.yaml` in the user's configuration folder.
    pub fn default_file() -> Option<PathBuf> {
        let folder = std::env::var_os("XDG_CONFIG_HOME")
            .map(PathBuf::from)
            .or_else(|| std::env::var_os("APPDATA").map(PathBuf::from))
            .or_else(|| std::env::var_os("HOME").map(|home| PathBuf::from(home).join(".config")))?;
        Some(folder.join("dwe-mapper").join("settings.yaml"))
    }

    /// Reads the settings; missing or unreadable ones are the defaults.
    pub fn load(file: &Path) -> Self {
        std::fs::read_to_string(file)
            .ok()
            .and_then(|text| serde_yaml_ng::from_str(&text).ok())
            .unwrap_or_default()
    }

    pub fn store(&self, file: &Path) -> std::io::Result<()> {
        if let Some(folder) = file.parent() {
            std::fs::create_dir_all(folder)?;
        }
        let text = serde_yaml_ng::to_string(self).map_err(std::io::Error::other)?;
        std::fs::write(file, text)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pack_ids_and_new_packs() {
        assert!(is_pack_id("my_maps_2"));
        assert!(!is_pack_id("My-Maps"));
        assert!(!is_pack_id(""));
        let folder = tempfile::tempdir().unwrap();
        let root = create_mod_pack(folder.path(), "my_maps").unwrap();
        let manifest = dwe_pack::PackManifest::read(&root).unwrap();
        assert_eq!(manifest.id, "my_maps");
        assert_eq!(manifest.layer, Layer::Mods);
        assert!(manifest.schemas.contains_key("map_patch"));
        assert!(create_mod_pack(folder.path(), "my_maps").is_err());
    }

    #[test]
    fn settings_round_trip() {
        let folder = tempfile::tempdir().unwrap();
        let file = folder.path().join("a/settings.yaml");
        assert_eq!(Settings::load(&file), Settings::default());
        let settings = Settings {
            last_target: Some(PathBuf::from("/x/mods/mine")),
            reload_on_save: Some(false),
            dwx: Some(PathBuf::from("/x/bin/dwx")),
            world_layouts: BTreeMap::from([(
                "/x/content".to_owned(),
                BTreeMap::from([("area_a".to_owned(), [10, -20])]),
            )]),
        };
        settings.store(&file).unwrap();
        assert_eq!(Settings::load(&file), settings);
    }
}
