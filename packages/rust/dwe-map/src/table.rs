//! The table of every field map, `data/maps` (content/schemas/maps.schema.json), composed entry
//! by entry over the packs (map_table.hpp): the parts the map tools use.

use std::collections::BTreeSet;

use glam::Vec3;
use serde::Deserialize;

use crate::document::RawVec3;
use crate::world::from_original_axes;

/// A map of the table (MapEntry).
#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
pub struct MapEntry {
    /// The data's folder, `map/<id>/`.
    pub id: String,
    /// The original's index, which warps use, or the one the engine gave a pack's map.
    pub index: i32,
    /// Empty for entries without a file.
    #[serde(default)]
    pub filename: String,
    #[serde(default)]
    pub flags: i32,
    #[serde(default)]
    pub fixed_time_of_day: bool,
    #[serde(default)]
    pub has_npcs: bool,
    /// Index into [`MapTable::doors`].
    pub doors: Option<i32>,
    /// Index into [`MapTable::toilets`].
    pub toilet: Option<i32>,
    /// Index into [`MapTable::map_names`]: the name shown when the map loads.
    #[serde(default)]
    pub loading_name: i32,
    /// Whether the base gave the index (the original's), not the engine.
    #[serde(skip, default = "original_index")]
    pub original_index: bool,
}

fn original_index() -> bool {
    true
}

impl MapEntry {
    pub fn has_file(&self) -> bool {
        !self.filename.is_empty()
    }
}

/// A door model placed on a map (DoorStruct).
#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
pub struct MapDoor {
    /// None for an unused slot.
    pub model_id: Option<i32>,
    pub position: RawVec3,
    pub rotation: i32,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
pub struct MapDoorSet {
    pub index: i32,
    pub doors: Vec<MapDoor>,
}

/// The toilet's two points, in the original's x and z: the partner runs to the first, then to the
/// second.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize)]
pub struct MapToilet {
    pub index: i32,
    pub x1: i32,
    pub z1: i32,
    pub x2: i32,
    pub z2: i32,
}

impl MapToilet {
    /// The two points in world space, on the ground.
    pub fn points(&self) -> [Vec3; 2] {
        [
            from_original_axes(Vec3::new(self.x1 as f32, 0.0, self.z1 as f32)),
            from_original_axes(Vec3::new(self.x2 as f32, 0.0, self.z2 as f32)),
        ]
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Deserialize)]
pub struct MapTable {
    pub maps: Vec<MapEntry>,
    #[serde(default)]
    pub doors: Vec<MapDoorSet>,
    #[serde(default)]
    pub toilets: Vec<MapToilet>,
    /// As stored, padded with spaces.
    #[serde(default)]
    pub map_names: Vec<String>,
}

impl MapTable {
    /// Reads a composed table; `engine_numbered` are the maps whose index the engine gave.
    pub fn from_value(
        value: serde_yaml_ng::Value,
        engine_numbered: &BTreeSet<String>,
    ) -> crate::Result<Self> {
        let mut table: Self = serde_yaml_ng::from_value(value)
            .map_err(|error| crate::Error::Invalid(format!("data/maps: {error}")))?;
        for entry in &mut table.maps {
            entry.original_index = !engine_numbered.contains(&entry.id);
        }
        Ok(table)
    }

    pub fn find(&self, id: &str) -> Option<&MapEntry> {
        self.maps.iter().find(|entry| entry.id == id)
    }

    /// The original's map with this index, for a warp's raw `target_map`.
    pub fn find_by_original_index(&self, index: i32) -> Option<&MapEntry> {
        self.maps
            .iter()
            .find(|entry| entry.original_index && entry.index == index)
    }

    /// The name shown when the map loads, without the padding; empty when out of the table.
    pub fn display_name(&self, entry: &MapEntry) -> &str {
        usize::try_from(entry.loading_name)
            .ok()
            .and_then(|index| self.map_names.get(index))
            .map_or("", |name| name.trim())
    }

    pub fn doors_of(&self, entry: &MapEntry) -> Option<&MapDoorSet> {
        let index = entry.doors?;
        self.doors.iter().find(|set| set.index == index)
    }

    pub fn toilet_of(&self, entry: &MapEntry) -> Option<&MapToilet> {
        let index = entry.toilet?;
        self.toilets.iter().find(|toilet| toilet.index == index)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TABLE: &str = "maps:\n\
        - {id: area_a, index: 0, filename: AREA_A, flags: 128, doors: 0, toilet: null, loading_name: 1}\n\
        - {id: area_b, index: 3, filename: AREA_B, doors: null, toilet: 0, loading_name: 0}\n\
        - {id: 'my_mod:meadow', index: 4, doors: null, toilet: null}\n\
        doors:\n- index: 0\n  doors:\n  - {model_id: null, position: {x: 0, y: 0, z: 0}, rotation: 0}\n\
        \x20 - {model_id: 9, position: {x: 100, y: 0, z: -50}, rotation: 1024}\n\
        toilets:\n- {index: 0, x1: 10, z1: 20, x2: 30, z2: 40}\n\
        map_names: [\"  First  \", \" Second \"]\n";

    #[test]
    fn finds_entries_names_doors_and_toilets() {
        let value = serde_yaml_ng::from_str(TABLE).unwrap();
        let table =
            MapTable::from_value(value, &BTreeSet::from(["my_mod:meadow".to_owned()])).unwrap();
        let a = table.find("area_a").unwrap();
        assert_eq!(table.display_name(a), "Second");
        assert_eq!(table.doors_of(a).unwrap().doors[1].model_id, Some(9));
        let b = table.find_by_original_index(3).unwrap();
        assert_eq!(b.id, "area_b");
        assert_eq!(
            table.toilet_of(b).unwrap().points()[1],
            Vec3::new(30.0, 0.0, -40.0)
        );
        // A pack's map is never reached by a raw original index.
        assert!(table.find_by_original_index(4).is_none());
        assert!(!table.find("my_mod:meadow").unwrap().has_file());
    }
}
