//! The editor's model driven headless over a synthetic content folder (neutral ids and numbers).

use std::path::Path;

use dwe_pack::GameFolders;
use glam::Vec2;
use mapper_core::{Action, ExitTarget, GroupBy, Layer, Mapper, Message};

/// A map document with one exit's trigger cells (value 110) at row 50, columns 50-51, a step
/// trigger (51) next to them and walls on row 0.
fn map_yaml(target: &str, target_index: i32) -> String {
    let mut rows = vec![".".repeat(100); 100];
    rows[0] = "#".repeat(100);
    rows[50] = format!("{}DDA{}", ".".repeat(50), ".".repeat(47));
    let rows: String = rows.iter().map(|row| format!("  - \"{row}\"\n")).collect();
    format!(
        "camera:\n  viewpoint: {{x: 0, y: -3000, z: -4000}}\n  reference: {{x: 0, y: 0, z: 0}}\n  projection: 1000\n\
         tiles:\n  width: 3\n  height: 2\n  rows: [\"0 1 2\", \"3 4 5\"]\n\
         warps:\n\
         - exit: 0\n  spawn: {{x: 0, y: 0, z: -300}}\n  rotation: 0\n  target_map: {target_index}\n  target_map_id: \"{target}\"\n  target_exit: 1\n\
         - exit: 1\n  spawn: {{x: 400, y: 0, z: 200}}\n  rotation: 1024\n  target_map: -1\n  target_map_id: null\n  target_exit: -1\n\
         npcs:\n- type_id: 3\n  follow_mode: 1\n  position: {{x: 100, y: 0, z: 100}}\n  rotation: {{x: 0, y: 0, z: 0}}\n  tracking_range: 300\n  script_id: 2\n  waypoints: []\n\
         collision:\n  width: 100\n  height: 100\n  encoding: \"legend\"\n  legend:\n\
         \x20 - {{cell: \".\", value: 0}}\n  - {{cell: \"A\", value: 51}}\n  - {{cell: \"D\", value: 110}}\n  - {{cell: \"#\", value: 128}}\n\
         \x20 rows:\n{rows}"
    )
}

fn write(path: &Path, text: &str) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, text).unwrap();
}

fn content(root: &Path) -> GameFolders {
    let base = root.join("content/base");
    write(
        &base.join("pack.yaml"),
        "id: base\nversion: 1.0.0\nlayer: base\n",
    );
    write(
        &base.join("data/maps.yaml"),
        "maps:\n\
         - {id: area_a, index: 0, filename: AREA_A, doors: 0, toilet: 0, loading_name: 0}\n\
         - {id: area_b, index: 1, filename: AREA_B, doors: null, toilet: null, loading_name: 1}\n\
         - {id: area_c, index: 2, filename: AREA_C, doors: null, toilet: null, loading_name: 1}\n\
         doors:\n- index: 0\n  doors:\n  - {model_id: 4, position: {x: 200, y: 0, z: 0}, rotation: 0}\n\
         toilets:\n- {index: 0, x1: -200, z1: 0, x2: -300, z2: 100}\n\
         map_names: [' Plains ', ' Woods ']\n",
    );
    write(&base.join("map/area_a/data.yaml"), &map_yaml("area_b", 1));
    write(&base.join("map/area_a/background_day.png"), "day");
    write(&base.join("map/area_a/background_night.png"), "night");
    write(&base.join("map/area_b/data.yaml"), &map_yaml("area_a", 0));
    // area_c has no data: the browser leaves it out.
    GameFolders {
        content: root.join("content"),
        ..GameFolders::default()
    }
}

fn opened(root: &Path) -> Mapper {
    let mut mapper = Mapper::new();
    mapper.apply(Action::OpenContent(content(root)));
    assert!(mapper.messages.is_empty(), "{:?}", mapper.messages);
    mapper.apply(Action::SetCanvasSize(Vec2::new(1000.0, 700.0)));
    mapper
}

#[test]
fn browses_the_maps_that_have_data() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = opened(dir.path());
    let titles: Vec<_> = mapper
        .groups()
        .iter()
        .map(|group| (group.title.clone(), group.maps.len()))
        .collect();
    assert_eq!(titles, vec![("Plains".into(), 1), ("Woods".into(), 1)]);
    mapper.apply(Action::SetGroupBy(GroupBy::Prefix));
    mapper.apply(Action::SetFilter("_b".into()));
    let groups = mapper.groups();
    assert_eq!(groups.len(), 1);
    assert_eq!(groups[0].maps[0].id, "area_b");
}

#[test]
fn opens_a_map_with_its_layers_and_backgrounds() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = opened(dir.path());
    mapper.apply(Action::OpenMap("area_a".into()));
    let map = mapper.map.as_ref().expect("the map opens");
    assert_eq!(map.background_size(), Vec2::new(384.0, 256.0));
    // Walls on row 0, the exit's two cells and the step cell.
    assert_eq!(map.overlay.cells.len(), 103);
    let exit = &map.overlay.exits[0];
    assert_eq!(exit.cell_count, 2);
    assert_eq!(
        exit.target,
        ExitTarget::Map {
            id: "area_b".into(),
            exit: 1
        }
    );
    assert!(exit.arrival.is_some());
    assert!(!map.overlay.exits[1].is_used());
    assert_eq!(map.overlay.npcs.len(), 1);
    assert_eq!(map.overlay.doors.len(), 1);
    assert!(map.overlay.toilet.is_some());
    assert!(
        mapper
            .background_file()
            .unwrap()
            .ends_with("background_day.png")
    );
    let revision = mapper.revision;
    mapper.apply(Action::SetVariant("night".into()));
    assert!(mapper.revision > revision);
    assert!(
        mapper
            .background_file()
            .unwrap()
            .ends_with("background_night.png")
    );
    // A variant the map lacks falls back to its first background.
    mapper.apply(Action::SetVariant("dusk".into()));
    assert!(
        mapper
            .background_file()
            .unwrap()
            .ends_with("background_day.png")
    );
    mapper.apply(Action::SetLayer(Layer::Walls, false));
    assert!(!mapper.layers.shows(Layer::Walls));
}

#[test]
fn hovering_reads_the_cell_and_clicking_an_exit_follows_it() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = opened(dir.path());
    mapper.apply(Action::OpenMap("area_a".into()));
    let label = mapper.map.as_ref().unwrap().overlay.exits[0].label.unwrap();
    let canvas = mapper.view.to_canvas(label);
    mapper.apply(Action::Hover(Some(canvas)));
    let hover = mapper.hover.expect("the cursor is over the ground");
    assert_eq!(hover.value, 110);
    assert_eq!(hover.cell.row, 50);
    assert!((50..=51).contains(&hover.cell.column));

    mapper.apply(Action::Click(canvas));
    assert_eq!(mapper.map.as_ref().unwrap().document.id, "area_b");
    // The view centres on where the player arrives: exit 1's spawn.
    let arrival = mapper.map.as_ref().unwrap().overlay.exits[1]
        .arrival
        .unwrap();
    assert!((mapper.view.center - arrival.from).length() < 1e-3);
    assert_eq!(mapper.history, vec!["area_a".to_owned()]);
    mapper.apply(Action::Back);
    assert_eq!(mapper.map.as_ref().unwrap().document.id, "area_a");
    assert!(mapper.history.is_empty());
}

#[test]
fn a_missing_map_or_content_is_a_message_not_a_crash() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = opened(dir.path());
    mapper.apply(Action::OpenMap("area_c".into()));
    assert!(mapper.map.is_none());
    assert!(matches!(mapper.messages.last(), Some(Message::Error(_))));
    let mut empty = Mapper::new();
    empty.apply(Action::OpenContent(GameFolders {
        content: dir.path().join("nowhere"),
        ..GameFolders::default()
    }));
    assert!(empty.content.is_none());
    assert_eq!(empty.messages.len(), 1);
}

/// The content of `content()` with a mods folder: one mod that patches area_a's exit 0 and
/// collision, and a `field/triggers` that moves the exits band.
fn patched_content(root: &Path) -> GameFolders {
    let mut folders = content(root);
    let base = root.join("content/base");
    write(
        &base.join("field/triggers.yaml"),
        "step: {first: 51, last: 79}\nconfirm: {first: 80, last: 109}\n\
         exits: {first: 20, last: 29}\ntoilet: 120\nblocked_bit: 128\n",
    );
    write(
        &base.join("story/names.yaml"),
        "triggers:\n  5: gate_open\nvariables:\n  2: counter\n",
    );
    let mod_a = root.join("mods/mod_a");
    write(
        &mod_a.join("pack.yaml"),
        "id: mod_a\nversion: 1.0.0\nlayer: mods\n",
    );
    write(
        &mod_a.join("map/area_a/patch.yaml"),
        "warps:\n- {exit: 1, target_map: -1, target_map_id: area_b, target_exit: 0}\n\
         collision:\n  cells:\n  - {column: 53, row: 50, width: 2, value: 21}\n",
    );
    folders.mods = vec![root.join("mods")];
    folders
}

#[test]
fn opens_a_patched_map_with_the_bands_of_the_content() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = Mapper::new();
    mapper.apply(Action::OpenContent(patched_content(dir.path())));
    assert!(mapper.messages.is_empty(), "{:?}", mapper.messages);
    assert_eq!(mapper.bands.exit_trigger(1), Some(21));
    assert_eq!(mapper.story_names.trigger(5), Some("gate_open"));
    mapper.apply(Action::SetCanvasSize(Vec2::new(1000.0, 700.0)));
    mapper.apply(Action::OpenMap("area_a".into()));
    let map = mapper.map.as_ref().expect("the map opens");
    assert_eq!(map.document.patches, ["mod_a"]);
    assert_eq!(map.document.exit_patch(1).unwrap().pack, "mod_a");
    // Exit 1 now leads to area_b through the two cells the patch set to 21, exit 1 of the band
    // that starts at 20; the data's 110 cells are no exit's any more.
    let exit = &map.overlay.exits[1];
    assert_eq!(exit.cell_count, 2);
    assert_eq!(
        exit.target,
        ExitTarget::Map {
            id: "area_b".into(),
            exit: 0
        }
    );
    assert_eq!(map.overlay.exits[0].cell_count, 0);
}

#[test]
fn broken_bands_fall_back_to_the_original_with_a_message() {
    let dir = tempfile::tempdir().unwrap();
    let folders = content(dir.path());
    write(
        &dir.path().join("content/base/field/triggers.yaml"),
        "step: {first: 1, last: 2}\n",
    );
    let mut mapper = Mapper::new();
    mapper.apply(Action::OpenContent(folders));
    assert!(mapper.content.is_some());
    assert_eq!(mapper.bands, dwe_map::TriggerBands::default());
    assert!(matches!(mapper.messages.last(), Some(Message::Error(_))));
}

// Editing and saving.

use dwe_map::save::{Problem, SaveMode};
use dwe_map::world;
use mapper_core::{Modifiers, PendingChoice, Selection, Tool};

/// The canvas point over a cell's centre.
fn canvas_of_cell(mapper: &Mapper, column: i32, row: i32) -> Vec2 {
    let map = mapper.map.as_ref().unwrap();
    let world = world::cell_center(dwe_map::Cell::new(column, row));
    let pixel = map.camera.project_to_background(world).unwrap().pixel;
    mapper.view.to_canvas(pixel)
}

/// The canvas point over an original position.
fn canvas_of(mapper: &Mapper, x: i32, y: i32, z: i32) -> Vec2 {
    let map = mapper.map.as_ref().unwrap();
    let world = world::to_world(glam::IVec3::new(x, y, z));
    let pixel = map.camera.project_to_background(world).unwrap().pixel;
    mapper.view.to_canvas(pixel)
}

fn press(mapper: &mut Mapper, at: Vec2) {
    mapper.apply(Action::PointerDown {
        at,
        modifiers: Modifiers::default(),
    });
}

fn value(mapper: &Mapper, column: i32, row: i32) -> u8 {
    mapper
        .map
        .as_ref()
        .unwrap()
        .document
        .collision
        .value(dwe_map::Cell::new(column, row))
}

fn editing(root: &Path) -> Mapper {
    let mut mapper = opened(root);
    mapper.apply(Action::OpenMap("area_a".into()));
    mapper.apply(Action::SetLayer(Layer::Grid, false));
    mapper
}

#[test]
fn a_brush_stroke_is_one_undo_step() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = editing(dir.path());
    mapper.apply(Action::SetTool(Tool::Brush));
    mapper.apply(Action::SetPaintValue(128));
    mapper.apply(Action::SetBrushSize(3));
    let at = canvas_of_cell(&mapper, 20, 50);
    press(&mut mapper, at);
    let to = canvas_of_cell(&mapper, 26, 50);
    mapper.apply(Action::PointerMove(to));
    mapper.apply(Action::PointerUp);
    for column in 19..=27 {
        assert_eq!(value(&mapper, column, 49), 128, "column {column}");
        assert_eq!(value(&mapper, column, 51), 128, "column {column}");
    }
    assert!(mapper.is_dirty());
    let walls = mapper.map.as_ref().unwrap().overlay.cells.len();
    assert_eq!(walls, 103 + 27);
    mapper.apply(Action::Undo);
    assert_eq!(value(&mapper, 22, 50), 0);
    assert!(!mapper.is_dirty());
    assert_eq!(mapper.map.as_ref().unwrap().overlay.cells.len(), 103);
    mapper.apply(Action::Redo);
    assert_eq!(value(&mapper, 22, 50), 128);
}

#[test]
fn rectangle_fill_and_eyedropper() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = editing(dir.path());
    // The eyedropper takes the exit's trigger.
    mapper.apply(Action::SetTool(Tool::Eyedropper));
    let at = canvas_of_cell(&mapper, 50, 50);
    press(&mut mapper, at);
    assert_eq!(mapper.paint_value, 110);
    mapper.apply(Action::SetTool(Tool::Rectangle));
    mapper.apply(Action::SetPaintValue(51));
    let at = canvas_of_cell(&mapper, 10, 60);
    press(&mut mapper, at);
    mapper.apply(Action::PointerMove(canvas_of_cell(&mapper, 12, 61)));
    assert_eq!(
        value(&mapper, 10, 60),
        0,
        "nothing is painted until the release"
    );
    mapper.apply(Action::PointerUp);
    assert_eq!(value(&mapper, 12, 61), 51);
    assert_eq!(value(&mapper, 13, 61), 0);
    // A fill of the free floor stops at the walls of row 0 and the triggers.
    mapper.apply(Action::SetTool(Tool::Fill));
    mapper.apply(Action::SetPaintValue(0x81));
    let at = canvas_of_cell(&mapper, 30, 30);
    press(&mut mapper, at);
    mapper.apply(Action::PointerUp);
    assert_eq!(value(&mapper, 90, 90), 0x81);
    assert_eq!(value(&mapper, 50, 50), 110);
    assert_eq!(value(&mapper, 10, 60), 51);
    // Alt with a painting tool also takes the value.
    let at = canvas_of_cell(&mapper, 51, 50);
    mapper.apply(Action::PointerDown {
        at,
        modifiers: Modifiers {
            alt: true,
            ..Modifiers::default()
        },
    });
    assert_eq!(mapper.paint_value, 110);
    mapper.apply(Action::Undo);
    mapper.apply(Action::Undo);
    assert!(!mapper.is_dirty());
}

#[test]
fn dragging_an_npc_moves_it_and_shift_turns_it() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = editing(dir.path());
    mapper.apply(Action::SetTool(Tool::Select));
    let at = canvas_of(&mapper, 100, 0, 100);
    press(&mut mapper, at);
    assert_eq!(mapper.selection, Some(Selection::Npc(0)));
    mapper.apply(Action::PointerMove(canvas_of(&mapper, 500, 0, -700)));
    mapper.apply(Action::PointerMove(canvas_of(&mapper, 600, 0, -800)));
    mapper.apply(Action::PointerUp);
    let npc = &mapper.map.as_ref().unwrap().document.npcs[0];
    assert!((npc.position.x - 600).abs() <= 2, "{:?}", npc.position);
    assert!((npc.position.z + 800).abs() <= 2, "{:?}", npc.position);
    mapper.apply(Action::Undo);
    assert_eq!(
        mapper.map.as_ref().unwrap().document.npcs[0].position.x,
        100
    );
    assert!(!mapper.is_dirty());
    // Shift-drag towards the original's -X: rotation 1024.
    let at = canvas_of(&mapper, 100, 0, 100);
    press(&mut mapper, at);
    let at = canvas_of(&mapper, 100, 0, 100);
    mapper.apply(Action::PointerDown {
        at,
        modifiers: Modifiers {
            shift: true,
            ..Modifiers::default()
        },
    });
    mapper.apply(Action::PointerMove(canvas_of(&mapper, -900, 0, 100)));
    mapper.apply(Action::PointerUp);
    let rotation = mapper.map.as_ref().unwrap().document.npcs[0].rotation.y;
    assert!((rotation - 1024).abs() <= 8, "{rotation}");
    // Q/E turn by a fixed angle.
    mapper.apply(Action::Rotate(512));
    let rotation_after = mapper.map.as_ref().unwrap().document.npcs[0].rotation.y;
    assert_eq!(rotation_after, (rotation + 512) % 4096);
}

#[test]
fn exits_change_their_target_and_their_arrival() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = editing(dir.path());
    // A click on an exit cell selects the exit; Ctrl+click follows it.
    let at = canvas_of_cell(&mapper, 51, 50);
    press(&mut mapper, at);
    mapper.apply(Action::PointerUp);
    assert_eq!(mapper.selection, Some(Selection::Exit(0)));
    let mut warp = mapper.map.as_ref().unwrap().document.warps[1].clone();
    warp.target_map_id = Some("area_b".into());
    warp.target_map = 1;
    warp.target_exit = 0;
    mapper.apply(Action::SetWarp(warp));
    let map = mapper.map.as_ref().unwrap();
    assert_eq!(
        map.overlay.exits[1].target,
        ExitTarget::Map {
            id: "area_b".into(),
            exit: 0
        }
    );
    // Its arrival marker drags.
    let at = canvas_of(&mapper, 400, 0, 200);
    press(&mut mapper, at);
    assert_eq!(mapper.selection, Some(Selection::Exit(1)));
    mapper.apply(Action::PointerMove(canvas_of(&mapper, -300, 0, 200)));
    mapper.apply(Action::PointerUp);
    let spawn = mapper.map.as_ref().unwrap().document.warps[1].spawn;
    assert!((spawn.x + 300).abs() <= 2, "{spawn:?}");
    let ctrl = Modifiers {
        ctrl: true,
        ..Modifiers::default()
    };
    // Leaving the map with changes asks first.
    let at = canvas_of_cell(&mapper, 50, 50);
    mapper.apply(Action::PointerDown {
        at,
        modifiers: ctrl,
    });
    assert!(mapper.pending.is_some());
    assert_eq!(mapper.map.as_ref().unwrap().document.id, "area_a");
    mapper.apply(Action::Resolve(PendingChoice::Cancel));
    assert!(mapper.pending.is_none());
    assert_eq!(mapper.map.as_ref().unwrap().document.id, "area_a");
    mapper.apply(Action::OpenMap("area_b".into()));
    mapper.apply(Action::Resolve(PendingChoice::Discard));
    assert_eq!(mapper.map.as_ref().unwrap().document.id, "area_b");
    assert!(!mapper.is_dirty());
}

#[test]
fn path_points_add_move_and_go() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = editing(dir.path());
    let at = canvas_of(&mapper, -500, 0, 300);
    mapper.apply(Action::AddWaypoint {
        slot: 0,
        at: Some(at),
    });
    mapper.apply(Action::AddWaypoint { slot: 0, at: None });
    let npc = mapper.map.as_ref().unwrap().document.npcs[0].clone();
    assert_eq!(npc.ai_sections, [1, 1, -1, -1, -1, -1, -1, -1]);
    assert_eq!(npc.waypoints.len(), 2);
    assert!((npc.waypoints[0].x + 500).abs() <= 2);
    assert_eq!(npc.waypoints[1].x, 300);
    assert_eq!(
        mapper.selection,
        Some(Selection::Waypoint { slot: 0, part: 1 })
    );
    assert_eq!(mapper.map.as_ref().unwrap().overlay.npcs[0].parts, [0, 1]);
    // A point drags.
    let at = canvas_of(&mapper, 300, 0, 100);
    press(&mut mapper, at);
    assert_eq!(
        mapper.selection,
        Some(Selection::Waypoint { slot: 0, part: 1 })
    );
    mapper.apply(Action::PointerMove(canvas_of(&mapper, 300, 0, 900)));
    mapper.apply(Action::PointerUp);
    let z = mapper.map.as_ref().unwrap().document.npcs[0].waypoints[1].z;
    assert!((z - 900).abs() <= 2, "{z}");
    mapper.apply(Action::MoveWaypoint {
        slot: 0,
        part: 1,
        later: false,
    });
    let npc = &mapper.map.as_ref().unwrap().document.npcs[0];
    assert!((npc.waypoints[0].z - 900).abs() <= 2);
    assert_eq!(
        mapper.selection,
        Some(Selection::Waypoint { slot: 0, part: 0 })
    );
    mapper.apply(Action::DeleteSelection);
    let npc = &mapper.map.as_ref().unwrap().document.npcs[0];
    assert_eq!(npc.ai_sections, [1, -1, -1, -1, -1, -1, -1, -1]);
    assert_eq!(npc.waypoints.len(), 1);
    for _ in 0..5 {
        mapper.apply(Action::Undo);
    }
    assert!(!mapper.is_dirty());
}

#[test]
fn saves_a_patch_into_a_new_mod_pack_and_reads_it_back() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = editing(dir.path());
    // Only mods (and the team pack of a checkout) are targets: never the base.
    assert!(mapper.targets().is_empty());
    mapper.apply(Action::SetPaintValue(128));
    mapper.apply(Action::SetTool(Tool::Brush));
    let at = canvas_of_cell(&mapper, 30, 40);
    press(&mut mapper, at);
    mapper.apply(Action::PointerUp);
    assert!(!mapper.save(SaveMode::Patch));
    assert!(matches!(mapper.messages.last(), Some(Message::Error(_))));

    let mods = dir.path().join("mods");
    std::fs::create_dir_all(&mods).unwrap();
    mapper.apply(Action::CreateModPack {
        folder: mods.clone(),
        id: "my_maps".into(),
    });
    assert!(mapper.is_dirty(), "creating the pack keeps the edits");
    let target = mapper.current_target().expect("the new pack is the target");
    assert_eq!(target.root, mods.join("my_maps"));
    let mut warp = mapper.map.as_ref().unwrap().document.warps[0].clone();
    warp.target_exit = 0;
    mapper.apply(Action::SetWarp(warp));
    mapper.apply(Action::Save(SaveMode::Patch));
    assert!(
        mapper.save_problems.is_empty(),
        "{:?}",
        mapper.save_problems
    );
    assert!(!mapper.is_dirty());
    assert_eq!(mapper.saves, 1);
    let patch = std::fs::read_to_string(mods.join("my_maps/map/area_a/patch.yaml")).unwrap();
    assert_eq!(
        patch,
        "warps:\n- exit: 0\n  target_exit: 0\ncollision:\n  cells:\n  - column: 30\n    row: 40\n    value: 128\n"
    );
    // The content was mounted again: the map comes from the packs with the patch.
    let map = mapper.map.as_ref().unwrap();
    assert_eq!(map.loaded.patches, ["my_maps"]);
    assert_eq!(map.loaded.collision.value(dwe_map::Cell::new(30, 40)), 128);
    assert_eq!(map.loaded.to_tree(), map.document.to_tree());
    // The base is untouched.
    let base =
        std::fs::read_to_string(dir.path().join("content/base/map/area_a/data.yaml")).unwrap();
    assert_eq!(base, map_yaml("area_b", 1));

    // A dead warp is refused and nothing is written.
    mapper.apply(Action::SetPaintValue(111));
    let at = canvas_of_cell(&mapper, 60, 60);
    press(&mut mapper, at);
    mapper.apply(Action::PointerUp);
    mapper.apply(Action::Save(SaveMode::Patch));
    assert_eq!(mapper.save_problems, [Problem::DeadExit { exit: 1 }]);
    assert!(mapper.is_dirty());
    // Undoing back to the saved map and saving keeps the patch as it was.
    mapper.apply(Action::Undo);
    assert!(!mapper.is_dirty());
}

#[test]
fn the_target_is_remembered() {
    let dir = tempfile::tempdir().unwrap();
    let settings = dir.path().join("settings/settings.yaml");
    let mods = dir.path().join("mods");
    std::fs::create_dir_all(&mods).unwrap();
    let mut mapper = Mapper::with_settings(settings.clone());
    mapper.apply(Action::OpenContent(content(dir.path())));
    mapper.apply(Action::CreateModPack {
        folder: mods.clone(),
        id: "mine".into(),
    });
    assert_eq!(mapper.current_target().unwrap().id, "mine");
    mapper.apply(Action::SetReloadOnSave(false));
    let mut again = Mapper::with_settings(settings);
    assert!(!again.reload_on_save());
    let mut folders = content(dir.path());
    folders.mods.push(mods.clone());
    again.apply(Action::OpenContent(folders));
    assert_eq!(again.current_target().unwrap().root, mods.join("mine"));
}
