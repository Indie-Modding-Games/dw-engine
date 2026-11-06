//! Reading maps through the packs' virtual file system, by the asset ids the engine uses
//! (map_document.cpp: mapDataId, findMapAssets, loadMapDocument; map_table.cpp).

use dwe_pack::{AssetId, ContentId, MAP_COMPOSITION, Vfs};

use crate::document::{MapDocument, Warp};
use crate::table::{MapEntry, MapTable};
use crate::{Error, Result};

/// The id of the composed map table.
pub const MAP_TABLE_ID: &str = "data/maps";

const BACKGROUND_PREFIX: &str = "background_";

/// A map's folder of assets: `map/<id>/`, or `map/<pack>/<name>/` for a pack's map.
pub fn map_folder(map_id: &str) -> String {
    format!("map/{}/", ContentId::asset_path_of(map_id))
}

/// The id of a map's data, `map/<id>/data`.
pub fn map_data_id(map_id: &str) -> Result<AssetId> {
    Ok(AssetId::parse(&format!("{}data", map_folder(map_id)))?)
}

/// Reads and parses a map's data.
pub fn load_map(vfs: &Vfs, map_id: &str) -> Result<MapDocument> {
    let text = vfs.read_text(&map_data_id(map_id)?)?;
    MapDocument::parse(map_id, &text)
}

/// The map table composed over every pack that has it.
pub fn load_map_table(vfs: &Vfs) -> Result<MapTable> {
    let id = AssetId::parse(MAP_TABLE_ID)?;
    let composed = vfs.read_composed_table(&id, &MAP_COMPOSITION)?;
    MapTable::from_value(composed.value, &composed.engine_numbered)
}

/// A background of a map: `map/<id>/background_<variant>`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Background {
    /// day, dusk, night, then palette3, palette4...
    pub variant: String,
    pub id: AssetId,
}

/// The backgrounds of a map in variant order: day, dusk, night, then the extra palettes.
pub fn backgrounds(vfs: &Vfs, map_id: &str) -> Vec<Background> {
    let folder = map_folder(map_id);
    let mut found: Vec<Background> = vfs
        .ids_with_prefix(&folder)
        .filter_map(|id| {
            let name = &id.as_str()[folder.len()..];
            let variant = name.strip_prefix(BACKGROUND_PREFIX)?;
            (!variant.contains('/')).then(|| Background {
                variant: variant.to_owned(),
                id: id.clone(),
            })
        })
        .collect();
    found.sort_by_key(|background| variant_order(&background.variant));
    found
}

fn variant_order(variant: &str) -> (u32, String) {
    match variant {
        "day" => (0, String::new()),
        "dusk" => (1, String::new()),
        "night" => (2, String::new()),
        other => {
            let number = other
                .strip_prefix("palette")
                .and_then(|n| n.parse::<u32>().ok())
                .unwrap_or(u32::MAX - 3);
            (3 + number, other.to_owned())
        }
    }
}

/// Where an exit leads (assets::resolveWarpTargets, docs/formats/mapas.md, "Destino de una
/// salida").
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WarpTarget<'a> {
    /// The exit leads to this map.
    Map(&'a MapEntry),
    /// `target_map_id` names no map, but the raw index is one of the original's: the game
    /// still goes there and logs a warning.
    UnknownIdFallback(&'a MapEntry),
    /// The id and the raw index name different maps: the game disables the warp.
    Conflict,
    /// The exit leads nowhere (an unused exit).
    Nowhere,
}

impl<'a> WarpTarget<'a> {
    /// The map the exit takes the player to, if any.
    pub fn map(self) -> Option<&'a MapEntry> {
        match self {
            Self::Map(entry) | Self::UnknownIdFallback(entry) => Some(entry),
            Self::Conflict | Self::Nowhere => None,
        }
    }
}

/// Decides where an exit leads: an id that names a map wins, unless the raw index names another
/// original map; otherwise a raw index of the original's maps.
pub fn warp_target<'a>(table: &'a MapTable, warp: &Warp) -> WarpTarget<'a> {
    let by_index = (warp.target_map >= 0)
        .then(|| table.find_by_original_index(warp.target_map))
        .flatten();
    match warp.target_map_id.as_deref().map(|id| table.find(id)) {
        Some(Some(entry)) => match by_index {
            Some(other) if other.id != entry.id => WarpTarget::Conflict,
            _ => WarpTarget::Map(entry),
        },
        Some(None) => by_index.map_or(WarpTarget::Nowhere, WarpTarget::UnknownIdFallback),
        None => by_index.map_or(WarpTarget::Nowhere, WarpTarget::Map),
    }
}

impl From<dwe_pack::Error> for Error {
    fn from(error: dwe_pack::Error) -> Self {
        Self::Pack(error)
    }
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeSet;

    use super::*;

    fn table() -> MapTable {
        let value = serde_yaml_ng::from_str(
            "maps:\n- {id: area_a, index: 0}\n- {id: area_b, index: 1}\n- {id: 'my_mod:meadow', index: 2}\n",
        )
        .unwrap();
        MapTable::from_value(value, &BTreeSet::from(["my_mod:meadow".to_owned()])).unwrap()
    }

    fn warp(target_map: i32, id: Option<&str>) -> Warp {
        Warp {
            exit: 0,
            spawn: Default::default(),
            rotation: 0,
            target_map,
            target_map_id: id.map(str::to_owned),
            target_exit: 0,
        }
    }

    #[test]
    fn resolves_exits_like_the_engine() {
        let table = table();
        let id_of = |target: WarpTarget| target.map().map(|entry| entry.id.clone());
        assert_eq!(
            id_of(warp_target(&table, &warp(1, Some("area_b")))),
            Some("area_b".into())
        );
        assert_eq!(
            id_of(warp_target(&table, &warp(-1, Some("my_mod:meadow")))),
            Some("my_mod:meadow".into())
        );
        assert_eq!(
            id_of(warp_target(&table, &warp(0, None))),
            Some("area_a".into())
        );
        assert_eq!(
            warp_target(&table, &warp(0, Some("area_b"))),
            WarpTarget::Conflict
        );
        assert!(matches!(
            warp_target(&table, &warp(1, Some("gone"))),
            WarpTarget::UnknownIdFallback(_)
        ));
        // A pack's map has no original index.
        assert_eq!(warp_target(&table, &warp(2, None)), WarpTarget::Nowhere);
        assert_eq!(warp_target(&table, &warp(-1, None)), WarpTarget::Nowhere);
    }

    #[test]
    fn folders_and_backgrounds() {
        assert_eq!(map_folder("area_a"), "map/area_a/");
        assert_eq!(map_folder("my_mod:meadow"), "map/my_mod/meadow/");
        let dir = tempfile::tempdir().unwrap();
        let pack = dir.path().join("base");
        std::fs::create_dir_all(pack.join("map/area_a/texture8")).unwrap();
        std::fs::write(
            pack.join("pack.yaml"),
            "id: base\nversion: 1.0.0\nlayer: base\n",
        )
        .unwrap();
        for file in [
            "background_night.png",
            "background_palette3.png",
            "background_day.png",
            "background_dusk.png",
            "texture8/000.png",
        ] {
            std::fs::write(pack.join("map/area_a").join(file), "").unwrap();
        }
        let vfs = Vfs::mount(&[dwe_pack::PackLocation {
            root: pack,
            origin: dwe_pack::PackOrigin::Engine,
        }])
        .unwrap();
        let variants: Vec<_> = backgrounds(&vfs, "area_a")
            .into_iter()
            .map(|background| background.variant)
            .collect();
        assert_eq!(variants, vec!["day", "dusk", "night", "palette3"]);
    }
}
