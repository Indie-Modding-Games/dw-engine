//! The world graph over a synthetic content folder (neutral ids and numbers): its edges, its
//! checks, its layout and paths, and the maps a mod pack adds, connects, renames and deletes.

use std::collections::BTreeMap;
use std::path::Path;

use dwe_map::new_map::{Connection, NewMap, Template};
use dwe_pack::GameFolders;
use glam::Vec2;
use mapper_core::{
    Action, CheckKind, Connect, EdgeKind, Mapper, Message, Severity, WorldCheck, WorldGraph,
};

fn write(path: &Path, text: &str) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, text).unwrap();
}

/// One exit: its number, where the player arrives through it (original z), the target's index and
/// id, the target's exit, and whether it has trigger cells (on row 50, two per exit).
struct Exit {
    exit: i32,
    arrival_z: i32,
    target_map: i32,
    target_id: Option<&'static str>,
    target_exit: i32,
    cells: bool,
}

const fn exit(exit: i32, target_map: i32, target_id: &'static str, target_exit: i32) -> Exit {
    Exit {
        exit,
        arrival_z: -1050,
        target_map,
        target_id: Some(target_id),
        target_exit,
        cells: true,
    }
}

const fn unused(exit: i32) -> Exit {
    Exit {
        exit,
        arrival_z: -1050,
        target_map: -1,
        target_id: None,
        target_exit: -1,
        cells: false,
    }
}

/// A map with walls on row 0, its exits' cells on row 50 and two step cells (section 51) on row 40.
fn map_yaml(exits: &[Exit]) -> String {
    let mut rows: Vec<Vec<char>> = vec![vec!['.'; 100]; 100];
    rows[0] = vec!['#'; 100];
    rows[40][20] = 'A';
    rows[40][21] = 'A';
    let mut warps = String::new();
    for exit in exits {
        if exit.cells {
            let symbol = char::from(b'0' + exit.exit as u8);
            rows[50][10 * exit.exit as usize] = symbol;
            rows[50][10 * exit.exit as usize + 1] = symbol;
        }
        let target = exit
            .target_id
            .map_or("null".to_owned(), |id| format!("\"{id}\""));
        warps += &format!(
            "- exit: {}\n  spawn: {{x: 0, y: 0, z: {}}}\n  rotation: 0\n  target_map: {}\n  target_map_id: {target}\n  target_exit: {}\n",
            exit.exit, exit.arrival_z, exit.target_map, exit.target_exit
        );
    }
    let rows: String = rows
        .iter()
        .map(|row| format!("  - \"{}\"\n", row.iter().collect::<String>()))
        .collect();
    let legend: String = (0..10)
        .map(|exit| format!("  - {{cell: \"{exit}\", value: {}}}\n", 110 + exit))
        .collect();
    format!(
        "camera:\n  viewpoint: {{x: 0, y: -3000, z: -4000}}\n  reference: {{x: 0, y: 0, z: 0}}\n  projection: 1000\n\
         tiles:\n  width: 3\n  height: 2\n  rows: [\"0 1 2\", \"3 4 5\"]\n\
         warps:\n{warps}\
         npcs: []\n\
         collision:\n  width: 100\n  height: 100\n  encoding: \"legend\"\n  legend:\n\
         \x20 - {{cell: \".\", value: 0}}\n  - {{cell: \"A\", value: 51}}\n  - {{cell: \"#\", value: 128}}\n{legend}\
         \x20 rows:\n{rows}"
    )
}

/// MAPHEAD: entering map 0 loads script 1.
const MAP_HEAD: &str = "-- dwx lua 3: script 0, from script/dg/000
local story <const> = story
local ENDED <const> = story.ENDED

local routines = {}

-- section 0x0000
function routines.at_0010()
  story.set_script(1, 0) -- script, map
  return ENDED -- end_section
end

return {
  script = 0,
  sections = {
    [0x0000] = routines.at_0010,
  },
}
";

/// Script 1: stepping on section 51 warps to map 1, exit 0, unless trigger 12 is set.
const MAP_SCRIPT: &str = "-- dwx lua 3: script 1, from script/dg/001
local story <const> = story
local ENDED <const> = story.ENDED

local routines = {}

-- section 0x0033
function routines.at_0040()
  if story.trigger(12) then
    return ENDED -- end_section
  end
  return story.warp_to(1, 0, 255) -- map, exit, section
end

return {
  script = 1,
  sections = {
    [0x0033] = routines.at_0040,
  },
}
";

/// area_a ⇄ area_b (an exit each way and a script warp), area_a's broken exits, area_c → area_d
/// cut off from the start, and area_b's exit 0 arriving in a wall.
fn content(root: &Path) -> GameFolders {
    let base = root.join("content/base");
    write(
        &base.join("pack.yaml"),
        "id: base\nversion: 1.0.0\nlayer: base\n",
    );
    write(
        &base.join("data/maps.yaml"),
        "maps:\n\
         - {id: area_a, index: 0, filename: AREA_A, doors: null, toilet: null, loading_name: 0}\n\
         - {id: area_b, index: 1, filename: AREA_B, doors: null, toilet: null, loading_name: 0}\n\
         - {id: area_c, index: 2, filename: AREA_C, doors: null, toilet: null, loading_name: 1}\n\
         - {id: area_d, index: 3, filename: AREA_D, doors: null, toilet: null, loading_name: 1}\n\
         - {id: ghost, index: 4, filename: GHOST, doors: null, toilet: null, loading_name: 1}\n\
         map_names: [' Plains ', ' Woods ']\n",
    );
    write(
        &base.join("field/warps.yaml"),
        "start:\n  map: area_a\n  exit: 0\n",
    );
    write(
        &base.join("map/area_a/data.yaml"),
        &map_yaml(&[
            exit(0, 1, "area_b", 0),
            exit(1, 2, "area_c", 7),
            Exit {
                cells: false,
                ..exit(2, 4, "ghost", 0)
            },
            Exit {
                cells: false,
                ..exit(3, -1, "nowhere_x", 0)
            },
            unused(4),
        ]),
    );
    write(
        &base.join("map/area_b/data.yaml"),
        &map_yaml(&[
            Exit {
                arrival_z: 4950,
                ..exit(0, 0, "area_a", 0)
            },
            unused(1),
        ]),
    );
    write(
        &base.join("map/area_c/data.yaml"),
        &map_yaml(&[exit(0, 3, "area_d", 0), unused(1)]),
    );
    write(&base.join("map/area_d/data.yaml"), &map_yaml(&[unused(0)]));
    for map in ["area_a", "area_b", "area_c", "area_d"] {
        write(&base.join(format!("map/{map}/background_day.png")), "day");
    }
    write(&base.join("story/000.lua"), MAP_HEAD);
    write(&base.join("story/001.lua"), MAP_SCRIPT);
    GameFolders {
        content: root.join("content"),
        mods: vec![root.join("mods")],
        ..GameFolders::default()
    }
}

fn opened(root: &Path) -> Mapper {
    std::fs::create_dir_all(root.join("mods")).unwrap();
    let mut mapper = Mapper::new();
    mapper.apply(Action::OpenContent(content(root)));
    assert!(mapper.messages.is_empty(), "{:?}", mapper.messages);
    mapper
}

fn built(mapper: &mut Mapper) -> WorldGraph {
    mapper.refresh_world();
    mapper.world.graph.clone()
}

fn check(severity: Severity, map: &str, kind: CheckKind, exit: Option<i32>) -> WorldCheck {
    WorldCheck {
        severity,
        map: map.to_owned(),
        kind,
        via: exit.map(|exit| EdgeKind::Exit { exit }),
    }
}

#[test]
fn the_graph_has_the_exits_and_script_warps_with_their_checks() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = opened(dir.path());
    let graph = built(&mut mapper);
    let ids: Vec<&str> = graph.nodes.iter().map(|node| node.id.as_str()).collect();
    assert_eq!(ids, ["area_a", "area_b", "area_c", "area_d"]);
    assert_eq!(graph.node("area_c").unwrap().area, "Woods");
    assert_eq!(graph.start, Some(("area_a".to_owned(), 0)));

    let edges: Vec<(String, bool)> = graph
        .edges
        .iter()
        .map(|edge| (edge.label(), edge.two_way))
        .collect();
    assert_eq!(
        edges,
        [
            ("area_a:0 → area_b:0".to_owned(), true),
            ("area_a 1:51 → area_b:0".to_owned(), true),
            ("area_b:0 → area_a:0".to_owned(), true),
            ("area_c:0 → area_d:0".to_owned(), false),
        ]
    );
    let script = &graph.edges[1];
    assert_eq!(
        script.kind,
        EdgeKind::Script {
            script: 1,
            section: 51,
            guards: "not story.trigger(12)".to_owned()
        }
    );
    assert_eq!(graph.edges[0].cells, 2);

    let missing = |target: &str| CheckKind::MissingMap {
        target: target.to_owned(),
    };
    let expected = [
        check(
            Severity::Error,
            "area_a",
            CheckKind::MissingExit {
                target: "area_c".to_owned(),
                exit: 7,
            },
            Some(1),
        ),
        check(Severity::Error, "area_a", missing("ghost"), Some(2)),
        check(Severity::Error, "area_a", missing("nowhere_x"), Some(3)),
        check(
            Severity::Warning,
            "area_b",
            CheckKind::ArrivalInWall { exit: 0 },
            None,
        ),
        check(Severity::Warning, "area_c", CheckKind::Unreachable, None),
        check(
            Severity::Info,
            "area_c",
            CheckKind::OneWay {
                target: "area_d".to_owned(),
            },
            Some(0),
        ),
        check(Severity::Warning, "area_d", CheckKind::Unreachable, None),
    ];
    let mut checks = graph.checks.clone();
    checks.sort_by(|a, b| (&a.map, &a.kind).cmp(&(&b.map, &b.kind)));
    let mut expected = expected.to_vec();
    expected.sort_by(|a, b| (&a.map, &a.kind).cmp(&(&b.map, &b.kind)));
    assert_eq!(checks, expected);
    assert_eq!(graph.worst("area_a"), Some(Severity::Error));
    assert_eq!(graph.worst("area_c"), Some(Severity::Warning));
    for check in &graph.checks {
        assert!(!check.message().is_empty());
    }

    // Paths and references.
    let path = graph.path("area_b", "area_a").unwrap();
    assert_eq!(path.len(), 1);
    assert!(graph.path("area_a", "area_d").is_none());
    assert_eq!(graph.path("area_a", "area_a").unwrap().len(), 0);
    assert_eq!(graph.references_to("area_b").len(), 2);
}

#[test]
fn the_layout_is_the_same_every_time_and_keeps_the_maps_the_user_moved() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = opened(dir.path());
    let graph = built(&mut mapper);
    let first = graph.layout(&BTreeMap::new());
    assert_eq!(first, graph.layout(&BTreeMap::new()));
    assert_eq!(first.len(), 4);
    for (a, b) in [
        ("area_a", "area_b"),
        ("area_c", "area_d"),
        ("area_a", "area_c"),
    ] {
        assert!(first[a].distance(first[b]) > 10.0, "{a} and {b} overlap");
    }
    // The linked maps of an area sit closer than maps of different areas.
    assert!(first["area_a"].distance(first["area_b"]) < first["area_a"].distance(first["area_d"]));

    mapper.apply(Action::MoveWorldNode {
        id: "area_d".into(),
        to: Vec2::new(5000.0, 5000.0),
    });
    assert_eq!(mapper.world.layout["area_d"], Vec2::new(5000.0, 5000.0));
    let key = mapper.world_key().unwrap();
    assert_eq!(mapper.settings.world_layouts[&key]["area_d"], [5000, 5000]);
    let again = graph.layout(&mapper.pinned());
    assert_eq!(again["area_d"], Vec2::new(5000.0, 5000.0));
    mapper.apply(Action::ResetWorldLayout);
    assert!(mapper.pinned().is_empty());
    assert_eq!(mapper.world.layout, first);
}

#[test]
fn the_open_maps_edits_show_in_the_graph() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = opened(dir.path());
    assert!(mapper.refresh_world());
    assert!(!mapper.refresh_world());
    mapper.apply(Action::OpenMap("area_c".into()));
    mapper.apply(Action::Connect(Connect {
        from: "area_c".into(),
        exit: 1,
        to: "area_a".into(),
        to_exit: 4,
        back: false,
    }));
    assert!(mapper.is_dirty());
    let graph = built(&mut mapper);
    let edge = graph
        .edges
        .iter()
        .find(|edge| edge.from == "area_c" && edge.exit() == Some(1))
        .expect("the edited exit is an edge");
    assert_eq!((edge.to.as_str(), edge.to_exit), ("area_a", 4));
}

fn mod_target(mapper: &mut Mapper, root: &Path) {
    mapper.apply(Action::CreateModPack {
        folder: root.join("mods"),
        id: "mine".into(),
    });
    assert!(mapper.current_target().is_some(), "{:?}", mapper.messages);
}

fn errors(mapper: &Mapper) -> Vec<&String> {
    mapper
        .messages
        .iter()
        .filter_map(|message| match message {
            Message::Error(error) => Some(error),
            Message::Info(_) => None,
        })
        .collect()
}

#[test]
fn a_new_map_is_written_into_the_mod_connected_both_ways_and_opened() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = opened(dir.path());
    mod_target(&mut mapper, dir.path());
    mapper.apply(Action::NewMap(NewMap {
        name: "meadow".into(),
        title: "Prado".into(),
        loading_name: 1,
        template: Template::Copy {
            from: "area_a".into(),
            copy_images: false,
            keep_exits: false,
        },
        connect: Some(Connection {
            map: "area_d".into(),
            exit: 0,
            own_exit: 0,
        }),
    }));
    assert!(errors(&mapper).is_empty(), "{:?}", mapper.messages);
    let map = mapper.map.as_ref().expect("the new map is open");
    assert_eq!(map.document.id, "mine:meadow");
    let pack = dir.path().join("mods/mine");
    assert!(pack.join("map/mine/meadow/data.yaml").exists());
    assert!(pack.join("map/area_d/patch.yaml").exists());
    assert!(!dir.path().join("content/base/map/mine").exists());

    let graph = built(&mut mapper);
    let node = graph.node("mine:meadow").unwrap();
    assert!(node.added);
    assert_eq!(node.title.as_deref(), Some("Prado"));
    assert_eq!(node.area, "Woods");
    let both: Vec<String> = graph
        .edges
        .iter()
        .filter(|edge| edge.from == "mine:meadow" || edge.to == "mine:meadow")
        .map(WorldEdgeLabel::label_and_way)
        .collect();
    assert_eq!(
        both,
        [
            "area_d:0 → mine:meadow:0 (two-way)",
            "mine:meadow:0 → area_d:0 (two-way)"
        ]
    );
    // area_d's exit 0 had no cells: connecting it gave it a door.
    let way_in = graph
        .edges
        .iter()
        .find(|edge| edge.from == "area_d")
        .unwrap();
    assert_eq!(way_in.cells, 2);
    assert_eq!(mapper.own_maps(), ["mine:meadow"]);

    // Something leads to it: no delete. A rename takes the mod's exits along.
    mapper.apply(Action::DeleteMap("mine:meadow".into()));
    assert!(
        mapper
            .content
            .as_ref()
            .unwrap()
            .table
            .find("mine:meadow")
            .is_some()
    );
    mapper.messages.clear();
    mapper.apply(Action::RenameMap {
        id: "mine:meadow".into(),
        name: "glade".into(),
    });
    assert!(errors(&mapper).is_empty(), "{:?}", mapper.messages);
    assert_eq!(mapper.map.as_ref().unwrap().document.id, "mine:glade");
    let graph = built(&mut mapper);
    assert!(
        graph
            .edges
            .iter()
            .any(|edge| edge.from == "area_d" && edge.to == "mine:glade")
    );
    // Cut the way in, then the map may go.
    mapper.apply(Action::OpenMap("area_d".into()));
    mapper.apply(Action::Connect(Connect {
        from: "area_d".into(),
        exit: 0,
        to: "area_c".into(),
        to_exit: 0,
        back: false,
    }));
    mapper.apply(Action::Save(dwe_map::save::SaveMode::Patch));
    mapper.messages.clear();
    mapper.apply(Action::DeleteMap("mine:glade".into()));
    assert!(errors(&mapper).is_empty(), "{:?}", mapper.messages);
    assert!(mapper.own_maps().is_empty());
    assert!(!pack.join("map/mine").exists());
}

#[test]
fn only_the_targets_own_maps_may_be_deleted() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = opened(dir.path());
    mod_target(&mut mapper, dir.path());
    mapper.apply(Action::DeleteMap("area_d".into()));
    assert_eq!(errors(&mapper).len(), 1);
    assert!(
        dir.path()
            .join("content/base/map/area_d/data.yaml")
            .exists()
    );
}

trait WorldEdgeLabel {
    fn label_and_way(&self) -> String;
}

impl WorldEdgeLabel for mapper_core::WorldEdge {
    fn label_and_way(&self) -> String {
        let way = if self.two_way { "two-way" } else { "one-way" };
        format!("{} ({way})", self.label())
    }
}

/// With `DWE_MAPPER_CONTENT` set to a content folder (and the checkout's team packs), the graph of
/// the whole game: every used exit of every map is an edge or a check, the layout places every map
/// and the start map reaches most of the world.
#[test]
fn the_whole_games_graph_classifies_every_exit() {
    let Some(folder) = std::env::var_os("DWE_MAPPER_CONTENT") else {
        eprintln!("DWE_MAPPER_CONTENT is not set: skipped");
        return;
    };
    let team_packs = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../../content/packs");
    let mut mapper = Mapper::new();
    mapper.apply(Action::OpenContent(GameFolders {
        content: folder.into(),
        team_packs: Some(team_packs),
        mods: Vec::new(),
    }));
    let started = std::time::Instant::now();
    let graph = built(&mut mapper);
    let took = started.elapsed();
    assert!(graph.broken.is_empty(), "{:?}", graph.broken);
    assert!(graph.nodes.len() > 200, "{} maps", graph.nodes.len());
    assert!(graph.start.is_some());
    let content = mapper.content.as_ref().unwrap();
    let mut exits = 0;
    for node in &graph.nodes {
        let document = dwe_map::load_map(content.vfs(), &node.id).unwrap();
        for warp in &document.warps {
            let trigger = u8::try_from(warp.exit)
                .ok()
                .and_then(|exit| mapper.bands.exit_trigger(exit));
            let walkable = document
                .collision
                .cells()
                .any(|(_, value)| Some(value) == trigger);
            let nowhere = dwe_map::warp_target(&content.table, warp)
                == dwe_map::WarpTarget::Nowhere
                && warp.target_map_id.is_none();
            if nowhere || (warp.target_exit < 0 && !walkable) {
                continue;
            }
            exits += 1;
            let edge = graph
                .edges
                .iter()
                .any(|edge| edge.from == node.id && edge.exit() == Some(warp.exit));
            let checked = graph.checks.iter().any(|check| {
                check.map == node.id && check.via == Some(EdgeKind::Exit { exit: warp.exit })
            });
            assert!(edge || checked, "{}:{} is neither", node.id, warp.exit);
        }
    }
    let scripts = graph.edges.iter().filter(|edge| !edge.is_exit()).count();
    assert!(scripts > 0);
    let layout = graph.layout(&BTreeMap::new());
    assert_eq!(layout.len(), graph.nodes.len());
    assert!(layout.values().all(|place| place.is_finite()));
    let start = &graph.start.as_ref().unwrap().0;
    let reached = graph.reachable_from(start).len();
    let count = |severity| {
        graph
            .checks
            .iter()
            .filter(|check| check.severity == severity)
            .count()
    };
    eprintln!(
        "{} maps, {} edges ({exits} used exits, {scripts} script warps), {reached} reached from {start}; \
         checks: {} errors, {} warnings, {} notes; built in {took:?}",
        graph.nodes.len(),
        graph.edges.len(),
        count(Severity::Error),
        count(Severity::Warning),
        count(Severity::Info),
    );
    for check in graph
        .checks
        .iter()
        .filter(|check| check.severity == Severity::Error)
    {
        eprintln!("  {}", check.message());
    }
    assert!(reached * 2 > graph.nodes.len());
}
