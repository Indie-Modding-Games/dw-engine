//! What the editor writes, checked against the engine: each case of the engine's
//! `packages/engine/assets/tests/data/written_maps` edits
//! `base.yaml`, writes the edit as a patch (`<case>/patch.yaml`) and the patched map whole
//! (`<case>/expected.yaml`). The engine's `map_written_tests.cpp` applies the same patch to the
//! same base with its own reader and checks that it gets the map of `expected.yaml`, so both
//! sides read and patch maps the same way.
//!
//! The files are the editor's output and are compared exactly; `DWE_MAPPER_BLESS=1` writes them
//! again after a deliberate change.

use std::path::{Path, PathBuf};

use dwe_map::diff::{MOST_RECTANGLES, diff_patch, patch_text};
use dwe_map::document::RawVec3;
use dwe_map::{Cell, MapDocument, MapPatchText, patch_map_document};

const MAP_ID: &str = "testa01";

fn folder() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../engine/assets/tests/data/written_maps")
}

fn base_text() -> String {
    std::fs::read_to_string(folder().join("base.yaml")).expect("base.yaml")
}

/// Compares a file with what the editor writes now, or writes it when blessing.
fn check_file(path: &Path, text: &str) {
    if std::env::var_os("DWE_MAPPER_BLESS").is_some() {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, text).unwrap();
        return;
    }
    let known =
        std::fs::read_to_string(path).unwrap_or_else(|error| panic!("{}: {error}", path.display()));
    assert_eq!(known, text, "{} changed", path.display());
}

/// Writes a case: the patch of the edit, then the base patched by it, which must be the edit.
fn check_case(name: &str, edit: impl FnOnce(&mut MapDocument)) {
    let base = MapDocument::parse(MAP_ID, &base_text()).expect("the base parses");
    let mut edited = base.clone();
    edit(&mut edited);
    let patch = diff_patch(&base, &edited).expect("the edit makes a patch");
    let text = patch_text(&patch);
    let patched = patch_map_document(
        MAP_ID,
        &base_text(),
        &[MapPatchText {
            pack: "written".to_owned(),
            base_rights: false,
            file: format!("{name}/patch.yaml"),
            text: text.clone(),
        }],
    )
    .expect("the patch applies");
    assert_eq!(
        patched.to_tree(),
        edited.to_tree(),
        "patching gives the edit"
    );
    check_file(&folder().join(name).join("patch.yaml"), &text);
    check_file(
        &folder().join(name).join("expected.yaml"),
        &patched.to_yaml(),
    );
}

#[test]
fn the_base_writes_back_as_it_is() {
    let text = base_text();
    let base = MapDocument::parse(MAP_ID, &text).unwrap();
    assert_eq!(base.to_yaml(), text);
    assert!(diff_patch(&base, &base.clone()).is_none());
}

#[test]
fn a_small_edit_writes_cells_exits_and_npc_fields() {
    check_case("cells", |map| {
        // A 3 x 3 wall, and exit 1's trigger painted on two more cells.
        for row in 39..=41 {
            for column in 29..=31 {
                map.collision.set(Cell::new(column, row), 0x80);
            }
        }
        map.collision.set(Cell::new(70, 70), 111);
        map.collision.set(Cell::new(71, 70), 111);
        let exit = map.warps.iter_mut().find(|warp| warp.exit == 0).unwrap();
        exit.target_exit = 3;
        exit.spawn = RawVec3 {
            x: -2300,
            y: 0,
            z: -2200,
        };
        exit.rotation = 2048;
        let exit = map.warps.iter_mut().find(|warp| warp.exit == 1).unwrap();
        exit.target_map_id = Some("room08".to_owned());
        exit.target_map = 118;
        // NPC 1 moves and walks one more point; NPC 0 changes a stat the tools keep as is.
        let npc = &mut map.npcs[1];
        npc.position.x = 700;
        npc.ai_sections[2] = 2;
        npc.waypoints.push(RawVec3 {
            x: 300,
            y: 0,
            z: -1500,
        });
        map.npcs[0]
            .other
            .insert("hp".into(), serde_yaml_ng::Value::from(750));
    });
}

#[test]
fn a_large_edit_writes_the_whole_grid() {
    check_case("grid", |map| {
        // A checkerboard: no rectangle covers more than one cell.
        let mut changed = 0;
        for row in 30..50 {
            for column in 30..50 {
                if (row + column) % 2 == 0 {
                    map.collision.set(Cell::new(column, row), 51);
                    changed += 1;
                }
            }
        }
        assert!(changed > MOST_RECTANGLES);
        map.npcs[0].rotation.y = 1024;
    });
}
