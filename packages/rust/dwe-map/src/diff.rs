//! Writing map patches (docs/formats/mapas.md, "Parches"): the patch that turns one map document
//! into another, with only what differs. Each change names its entry as a patch must (an exit by
//! its `exit`; a light, an NPC, a sprite or an instance by its place) and gives only the fields
//! that differ, each whole. The collision goes as rectangles of cells while they are few
//! ([`MOST_RECTANGLES`]) and as the whole grid beyond that.

use serde::Serialize;
use serde_yaml_ng::{Mapping, Value};

use crate::document::{MapDocument, Npc};
use crate::world::{Cell, GRID_SIZE};

/// The most rectangles of cells a written patch holds; a collision change that needs more is
/// written as the whole grid (about 10 KB, against about 60 bytes a rectangle).
pub const MOST_RECTANGLES: usize = 64;

/// The patch that turns `floor` into `edited`, or None when they do not differ. Both must have
/// the same entries (a patch neither adds nor removes any).
pub fn diff_patch(floor: &MapDocument, edited: &MapDocument) -> Option<Value> {
    let mut patch = Mapping::new();
    let camera = changed_fields(&to_mapping(&floor.camera), &to_mapping(&edited.camera));
    if !camera.is_empty() {
        patch.insert("camera".into(), Value::Mapping(camera));
    }
    let lights = by_place(&floor.lights, &edited.lights, "index");
    if !lights.is_empty() {
        patch.insert("lights".into(), Value::Sequence(lights));
    }
    let warps: Vec<Value> = edited
        .warps
        .iter()
        .filter_map(|warp| {
            let below = floor.warp(warp.exit)?;
            let mut fields = changed_fields(&to_mapping(below), &to_mapping(warp));
            fields.shift_remove("exit");
            (!fields.is_empty()).then(|| entry("exit", warp.exit, fields))
        })
        .collect();
    if !warps.is_empty() {
        patch.insert("warps".into(), Value::Sequence(warps));
    }
    let npcs: Vec<Value> = floor
        .npcs
        .iter()
        .zip(&edited.npcs)
        .enumerate()
        .filter_map(|(place, (below, npc))| {
            let fields = changed_fields(&npc_fields(below), &npc_fields(npc));
            (!fields.is_empty()).then(|| entry("index", place as i32, fields))
        })
        .collect();
    if !npcs.is_empty() {
        patch.insert("npcs".into(), Value::Sequence(npcs));
    }
    if let (Some(below), Some(objects)) = (&floor.objects, &edited.objects) {
        let mut changes = Mapping::new();
        let sprites = by_place(&below.sprites, &objects.sprites, "index");
        if !sprites.is_empty() {
            changes.insert("sprites".into(), Value::Sequence(sprites));
        }
        let instances = by_place(&below.instances, &objects.instances, "index");
        if !instances.is_empty() {
            changes.insert("instances".into(), Value::Sequence(instances));
        }
        if !changes.is_empty() {
            patch.insert("objects".into(), Value::Mapping(changes));
        }
    }
    if let Some(collision) = collision_change(floor, edited) {
        patch.insert("collision".into(), collision);
    }
    (!patch.is_empty()).then_some(Value::Mapping(patch))
}

/// The text of a patch, written as the tools write YAML.
pub fn patch_text(patch: &Value) -> String {
    crate::emit::to_yaml(patch)
}

/// A rectangle of cells with one value, as a patch gives it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CellRect {
    pub column: i32,
    pub row: i32,
    pub width: i32,
    pub height: i32,
    pub value: u8,
}

/// Covers the cells that differ with rectangles of one value each, greedily: from the first
/// uncovered cell in reading order, as wide as the row allows, then as tall as whole rows allow.
pub fn changed_rectangles(floor: &MapDocument, edited: &MapDocument) -> Vec<CellRect> {
    let size = GRID_SIZE as usize;
    let value =
        |column: usize, row: usize| edited.collision.value(Cell::new(column as i32, row as i32));
    let mut pending: Vec<bool> = floor
        .collision
        .values()
        .iter()
        .zip(edited.collision.values())
        .map(|(below, now)| below != now)
        .collect();
    let mut rectangles = Vec::new();
    for row in 0..size {
        for column in 0..size {
            if !pending[row * size + column] {
                continue;
            }
            let wanted = value(column, row);
            let fits = |column: usize, row: usize| {
                pending[row * size + column] && value(column, row) == wanted
            };
            let mut width = 1;
            while column + width < size && fits(column + width, row) {
                width += 1;
            }
            let mut height = 1;
            while row + height < size && (column..column + width).all(|at| fits(at, row + height)) {
                height += 1;
            }
            for covered in row..row + height {
                pending[covered * size + column..covered * size + column + width].fill(false);
            }
            rectangles.push(CellRect {
                column: column as i32,
                row: row as i32,
                width: width as i32,
                height: height as i32,
                value: wanted,
            });
        }
    }
    rectangles
}

fn collision_change(floor: &MapDocument, edited: &MapDocument) -> Option<Value> {
    if floor.collision == edited.collision {
        return None;
    }
    let rectangles = changed_rectangles(floor, edited);
    let mut collision = Mapping::new();
    if rectangles.len() > MOST_RECTANGLES {
        collision.insert("grid".into(), to_value(&edited.collision.to_section()));
    } else {
        let cells = rectangles
            .iter()
            .map(|rect| {
                let mut cell = Mapping::new();
                cell.insert("column".into(), rect.column.into());
                cell.insert("row".into(), rect.row.into());
                if rect.width != 1 {
                    cell.insert("width".into(), rect.width.into());
                }
                if rect.height != 1 {
                    cell.insert("height".into(), rect.height.into());
                }
                cell.insert("value".into(), rect.value.into());
                Value::Mapping(cell)
            })
            .collect();
        collision.insert("cells".into(), Value::Sequence(cells));
    }
    Some(Value::Mapping(collision))
}

/// An NPC slot's fields as the document has them: its own and the others.
fn npc_fields(npc: &Npc) -> Mapping {
    let mut fields = to_mapping(npc);
    for (key, value) in &npc.other {
        fields.insert(key.clone(), value.clone());
    }
    fields
}

/// The changes of a list whose entries a patch names by place.
fn by_place<T: Serialize>(floor: &[T], edited: &[T], key: &str) -> Vec<Value> {
    floor
        .iter()
        .zip(edited)
        .enumerate()
        .filter_map(|(place, (below, now))| {
            let fields = changed_fields(&to_mapping(below), &to_mapping(now));
            (!fields.is_empty()).then(|| entry(key, place as i32, fields))
        })
        .collect()
}

/// The fields of `edited` whose value is not the one in `floor`, in `edited`'s order.
fn changed_fields(floor: &Mapping, edited: &Mapping) -> Mapping {
    edited
        .iter()
        .filter(|(key, value)| floor.get(*key) != Some(*value))
        .map(|(key, value)| (key.clone(), value.clone()))
        .collect()
}

/// A change naming its entry first.
fn entry(key: &str, number: i32, fields: Mapping) -> Value {
    let mut change = Mapping::new();
    change.insert(key.into(), number.into());
    change.extend(fields);
    Value::Mapping(change)
}

fn to_value<T: Serialize>(value: &T) -> Value {
    serde_yaml_ng::to_value(value).unwrap_or(Value::Null)
}

fn to_mapping<T: Serialize>(value: &T) -> Mapping {
    match to_value(value) {
        Value::Mapping(mapping) => mapping,
        _ => Mapping::new(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::patch::{MapPatchText, patch_map_document, tests::fixture_map};

    fn fixture() -> MapDocument {
        MapDocument::parse("testa01", &fixture_map("null")).unwrap()
    }

    /// Applies a written patch to the floor as the engine would.
    fn applied(floor: &MapDocument, patch: &Value) -> MapDocument {
        let text = MapPatchText {
            pack: "mod_a".to_owned(),
            base_rights: false,
            file: "mod_a/map/testa01/patch.yaml".to_owned(),
            text: patch_text(patch),
        };
        crate::patch::check_map_patch(&text.text).unwrap();
        patch_map_document("testa01", &floor.to_yaml(), &[text]).unwrap()
    }

    fn same(a: &MapDocument, b: &MapDocument) -> bool {
        a.to_tree() == b.to_tree()
    }

    #[test]
    fn no_edit_is_no_patch() {
        let floor = fixture();
        assert_eq!(diff_patch(&floor, &floor.clone()), None);
    }

    #[test]
    fn a_patch_holds_only_what_changed_and_gives_the_edit_back() {
        let floor = fixture();
        let mut edited = floor.clone();
        edited.warps[0].target_exit = 4;
        edited.warps[0].spawn.x = 120;
        edited.camera.projection = 1900;
        edited.collision.set(Cell::new(30, 40), 128);
        edited.collision.set(Cell::new(31, 40), 128);
        edited.collision.set(Cell::new(30, 41), 128);
        edited.collision.set(Cell::new(31, 41), 128);
        edited.collision.set(Cell::new(50, 50), 112);
        let patch = diff_patch(&floor, &edited).unwrap();
        let text = patch_text(&patch);
        assert_eq!(
            text,
            "camera:\n  projection: 1900\nwarps:\n- exit: 0\n  spawn:\n    x: 120\n    y: 0\n    z: -2425\n  \
             target_exit: 4\ncollision:\n  cells:\n  - column: 30\n    row: 40\n    width: 2\n    height: 2\n    \
             value: 128\n  - column: 50\n    row: 50\n    value: 112\n"
        );
        assert!(same(&applied(&floor, &patch), &edited));
    }

    #[test]
    fn npc_fields_by_place_and_their_other_fields() {
        let floor =
            MapDocument::parse("testa01", &crate::patch::tests::fixture_map_with_npc()).unwrap();
        let mut edited = floor.clone();
        edited.npcs[0].position.z = -900;
        edited.npcs[0]
            .waypoints
            .push(crate::document::RawVec3 { x: 1, y: 0, z: 2 });
        edited.npcs[0].ai_sections[1] = 1;
        edited.npcs[0].other.insert("hp".into(), Value::from(321));
        let patch = diff_patch(&floor, &edited).unwrap();
        let npc = &patch["npcs"][0];
        assert_eq!(npc["index"], Value::from(0));
        let keys: Vec<_> = npc
            .as_mapping()
            .unwrap()
            .keys()
            .filter_map(Value::as_str)
            .collect();
        assert_eq!(
            keys,
            ["index", "position", "ai_sections", "waypoints", "hp"]
        );
        assert!(same(&applied(&floor, &patch), &edited));
    }

    #[test]
    fn many_rectangles_turn_into_the_whole_grid() {
        let floor = fixture();
        let mut edited = floor.clone();
        // A checkerboard: every cell its own rectangle.
        for row in 10..30 {
            for column in 10..30 {
                if (row + column) % 2 == 0 {
                    edited.collision.set(Cell::new(column, row), 51);
                }
            }
        }
        assert!(changed_rectangles(&floor, &edited).len() > MOST_RECTANGLES);
        let patch = diff_patch(&floor, &edited).unwrap();
        assert!(patch["collision"]["grid"].is_mapping());
        assert!(same(&applied(&floor, &patch), &edited));
    }

    #[test]
    fn rectangles_cover_every_changed_cell_once() {
        let floor = fixture();
        let mut edited = floor.clone();
        for (column, row, value) in [(5, 5, 51), (6, 5, 51), (7, 5, 52), (5, 6, 51), (6, 6, 51)] {
            edited.collision.set(Cell::new(column, row), value);
        }
        let rectangles = changed_rectangles(&floor, &edited);
        assert_eq!(
            rectangles,
            [
                CellRect {
                    column: 5,
                    row: 5,
                    width: 2,
                    height: 2,
                    value: 51
                },
                CellRect {
                    column: 7,
                    row: 5,
                    width: 1,
                    height: 1,
                    value: 52
                },
            ]
        );
    }
}
