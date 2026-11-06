//! Partial overrides of a map's data (docs/formats/mapas.md, "Parches";
//! content/schemas/map_patch.schema.json), applied exactly as the engine applies them
//! (packages/engine/assets/src/map_patch.cpp). A pack ships `map/<id>/patch` next to the map's
//! `map/<id>/data` to change one exit, one NPC slot, an object's sprite or instance, a light or the
//! camera field by field, and the collision whole or by rectangles of cells. The patches of the
//! pack whose data wins and of every pack above it apply in stack order; a field a patch gives
//! replaces the data's whole (a vector, a list of waypoints).

use std::path::Path;

use dwe_pack::{AssetId, MountedPack, Vfs};
use serde_yaml_ng::{Mapping, Value};

use crate::collision::{Collision, CollisionSection};
use crate::document::{ExitPatch, MapDocument};
use crate::load::{map_data_id, map_folder};
use crate::reader::{
    Checked, expect_keys, field, integer, optional_integer, sequence, whole_number,
};
use crate::world::{Cell, GRID_SIZE};
use crate::{Error, Result};

/// The last segment of a map's patch id.
pub const PATCH_LEAF: &str = "patch";

// The sections of a patch, named as the parts of the data they change.
const CAMERA: &str = "camera";
const LIGHTS: &str = "lights";
const WARPS: &str = "warps";
const NPCS: &str = "npcs";
const OBJECTS: &str = "objects";
const SPRITES: &str = "sprites";
const INSTANCES: &str = "instances";
const COLLISION: &str = "collision";
// A whole collision grid, and rectangles of cells.
const GRID: &str = "grid";
const CELLS: &str = "cells";
// What names the entry a change is for: its place in the data's list, or an exit's number.
const INDEX: &str = "index";
const EXIT: &str = "exit";

/// The context of a patch's own problems.
const PATCH_CONTEXT: &str = "map patch";

/// A map's patch as [`patch_map_document`] applies it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MapPatchText {
    pub pack: String,
    /// Whether the pack has the base's rights: the exits it changes are checked as the base's.
    pub base_rights: bool,
    /// The file, for the errors.
    pub file: String,
    pub text: String,
}

/// The asset id of a map's patch, `map/<id>/patch`; a namespaced id is a folder per part,
/// `map/<pack>/<name>/patch`, as the data's.
pub fn map_patch_id(map_id: &str) -> Result<AssetId> {
    Ok(AssetId::parse(&format!(
        "{}{PATCH_LEAF}",
        map_folder(map_id)
    ))?)
}

/// A change to one entry of one of the data's lists: the entry's number and the patch's mapping,
/// whose other keys replace the entry's.
struct EntryChange {
    number: i32,
    fields: Mapping,
}

/// A rectangle of collision cells set to one value.
#[derive(Clone, Copy)]
struct CellChange {
    column: i32,
    row: i32,
    width: i32,
    height: i32,
    value: u8,
}

/// A patch as read.
#[derive(Default)]
struct MapPatch {
    camera: Option<Mapping>,
    lights: Vec<EntryChange>,
    warps: Vec<EntryChange>,
    npcs: Vec<EntryChange>,
    objects: bool,
    sprites: Vec<EntryChange>,
    instances: Vec<EntryChange>,
    grid: Option<Value>,
    cells: Vec<CellChange>,
}

fn read_changes(parent: &Mapping, section: &str, key: &str) -> Checked<Vec<EntryChange>> {
    let mut changes: Vec<EntryChange> = Vec::new();
    let Some(list) = parent.get(section) else {
        return Ok(changes);
    };
    for item in sequence(list, section)? {
        let Some((fields, number)) = item
            .as_mapping()
            .and_then(|fields| Some((fields, fields.get(key)?)))
        else {
            return Err(format!(
                "every change of '{section}' must be a mapping with its '{key}'"
            ));
        };
        let number: i32 = integer(number)?;
        if number < 0 {
            return Err(format!("'{key}' counts from 0, not {number}"));
        }
        if changes.iter().any(|change| change.number == number) {
            return Err(format!("'{key}' {number} of '{section}' is changed twice"));
        }
        changes.push(EntryChange {
            number,
            fields: fields.clone(),
        });
    }
    Ok(changes)
}

fn read_cells(node: &Value) -> Checked<Vec<CellChange>> {
    let mut cells = Vec::new();
    for item in sequence(node, CELLS)? {
        let mapping = expect_keys(
            item,
            "a rectangle of cells",
            &["column", "row", "value"],
            &["width", "height"],
        )?;
        let cell = CellChange {
            column: integer(field(mapping, "column")?)?,
            row: integer(field(mapping, "row")?)?,
            width: optional_integer(mapping, "width")?.unwrap_or(1),
            height: optional_integer(mapping, "height")?.unwrap_or(1),
            value: integer(field(mapping, "value")?)?,
        };
        if cell.width < 1 || cell.height < 1 {
            return Err(format!(
                "a rectangle of cells is at least 1 x 1, not {} x {}",
                cell.width, cell.height
            ));
        }
        let inside = |column: i64, row: i64| {
            let grid = 0..i64::from(GRID_SIZE);
            grid.contains(&column) && grid.contains(&row)
        };
        let (column, row) = (i64::from(cell.column), i64::from(cell.row));
        let last = (
            column + i64::from(cell.width) - 1,
            row + i64::from(cell.height) - 1,
        );
        if !inside(column, row) || !inside(last.0, last.1) {
            return Err(format!(
                "the cells from column {}, row {} ({} x {}) leave the {GRID_SIZE} x {GRID_SIZE} \
                 collision grid",
                cell.column, cell.row, cell.width, cell.height
            ));
        }
        cells.push(cell);
    }
    Ok(cells)
}

/// A whole collision grid, checked as the data's `collision` (readMapCollision).
fn check_grid(grid: &Value) -> Checked<()> {
    let section: CollisionSection =
        serde_yaml_ng::from_value(grid.clone()).map_err(|error| format!("collision: {error}"))?;
    Collision::from_section(&section)
        .map(|_| ())
        .map_err(|error| error.to_string())
}

fn read_patch(root: &Value) -> Checked<MapPatch> {
    let mut patch = MapPatch::default();
    let root = expect_keys(
        root,
        "a map patch",
        &[],
        &[CAMERA, LIGHTS, WARPS, NPCS, OBJECTS, COLLISION],
    )?;
    if let Some(camera) = root.get(CAMERA) {
        let camera = expect_keys(
            camera,
            "the camera of a patch",
            &[],
            &["viewpoint", "reference", "projection"],
        )?;
        patch.camera = Some(camera.clone());
    }
    patch.lights = read_changes(root, LIGHTS, INDEX)?;
    patch.warps = read_changes(root, WARPS, EXIT)?;
    patch.npcs = read_changes(root, NPCS, INDEX)?;
    if let Some(objects) = root.get(OBJECTS) {
        let objects = expect_keys(
            objects,
            "the objects of a patch",
            &[],
            &[SPRITES, INSTANCES],
        )?;
        patch.objects = true;
        patch.sprites = read_changes(objects, SPRITES, INDEX)?;
        patch.instances = read_changes(objects, INSTANCES, INDEX)?;
    }
    if let Some(collision) = root.get(COLLISION) {
        let collision = expect_keys(collision, "the collision of a patch", &[], &[GRID, CELLS])?;
        if let Some(grid) = collision.get(GRID) {
            check_grid(grid)?;
            patch.grid = Some(grid.clone());
        }
        if let Some(cells) = collision.get(CELLS) {
            patch.cells = read_cells(cells)?;
        }
    }
    Ok(patch)
}

/// The place of an exit in the data's warps, by the number at its `exit`.
fn exit_place(warps: &[Value], exit: i32) -> Option<usize> {
    warps.iter().position(|warp| {
        let number = match warp.as_mapping().and_then(|warp| warp.get(EXIT)) {
            Some(Value::Number(number)) => number.as_i64(),
            Some(Value::String(text)) => whole_number(text),
            _ => None,
        };
        number == Some(i64::from(exit))
    })
}

/// The mapping with the fields of a change put over its own, all but the key that names it.
fn put_fields(entry: &mut Value, fields: &Mapping, key: &str) {
    if !entry.is_mapping() {
        *entry = Value::Mapping(Mapping::new());
    }
    let Value::Mapping(entry) = entry else {
        return;
    };
    for (name, value) in fields {
        if name.as_str() != Some(key) {
            entry.insert(name.clone(), value.clone());
        }
    }
}

/// Applies the changes of one section to the list of that name in `parent`.
fn apply_changes(
    parent: &mut Mapping,
    section: &str,
    changes: &[EntryChange],
    key: &str,
) -> Checked<()> {
    let Some(Value::Sequence(items)) = parent.get_mut(section) else {
        return Ok(());
    };
    for change in changes {
        let place = if key == EXIT {
            exit_place(items, change.number)
        } else {
            usize::try_from(change.number)
                .ok()
                .filter(|&place| place < items.len())
        };
        let Some(place) = place else {
            return Err(format!(
                "the map's data has no '{section}' with '{key}' {}",
                change.number
            ));
        };
        put_fields(&mut items[place], &change.fields, key);
    }
    Ok(())
}

/// Puts a patch over the tree of a map's data, which the data's reader reads afterwards.
fn apply_patch(patch: &MapPatch, data: &mut Mapping) -> Checked<()> {
    if let Some(fields) = &patch.camera
        && let Some(camera) = data.get_mut(CAMERA)
    {
        put_fields(camera, fields, "");
    }
    apply_changes(data, LIGHTS, &patch.lights, INDEX)?;
    apply_changes(data, WARPS, &patch.warps, EXIT)?;
    apply_changes(data, NPCS, &patch.npcs, INDEX)?;
    if patch.objects {
        let Some(Value::Mapping(objects)) = data.get_mut(OBJECTS) else {
            return Err("the map's data has no objects to change".to_owned());
        };
        apply_changes(objects, SPRITES, &patch.sprites, INDEX)?;
        apply_changes(objects, INSTANCES, &patch.instances, INDEX)?;
    }
    if let Some(grid) = &patch.grid {
        data.insert(Value::from(COLLISION), grid.clone());
    }
    Ok(())
}

/// Checks a patch on its own: its sections, what each change names (an index, an exit, a rectangle
/// inside the grid) and a whole collision grid. The values of the fields it replaces are checked
/// once applied, by the reader of the data (checkMapPatch).
pub fn check_map_patch(yaml: &str) -> Result<()> {
    let invalid = |message: String| Error::Invalid(format!("{PATCH_CONTEXT}: {message}"));
    let tree: Value = serde_yaml_ng::from_str(yaml).map_err(|error| invalid(error.to_string()))?;
    read_patch(&tree).map(|_| ()).map_err(invalid)
}

/// Parses a map's data and applies the patches to it, bottom to top (patchMapDocument). An index
/// or an exit that the data does not have, or a value that the data's reader refuses once applied,
/// is an error that names the patch's file. The result's `patches` lists the packs, and its
/// `patched_exits` the exits they changed.
pub fn patch_map_document(id: &str, data: &str, patches: &[MapPatchText]) -> Result<MapDocument> {
    let invalid = |message: String| Error::Invalid(format!("map '{id}': {message}"));
    let mut tree: Value =
        serde_yaml_ng::from_str(data).map_err(|error| invalid(format!("map data: {error}")))?;
    // The data on its own first, so that a problem of the data is never blamed on a patch.
    let mut document =
        MapDocument::from_tree(id, &tree).map_err(|error| invalid(format!("map data: {error}")))?;
    let mut cells: Vec<CellChange> = Vec::new();
    let mut patched_exits: Vec<ExitPatch> = Vec::new();
    for text in patches {
        let failed = |message: String| invalid(format!("patch {}: {message}", text.file));
        let patch_tree: Value =
            serde_yaml_ng::from_str(&text.text).map_err(|error| failed(error.to_string()))?;
        let patch = read_patch(&patch_tree)
            .and_then(|patch| {
                let Value::Mapping(data) = &mut tree else {
                    return Err("the map's data is not a mapping".to_owned());
                };
                apply_patch(&patch, data).map(|()| patch)
            })
            .map_err(|message| failed(format!("{PATCH_CONTEXT}: {message}")))?;
        // Read after each patch: the values it gave are checked, and a problem is its own.
        document = MapDocument::from_tree(id, &tree)
            .map_err(|error| failed(format!("map data: {error}")))?;
        if patch.grid.is_some() {
            cells.clear();
        }
        cells.extend(&patch.cells);
        for change in &patch.warps {
            patched_exits.retain(|known| known.exit != change.number);
            patched_exits.push(ExitPatch {
                exit: change.number,
                pack: text.pack.clone(),
                base_rights: text.base_rights,
            });
        }
    }
    for change in &cells {
        for row in change.row..change.row + change.height {
            for column in change.column..change.column + change.width {
                document.collision.set(Cell::new(column, row), change.value);
            }
        }
    }
    patched_exits.sort_by_key(|patch| patch.exit);
    document.patched_exits = patched_exits;
    document.patches = patches.iter().map(|text| text.pack.clone()).collect();
    Ok(document)
}

/// The packs whose patch applies to a map, with the patch's file: `map/<id>/patch` of the pack
/// whose data wins and of every pack above it, bottom to top. A patch below that pack was written
/// for data that no longer wins, so it is left out. None when the map has no data
/// (mapPatchSources).
pub fn map_patch_sources<'a>(vfs: &'a Vfs, map_id: &str) -> Vec<(&'a MountedPack, &'a Path)> {
    let (Ok(data_id), Ok(patch_id)) = (map_data_id(map_id), map_patch_id(map_id)) else {
        return Vec::new();
    };
    let Some((data_pack, _)) = vfs.resolve(&data_id) else {
        return Vec::new();
    };
    let place_of = |pack: &MountedPack| {
        vfs.packs()
            .iter()
            .position(|mounted| mounted.id() == pack.id())
    };
    let floor = place_of(data_pack);
    vfs.sources(&patch_id)
        .into_iter()
        .filter(|pack| place_of(pack) >= floor)
        .filter_map(|pack| Some((pack, pack.file(&patch_id)?)))
        .collect()
}

/// Reads the patches of [`map_patch_sources`].
pub fn read_map_patches(vfs: &Vfs, map_id: &str) -> Result<Vec<MapPatchText>> {
    map_patch_sources(vfs, map_id)
        .into_iter()
        .map(|(pack, file)| {
            let text = std::fs::read_to_string(file)
                .map_err(|error| Error::Invalid(format!("{}: {error}", file.display())))?;
            Ok(MapPatchText {
                pack: pack.id().to_owned(),
                base_rights: pack.has_base_rights(),
                file: file.display().to_string(),
                text,
            })
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The engine's test map (packages/engine/assets/tests/map_documents.hpp) with neutral
    /// numbers: a blocked border, a trigger 'A' (110) at column 10 of row 20 and a trigger 'B' (51)
    /// at column 11, three lights, ten exits (exit 0 leads to exit 2 of `room08`) and no NPC.
    pub(crate) fn fixture_map(objects: &str) -> String {
        let mut rows = String::new();
        for row in 0..100 {
            let mut cells = vec!['.'; 100];
            cells[0] = '#';
            cells[99] = '#';
            if row == 0 || row == 99 {
                cells = vec!['#'; 100];
            }
            if row == 20 {
                cells[10] = 'A';
                cells[11] = 'B';
            }
            rows += &format!("  - \"{}\"\n", cells.iter().collect::<String>());
        }
        let mut warps = String::new();
        for exit in 0..10 {
            warps += &if exit == 0 {
                "- exit: 0\n  spawn:\n    x: -2400\n    y: 0\n    z: -2425\n  rotation: 3072\n  \
                 target_map: 118\n  target_map_id: \"room08\"\n  target_exit: 2\n"
                    .to_owned()
            } else {
                format!(
                    "- exit: {exit}\n  spawn:\n    x: 0\n    y: 0\n    z: 0\n  rotation: 0\n  \
                     target_map: -1\n  target_map_id: null\n  target_exit: -1\n"
                )
            };
        }
        format!(
            "camera:\n  viewpoint:\n    x: 0\n    y: -4200\n    z: -5900\n  reference:\n    x: 0\n    \
             y: 0\n    z: -250\n  projection: 1800\n\
             lights:\n- direction: {{x: 0, y: 160, z: 90}}\n  color: {{r: 170, g: 170, b: 170}}\n\
             - direction: {{x: 1, y: 1, z: 1}}\n  color: {{r: 0, g: 0, b: 0}}\n\
             - direction: {{x: 1, y: 1, z: 1}}\n  color: {{r: 0, g: 0, b: 0}}\n\
             ambient: {{r: 0, g: 0, b: 0}}\n\
             tiles:\n  width: 3\n  height: 2\n  rows:\n  - \"0 1 -1\"\n  - \"2 3 4\"\n\
             objects: {objects}\n\
             warps:\n{warps}\
             npcs: []\n\
             collision:\n  width: 100\n  height: 100\n  encoding: \"legend\"\n  legend:\n  \
             - cell: \".\"\n    value: 0\n  - cell: \"A\"\n    value: 110\n  - cell: \"B\"\n    \
             value: 51\n  - cell: \"#\"\n    value: 128\n  rows:\n{rows}"
        )
    }

    const OBJECTS: &str = "\n  sprites:\n  - {tex_x: 96, tex_y: 40, width: 16, height: 8, position: \
                           {x: 1, y: 2, z: 3}, clut: -1, transparency: 4}\n  instances:\n  - frames:\
                           \n    - {sprite: 0, time: 1}\n    x: 392\n    y: 0\n    flag: 1";

    const EXIT_ZERO_TO_ROOM: &str =
        "warps:\n- exit: 0\n  target_exit: 5\n  spawn: {x: 100, y: 0, z: -200}\n";

    fn patch_of(text: &str, pack: &str) -> MapPatchText {
        MapPatchText {
            pack: pack.to_owned(),
            base_rights: false,
            file: "mods/patch.yaml".to_owned(),
            text: text.to_owned(),
        }
    }

    fn patched(map: &str, patches: &[MapPatchText]) -> Result<MapDocument> {
        patch_map_document("testa01", map, patches)
    }

    fn vec3(x: i32, y: i32, z: i32) -> crate::document::RawVec3 {
        crate::document::RawVec3 { x, y, z }
    }

    #[test]
    fn changes_one_exit_field_by_field_and_leaves_the_others() {
        let map = patched(
            &fixture_map("null"),
            &[patch_of(EXIT_ZERO_TO_ROOM, "mod_a")],
        )
        .unwrap();
        let exit = map.warp(0).unwrap();
        assert_eq!(exit.target_exit, 5);
        assert_eq!(exit.spawn, vec3(100, 0, -200));
        assert_eq!(exit.rotation, 3072);
        assert_eq!(exit.target_map_id.as_deref(), Some("room08"));
        let patch = map.exit_patch(0).unwrap();
        assert_eq!(patch.pack, "mod_a");
        assert!(!patch.base_rights);
        assert!(map.exit_patch(1).is_none());
        assert_eq!(map.warps.len(), 10);
        assert_eq!(map.patches, ["mod_a"]);
    }

    #[test]
    fn patches_apply_bottom_to_top_each_over_the_one_below() {
        let map = patched(
            &fixture_map("null"),
            &[
                patch_of(
                    "camera: {projection: 1900}\nlights:\n- {index: 1, color: {r: 9, g: 8, b: 7}}\n\
                     warps:\n- {exit: 2, target_exit: 1}\n",
                    "mod_a",
                ),
                patch_of(
                    "warps:\n- {exit: 2, target_exit: 4, rotation: 1024}\n",
                    "mod_b",
                ),
            ],
        )
        .unwrap();
        assert_eq!(map.camera.projection, 1900);
        assert_eq!(map.camera.viewpoint, vec3(0, -4200, -5900));
        assert_eq!(map.lights[1].color.r, 9);
        assert_eq!(map.lights[1].direction, vec3(1, 1, 1));
        assert_eq!(map.warp(2).unwrap().target_exit, 4);
        assert_eq!(map.warp(2).unwrap().rotation, 1024);
        assert_eq!(map.exit_patch(2).unwrap().pack, "mod_b");
        assert_eq!(map.patched_exits.len(), 1);
        assert_eq!(map.patches, ["mod_a", "mod_b"]);
    }

    fn empty_grid() -> String {
        let rows: String = (0..100)
            .map(|_| format!("    - \"{}\"\n", ".".repeat(100)))
            .collect();
        format!(
            "collision:\n  grid:\n    width: 100\n    height: 100\n    encoding: legend\n    \
             legend:\n    - {{cell: \".\", value: 0}}\n    rows:\n{rows}"
        )
    }

    #[test]
    fn rectangles_set_every_cell_they_cover() {
        let map = patched(
            &fixture_map("null"),
            &[patch_of(
                "collision:\n  cells:\n  - {column: 40, row: 50, width: 3, height: 2, value: 128}\n\
                 \x20 - {column: 41, row: 51, value: 0}\n",
                "mod_a",
            )],
        )
        .unwrap();
        let value = |column, row| map.collision.value(Cell::new(column, row));
        assert_eq!(value(40, 50), 128);
        assert_eq!(value(42, 51), 128);
        assert_eq!(value(41, 51), 0);
        assert_eq!(value(43, 50), 0);
        assert_eq!(value(10, 20), 110);
    }

    #[test]
    fn a_higher_grid_replaces_the_rectangles_below_it_and_its_own_apply_over_it() {
        let map = patched(
            &fixture_map("null"),
            &[
                patch_of(
                    "collision:\n  cells:\n  - {column: 40, row: 50, value: 128}\n",
                    "mod_a",
                ),
                patch_of(
                    &(empty_grid() + "  cells:\n  - {column: 1, row: 2, value: 51}\n"),
                    "mod_b",
                ),
            ],
        )
        .unwrap();
        let value = |column, row| map.collision.value(Cell::new(column, row));
        assert_eq!(value(40, 50), 0);
        assert_eq!(value(0, 0), 0);
        assert_eq!(value(10, 20), 0);
        assert_eq!(value(1, 2), 51);
    }

    #[test]
    fn changes_object_sprites_and_instances_by_place() {
        let patches = [patch_of(
            "objects:\n  sprites:\n  - {index: 0, clut: 3}\n  instances:\n  - {index: 0, x: 10, flag: 0}\n",
            "mod_a",
        )];
        let map = patched(&fixture_map(OBJECTS), &patches).unwrap();
        let objects = map.objects.as_ref().expect("the map has objects");
        assert_eq!(objects.sprites[0].clut, 3);
        assert_eq!(objects.sprites[0].tex_x, 96);
        assert_eq!(objects.instances[0].x, 10);
        assert_eq!(objects.instances[0].flag, 0);
        assert_eq!(objects.instances[0].frames.len(), 1);

        let none = patched(&fixture_map("null"), &patches)
            .unwrap_err()
            .to_string();
        assert!(none.contains("no objects"), "{none}");
    }

    #[test]
    fn naming_what_the_data_does_not_have_is_an_error_naming_the_file() {
        let fails = |text: &str, expected: &str| {
            let error = patched(&fixture_map("null"), &[patch_of(text, "mod_a")])
                .unwrap_err()
                .to_string();
            assert!(
                error.starts_with("map 'testa01': patch mods/patch.yaml: "),
                "{error}"
            );
            assert!(error.contains(expected), "{error} lacks {expected}");
        };
        fails(
            "warps:\n- {exit: 12, target_exit: 0}\n",
            "no 'warps' with 'exit' 12",
        );
        fails(
            "npcs:\n- {index: 0, script_id: 3}\n",
            "no 'npcs' with 'index' 0",
        );
        fails(
            "lights:\n- {index: 3, color: {r: 1, g: 1, b: 1}}\n",
            "no 'lights' with 'index' 3",
        );
        fails(
            "warps:\n- {exit: 1, target_exit: 0}\n- {exit: 1, rotation: 0}\n",
            "changed twice",
        );
        fails("warps:\n- {target_exit: 0}\n", "with its 'exit'");
        fails("warps:\n- {exit: 1, rotation: north}\n", "north");
        fails("warps:\n- {exit: 1, sideways: 1}\n", "sideways");
        fails("lights:\n- {index: 0, glow: 1}\n", "glow");
        fails(
            "collision:\n  cells:\n  - {column: 98, row: 0, width: 3, value: 1}\n",
            "leave the",
        );
        fails("music: 3\n", "music");
    }

    #[test]
    fn a_patch_is_checked_on_its_own_for_what_it_names() {
        assert!(check_map_patch("warps:\n- {exit: 12, target_exit: 0}\n").is_ok());
        assert!(check_map_patch("{}").is_ok());
        assert!(check_map_patch("warps:\n- {exit: -1}\n").is_err());
        assert!(check_map_patch("collision:\n  cells:\n  - {column: 0, row: 0}\n").is_err());
        assert!(check_map_patch("collision:\n  grid: {width: 100}\n").is_err());
        assert!(check_map_patch("camera: {zoom: 2}\n").is_err());
        assert!(check_map_patch("- 1\n").is_err());
        assert!(
            check_map_patch("collision:\n  cells:\n  - {column: 0, row: 0, width: 0, value: 1}\n")
                .unwrap_err()
                .to_string()
                .contains("at least 1 x 1")
        );
    }

    #[test]
    fn a_patch_id_sits_next_to_the_data() {
        assert_eq!(
            map_patch_id("testa01").unwrap().as_str(),
            "map/testa01/patch"
        );
        assert_eq!(
            map_patch_id("example_mod:meadow").unwrap().as_str(),
            "map/example_mod/meadow/patch"
        );
    }

    /// Packs in a temporary folder, each a pack.yaml and its files.
    struct Packs {
        dir: tempfile::TempDir,
    }

    impl Packs {
        fn new() -> Self {
            Self {
                dir: tempfile::tempdir().unwrap(),
            }
        }

        fn file(&self, path: &str, text: &str) {
            let path = self.dir.path().join(path);
            std::fs::create_dir_all(path.parent().unwrap()).unwrap();
            std::fs::write(path, text).unwrap();
        }

        fn pack(&self, id: &str, layer: &str, priority: i32) {
            self.file(
                &format!("{id}/pack.yaml"),
                &format!("id: {id}\nversion: 1.0.0\nlayer: {layer}\npriority: {priority}\n"),
            );
        }

        fn mount(&self, ids: &[&str]) -> Vfs {
            let locations: Vec<_> = ids
                .iter()
                .map(|id| dwe_pack::PackLocation {
                    root: self.dir.path().join(id),
                    origin: dwe_pack::PackOrigin::Engine,
                })
                .collect();
            Vfs::mount(&locations).unwrap()
        }
    }

    fn stacked() -> Packs {
        let packs = Packs::new();
        packs.pack("base", "base", 0);
        packs.file("base/map/testa01/data.yaml", &fixture_map("null"));
        packs.pack("low", "mods", 0);
        packs.file(
            "low/map/testa01/patch.yaml",
            "warps:\n- {exit: 0, target_exit: 7}\n",
        );
        packs.pack("high", "mods", 2);
        packs.file(
            "high/map/testa01/patch.yaml",
            "camera: {projection: 1234}\n",
        );
        packs
    }

    #[test]
    fn load_map_applies_every_patch_over_the_base_bottom_to_top() {
        let packs = stacked();
        let vfs = packs.mount(&["high", "base", "low"]);
        let map = crate::load_map(&vfs, "testa01").unwrap();
        assert_eq!(map.patches, ["low", "high"]);
        assert_eq!(map.warp(0).unwrap().target_exit, 7);
        assert_eq!(map.camera.projection, 1234);
    }

    #[test]
    fn a_packs_whole_data_drops_the_patches_below_it() {
        let packs = stacked();
        packs.pack("mid", "mods", 1);
        packs.file("mid/map/testa01/data.yaml", &fixture_map("null"));
        let vfs = packs.mount(&["base", "low", "mid", "high"]);
        let map = crate::load_map(&vfs, "testa01").unwrap();
        assert_eq!(map.patches, ["high"]);
        assert_eq!(map.warp(0).unwrap().target_exit, 2);
        assert_eq!(map.camera.projection, 1234);
    }

    #[test]
    fn a_patch_that_does_not_apply_stops_the_map_from_loading() {
        let packs = stacked();
        packs.file(
            "high/map/testa01/patch.yaml",
            "npcs:\n- {index: 4, script_id: 1}\n",
        );
        let vfs = packs.mount(&["base", "high"]);
        let error = crate::load_map(&vfs, "testa01").unwrap_err().to_string();
        assert!(error.contains("testa01"), "{error}");
        assert!(error.contains("high"), "{error}");
        // A map without data has no patches that apply.
        assert!(map_patch_sources(&vfs, "testa02").is_empty());
    }
}
