//! The timeline of a section driven headless over a synthetic content folder (neutral ids,
//! numbers and lines): where the scene starts on the map, the edits the timeline makes (points,
//! frames, order, new commands) with undo, the story patch they save as, and the code "play from
//! here" sends.

use std::path::Path;

use dwe_map::save::SaveMode;
use dwe_pack::GameFolders;
use dwe_story::StoryFile;
use dwe_story::edit::StmtPath;
use dwe_story::sequence::{self, PLAYER, Track};
use glam::{Vec2, Vec3};
use mapper_core::{Action, Mapper, Message, NewCue};

fn write(path: &Path, text: &str) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, text).unwrap();
}

/// A flat map with one exit (its arrival at (0, 0, -300) of the original) and one NPC (id 2).
fn map_yaml() -> String {
    let rows = vec![".".repeat(100); 100];
    let rows: String = rows.iter().map(|row| format!("  - \"{row}\"\n")).collect();
    format!(
        "camera:\n  viewpoint: {{x: 0, y: -3000, z: -4000}}\n  reference: {{x: 0, y: 0, z: 0}}\n  projection: 1000\n\
         tiles:\n  width: 1\n  height: 1\n  rows: [\"0\"]\n\
         warps:\n\
         - exit: 0\n  spawn: {{x: 0, y: 0, z: -300}}\n  rotation: 0\n  target_map: 0\n  target_map_id: \"area_a\"\n  target_exit: 0\n\
         npcs:\n- type_id: 3\n  follow_mode: 1\n  position: {{x: 400, y: 0, z: 0}}\n  rotation: {{x: 0, y: 0, z: 0}}\n  tracking_range: 300\n  script_id: 2\n  hp: 5\n  waypoints: []\n\
         collision:\n  width: 100\n  height: 100\n  encoding: \"legend\"\n  legend:\n\
         \x20 - {{cell: \".\", value: 0}}\n\
         \x20 rows:\n{rows}"
    )
}

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

/// A scene: a guard, the player's walk with the camera after it, a delay, a line, a wait for
/// the walker, the NPC's slide and a trigger.
const SCENE: &str = "-- dwx lua 3: script 1, from script/dg/001
local story <const> = story
local ENDED <const> = story.ENDED
local PLAYER <const> = story.PLAYER
local PARTNER <const> = story.PARTNER

local routines = {}

-- section 0x0050
function routines.at_0080()
  if story.trigger(30) then
    return ENDED -- end_section
  end
  story.entity_walk_to(PLAYER, 200, -100, 0, 0)
  story.move_camera_to(30, 200, -100)
  story.delay(15)
  story.show_textbox(\"story_001.text.0001\")
  story.wait_for_entity(PLAYER)
  story.entity_move_to(2, 400, -400, 20, 0)
  story.set_trigger(30)
  return ENDED -- end_section
end

return {
  script = 1,
  sections = {
    [0x0050] = routines.at_0080,
  },
}
";

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
         map_names: [' Plains ']\n",
    );
    write(&base.join("map/area_a/data.yaml"), &map_yaml());
    write(&base.join("story/000.lua"), MAP_HEAD);
    write(&base.join("story/001.lua"), SCENE);
    write(
        &base.join("text/en/story_001.yaml"),
        "text.0001: \"Line one\"\n",
    );
    GameFolders {
        content: root.join("content"),
        mods: vec![root.join("mods")],
        ..GameFolders::default()
    }
}

/// The scene open with its timeline, a pack to save into.
fn scene(root: &Path) -> Mapper {
    std::fs::create_dir_all(root.join("mods")).unwrap();
    let mut mapper = Mapper::new();
    mapper.apply(Action::OpenContent(content(root)));
    mapper.apply(Action::SetCanvasSize(Vec2::new(1000.0, 700.0)));
    mapper.apply(Action::OpenMap("area_a".into()));
    mapper.apply(Action::CreateModPack {
        folder: root.join("mods"),
        id: "mine".into(),
    });
    mapper.apply(Action::OpenSection {
        script: 1,
        section: 0x50,
    });
    mapper.apply(Action::ShowTimeline(true));
    assert!(
        !mapper
            .messages
            .iter()
            .any(|message| matches!(message, Message::Error(_))),
        "{:?}",
        mapper.messages
    );
    mapper.messages.clear();
    mapper
}

fn code(mapper: &Mapper) -> String {
    dwe_story::write::block_text(mapper.open_code().unwrap())
}

/// Where a ground point of the world is on the canvas.
fn canvas_of(mapper: &Mapper, world: Vec3) -> Vec2 {
    let map = mapper.map.as_ref().unwrap();
    let pixel = map.camera.project_to_background(world).unwrap().pixel;
    mapper.view.to_canvas(pixel)
}

/// The two operands of a call's point (x, z), from the code shown.
fn point_of(mapper: &Mapper, index: usize) -> (i64, i64) {
    let stmt = dwe_story::edit::stmt_at(mapper.open_code().unwrap(), &StmtPath::top(index))
        .unwrap()
        .clone();
    let mut stmt = stmt;
    let (name, arguments) = dwe_story::edit::call_of_mut(&mut stmt).unwrap();
    let (x, z) = sequence::point_operands(name).unwrap();
    (
        sequence::number(&arguments[x]).unwrap(),
        sequence::number(&arguments[z]).unwrap(),
    )
}

#[test]
fn the_scene_starts_where_the_map_puts_everyone() {
    let dir = tempfile::tempdir().unwrap();
    let mapper = scene(dir.path());
    let timeline = mapper.sequence().unwrap();
    // No event runs the section: the player starts at the map's first arrival.
    let walk = timeline
        .cues
        .iter()
        .find(|cue| cue.command == "entity_walk_to")
        .unwrap();
    let (from, to) = walk.way.unwrap();
    assert_eq!(from, Vec3::new(0.0, 0.0, 300.0));
    assert_eq!(to, sequence::ground_point(200, -100));
    assert_eq!(walk.track, Track::Actor(PLAYER));
    // The NPC slides from where the map has it, for its 20 frames.
    let slide = timeline
        .cues
        .iter()
        .find(|cue| cue.command == "entity_move_to")
        .unwrap();
    assert_eq!(slide.way.unwrap().0, Vec3::new(400.0, 0.0, 0.0));
    assert_eq!(slide.frames, 20);
    // The camera's pan and the walk's target are markers a drag moves.
    let handles = mapper.handles(&timeline);
    assert_eq!(handles.len(), 3);
}

#[test]
fn timeline_edits_undo_and_save_as_a_story_patch() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = scene(dir.path());
    let original = code(&mapper);

    // Frames: the delay goes from 15 to 40, and back with undo.
    mapper.apply(Action::SetCueFrames {
        path: StmtPath::top(3),
        frames: 40,
    });
    assert!(code(&mapper).contains("story.delay(40)"));
    mapper.apply(Action::Undo);
    assert_eq!(code(&mapper), original);

    // A drag of the walk's marker: one undo step, the point under the pointer.
    let target = Vec3::new(500.0, 0.0, 300.0);
    let walk_at = canvas_of(&mapper, sequence::ground_point(200, -100));
    mapper.apply(Action::PointerDown {
        at: walk_at,
        modifiers: mapper_core::Modifiers::default(),
    });
    mapper.apply(Action::PointerMove(walk_at + Vec2::new(5.0, 5.0)));
    mapper.apply(Action::PointerMove(canvas_of(&mapper, target)));
    mapper.apply(Action::PointerUp);
    let (x, z) = point_of(&mapper, 1);
    assert!((x - 500).abs() <= 2 && (z + 300).abs() <= 2, "{x} {z}");
    assert_eq!(
        mapper.section.as_ref().unwrap().selected,
        Some(StmtPath::top(1))
    );
    mapper.apply(Action::Undo);
    assert_eq!(code(&mapper), original);

    // Order: the delay after the text; the return cannot be passed.
    mapper.apply(Action::MoveCue {
        path: StmtPath::top(3),
        later: true,
    });
    assert!(code(&mapper).find("show_textbox").unwrap() < code(&mapper).find("delay").unwrap());
    mapper.apply(Action::Undo);
    assert_eq!(code(&mapper), original);
    mapper.apply(Action::MoveCue {
        path: StmtPath::top(7),
        later: true,
    });
    assert_eq!(code(&mapper), original);
    assert!(matches!(mapper.messages.last(), Some(Message::Error(_))));

    // New commands at the playhead: after the statement running then.
    mapper.apply(Action::SetPlayhead(16.0));
    mapper.apply(Action::AddCue {
        kind: NewCue::Text,
        at: None,
    });
    let added = mapper.section.as_ref().unwrap().selected.clone().unwrap();
    let text = code(&mapper);
    assert!(
        text.contains("story.show_textbox(\"story_001.text.0002\")"),
        "{text}"
    );
    assert_eq!(mapper.text_of("story_001.text.0002"), Some("…"));
    // The line and its text are one step.
    mapper.apply(Action::Undo);
    assert_eq!(code(&mapper), original);
    assert_eq!(mapper.text_of("story_001.text.0002"), None);
    mapper.apply(Action::Redo);
    assert_eq!(mapper.section.as_ref().unwrap().selected, Some(added));
    mapper.apply(Action::AddCue {
        kind: NewCue::Walk,
        at: Some(canvas_of(&mapper, Vec3::new(-200.0, 0.0, 100.0))),
    });
    assert!(
        code(&mapper).contains("story.entity_walk_to(PLAYER, -200, -100"),
        "{}",
        code(&mapper)
    );
    mapper.apply(Action::SetCueFrames {
        path: StmtPath::top(3),
        frames: 30,
    });

    // Saved as a story patch, read back from the packs as the same code.
    let edited = code(&mapper);
    mapper.apply(Action::Save(SaveMode::Patch));
    assert!(mapper.save_problems.is_empty() && mapper.story_problems.is_empty());
    let patch =
        std::fs::read_to_string(dir.path().join("mods/mine/story/patches/mine_001.lua")).unwrap();
    assert!(patch.contains("[0x0050] = function(original)"));
    assert!(patch.contains("story.delay(30)"));
    assert_eq!(code(&mapper), edited);
    assert_eq!(
        mapper.open_origin(),
        Some("mine:story/patches/mine_001".into())
    );
}

#[test]
fn play_from_here_places_everyone_and_maps_its_lines_back() {
    let dir = tempfile::tempdir().unwrap();
    let mapper = scene(dir.path());
    // From the wait for the walker: the walk, the pan, the delay and the text happened.
    let at = StmtPath::top(5);
    let sent = mapper.play_from_here(&at).unwrap();
    assert_eq!((sent.script, sent.section), (1, 0x50));
    let file = StoryFile::parse(&sent.code).unwrap();
    file.check_patch().unwrap();
    let body = file.section_block(0x50).unwrap();
    let text = dwe_story::write::block_text(body);
    assert!(
        text.starts_with("story.entity_move_to(story.PLAYER, 200, -100, 1, 0)"),
        "{text}"
    );
    assert!(text.contains("story.move_camera_to(1, 200, -100)"));
    assert!(!text.contains("show_textbox"));
    assert!(text.contains("story.trigger(30)"));
    // The game's line of the wait is the wait.
    let line = body
        .iter()
        .find(|stmt| {
            dwe_story::write::block_text(&vec![(*stmt).clone()]).contains("wait_for_entity(PLAYER)")
        })
        .unwrap()
        .line;
    assert_eq!(
        sent.lines.get(&line),
        Some(&at),
        "{line} {:?}\n{}",
        sent.lines,
        sent.code
    );
    // Playing asks first, with the writes that get past the guard.
    let plan = mapper.play_plan().unwrap();
    assert!(!plan.press);
    assert_eq!(plan.writes.len(), 1, "{plan:?}");
}
