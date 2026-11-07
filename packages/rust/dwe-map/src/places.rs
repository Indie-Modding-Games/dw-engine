//! A map's doors and toilet, which live in the map table (`data/maps`), not in the map's data:
//! the door set and the toilet the map's entry names. Saving them writes the target pack's
//! `data/maps` as the composition reads it (docs/formats/mapas.md): the `doors` and `toilets`
//! sections whole, as a pack that has them replaces them, and the map's entry when it has to name
//! another set (one shared with other maps gets a copy of its own).

use dwe_pack::{AssetId, MAP_COMPOSITION, MountedPack, Vfs};
use serde_yaml_ng::{Mapping, Value};

use crate::load::MAP_TABLE_ID;
use crate::save::FileChange;
use crate::table::{MapDoor, MapEntry, MapTable, MapToilet};
use crate::{Error, Result};

/// The door slots of a set: the engine reads exactly this many (map_table.cpp).
pub const DOORS_PER_SET: usize = 6;

/// The doors and the toilet of a map.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Places {
    /// The map's door slots ([`DOORS_PER_SET`]; empty when its entry names no set).
    pub doors: Vec<MapDoor>,
    /// The toilet's points (its `index` is the set's, kept as read).
    pub toilet: Option<MapToilet>,
}

impl Places {
    /// The places the table gives a map.
    pub fn of(table: &MapTable, entry: Option<&MapEntry>) -> Self {
        Self {
            doors: entry
                .and_then(|entry| table.doors_of(entry))
                .map(|set| set.doors.clone())
                .unwrap_or_default(),
            toilet: entry.and_then(|entry| table.toilet_of(entry)).cloned(),
        }
    }

    /// The door slots, all [`DOORS_PER_SET`] of them (unused ones without a model).
    pub fn door_slots(&self) -> Vec<MapDoor> {
        let mut doors = self.doors.clone();
        doors.resize(
            DOORS_PER_SET,
            MapDoor {
                model_id: None,
                position: crate::document::RawVec3 { x: 0, y: 0, z: 0 },
                rotation: 0,
            },
        );
        doors
    }
}

/// The `data/maps` file the target needs so that the map gets `after`, or None when the places
/// did not change.
pub fn plan_places_save(
    vfs: &Vfs,
    target: &MountedPack,
    map: &str,
    before: &Places,
    after: &Places,
) -> Result<Option<FileChange>> {
    if before == after {
        return Ok(None);
    }
    let id = AssetId::parse(MAP_TABLE_ID)?;
    let composed_table = vfs.read_composed_table(&id, &MAP_COMPOSITION)?;
    let numbered_by_engine = composed_table.engine_numbered.contains(map);
    let composed = composed_table.value;
    let maps = composed
        .get("maps")
        .and_then(Value::as_sequence)
        .cloned()
        .unwrap_or_default();
    let mut entry = maps
        .iter()
        .find(|entry| entry.get("id").and_then(Value::as_str) == Some(map))
        .cloned()
        .ok_or_else(|| Error::Invalid(format!("data/maps has no map '{map}'")))?;
    if numbered_by_engine {
        // A pack's map: the engine numbers it, and a pack that writes its number is refused.
        if let Some(entry) = entry.as_mapping_mut() {
            entry.remove("index");
        }
    }
    let file = target.file(&id).map(std::path::Path::to_path_buf);
    let mut own: Value = match &file {
        Some(file) => {
            let text = std::fs::read_to_string(file)
                .map_err(|error| Error::Invalid(format!("{}: {error}", file.display())))?;
            serde_yaml_ng::from_str(&text)
                .map_err(|error| Error::Invalid(format!("{}: {error}", file.display())))?
        }
        None => Value::Mapping(Mapping::new()),
    };
    let mut entry_changed = false;
    if before.doors != after.doors {
        let doors = to_value(&after.door_slots())?;
        entry_changed |= put_set(
            &composed,
            &maps,
            &mut entry,
            &mut own,
            "doors",
            "doors",
            |set| {
                set.insert("doors".into(), doors.clone());
            },
        );
    }
    if before.toilet != after.toilet {
        match &after.toilet {
            Some(toilet) => {
                let toilet = to_value(toilet)?;
                entry_changed |= put_set(
                    &composed,
                    &maps,
                    &mut entry,
                    &mut own,
                    "toilets",
                    "toilet",
                    |set| {
                        for key in ["x1", "z1", "x2", "z2"] {
                            set.insert(key.into(), toilet[key].clone());
                        }
                    },
                );
            }
            None => {
                entry_changed |= !entry["toilet"].is_null();
                entry["toilet"] = Value::Null;
            }
        }
    }
    if entry_changed {
        let own_maps = own
            .as_mapping_mut()
            .ok_or_else(|| Error::Invalid("data/maps is not a mapping".to_owned()))?
            .entry("maps".into())
            .or_insert_with(|| Value::Sequence(Vec::new()));
        let Some(own_maps) = own_maps.as_sequence_mut() else {
            return Err(Error::Invalid("data/maps: maps is not a list".to_owned()));
        };
        match own_maps
            .iter_mut()
            .find(|known| known.get("id").and_then(Value::as_str) == Some(map))
        {
            Some(known) => *known = entry,
            None => own_maps.push(entry),
        }
    }
    let text = serde_yaml_ng::to_string(&own).map_err(|error| Error::Invalid(error.to_string()))?;
    Ok(Some(FileChange {
        path: file.unwrap_or_else(|| target.root.join(format!("{MAP_TABLE_ID}.yaml"))),
        text: Some(text),
    }))
}

fn to_value(value: &impl serde::Serialize) -> Result<Value> {
    serde_yaml_ng::to_value(value).map_err(|error| Error::Invalid(error.to_string()))
}

/// Puts the map's set of a section (`doors` or `toilets`) into the pack's file: the whole section
/// as composed, with the map's set changed by `fill`. A set other maps share, or none, becomes a
/// new set; returns whether the entry now names another one.
fn put_set(
    composed: &Value,
    maps: &[Value],
    entry: &mut Value,
    own: &mut Value,
    section: &str,
    entry_key: &str,
    fill: impl Fn(&mut Mapping),
) -> bool {
    let mut sets = composed
        .get(section)
        .and_then(Value::as_sequence)
        .cloned()
        .unwrap_or_default();
    let index = entry.get(entry_key).and_then(Value::as_i64);
    let users = |index: i64| {
        maps.iter()
            .filter(|map| map.get(entry_key).and_then(Value::as_i64) == Some(index))
            .count()
    };
    let (index, renamed) = match index {
        Some(index) if users(index) <= 1 => (index, false),
        _ => {
            let next = sets
                .iter()
                .filter_map(|set| set.get("index").and_then(Value::as_i64))
                .max()
                .map_or(0, |max| max + 1);
            (next, true)
        }
    };
    let position = sets
        .iter()
        .position(|set| set.get("index").and_then(Value::as_i64) == Some(index));
    let mut set = match position {
        Some(position) if !renamed => sets[position].as_mapping().cloned().unwrap_or_default(),
        _ => {
            let mut set = Mapping::new();
            set.insert("index".into(), Value::from(index));
            set
        }
    };
    fill(&mut set);
    match position {
        Some(position) if !renamed => sets[position] = Value::Mapping(set),
        _ => sets.push(Value::Mapping(set)),
    }
    if let Some(own) = own.as_mapping_mut() {
        own.insert(section.into(), Value::Sequence(sets));
    }
    if renamed {
        entry[entry_key] = Value::from(index);
    }
    renamed
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::document::RawVec3;
    use crate::patch::tests::Packs;

    const TABLE: &str = "maps:\n\
        - {id: area_a, index: 0, filename: AREA_A, images_8bpp: 1, images_4bpp: 0, flags: 128, sound_id: 0, fixed_time_of_day: false, has_npcs: true, doors: 0, toilet: 0, loading_name: 0}\n\
        - {id: area_b, index: 1, filename: AREA_B, images_8bpp: 1, images_4bpp: 0, flags: 128, sound_id: 0, fixed_time_of_day: false, has_npcs: true, doors: 0, toilet: null, loading_name: 0}\n\
        - {id: area_c, index: 2, filename: AREA_C, images_8bpp: 1, images_4bpp: 0, flags: 128, sound_id: 0, fixed_time_of_day: false, has_npcs: true, doors: null, toilet: null, loading_name: 0}\n\
        doors:\n- index: 0\n  doors:\n  - {model_id: 9, position: {x: 1, y: 0, z: 2}, rotation: 0}\n\
        toilets:\n- {index: 0, x1: 10, z1: 20, x2: 30, z2: 40}\n\
        map_names: [one]\nlight_updates: []\n";

    fn stack() -> Packs {
        let packs = Packs::new();
        packs.pack("base", "base", 0);
        packs.file("base/data/maps.yaml", TABLE);
        packs.pack("mine", "mods", 0);
        packs
    }

    fn table(packs: &Packs) -> MapTable {
        crate::load_map_table(&packs.mount(&["base", "mine"])).unwrap()
    }

    fn save(packs: &Packs, map: &str, change: impl Fn(&mut Places)) -> MapTable {
        let vfs = packs.mount(&["base", "mine"]);
        let table = crate::load_map_table(&vfs).unwrap();
        let before = Places::of(&table, table.find(map));
        let mut after = before.clone();
        change(&mut after);
        let target = vfs.packs().iter().find(|pack| pack.id() == "mine").unwrap();
        let change = plan_places_save(&vfs, target, map, &before, &after)
            .unwrap()
            .unwrap();
        crate::save::write_changes(&[change]).unwrap();
        let table = self::table(packs);
        assert_eq!(Places::of(&table, table.find(map)).doors, after.doors);
        table
    }

    #[test]
    fn a_shared_set_gets_a_copy_and_an_own_set_is_changed() {
        let packs = stack();
        let table = save(&packs, "area_a", |places| {
            places.doors = places.door_slots();
            places.doors[1].model_id = Some(4);
            places.doors[1].position = RawVec3 { x: 5, y: 0, z: 6 };
        });
        // area_a shared set 0 with area_b: it now has set 1, and area_b keeps set 0.
        assert_eq!(table.find("area_a").unwrap().doors, Some(1));
        assert_eq!(table.find("area_b").unwrap().doors, Some(0));
        assert_eq!(
            table
                .doors_of(table.find("area_b").unwrap())
                .unwrap()
                .doors
                .len(),
            1
        );
        // A second save changes area_a's own set in place.
        let table = save(&packs, "area_a", |places| places.doors[1].rotation = 1024);
        assert_eq!(table.doors.len(), 2);
        assert_eq!(table.find("area_a").unwrap().doors, Some(1));
    }

    #[test]
    fn toilets_are_moved_added_and_removed() {
        let packs = stack();
        let table = save(&packs, "area_a", |places| {
            places.toilet.as_mut().unwrap().x1 = 99;
        });
        assert_eq!(
            table.toilet_of(table.find("area_a").unwrap()).unwrap().x1,
            99
        );
        assert_eq!(table.toilets.len(), 1);
        let table = save(&packs, "area_c", |places| {
            places.toilet = Some(MapToilet {
                index: 0,
                x1: 1,
                z1: 2,
                x2: 3,
                z2: 4,
            });
        });
        let c = table.find("area_c").unwrap();
        assert_eq!(c.toilet, Some(1));
        assert_eq!(table.toilet_of(c).unwrap().z2, 4);
        let table = save(&packs, "area_a", |places| places.toilet = None);
        assert_eq!(table.find("area_a").unwrap().toilet, None);
    }
}
