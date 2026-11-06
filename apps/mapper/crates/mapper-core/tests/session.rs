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
