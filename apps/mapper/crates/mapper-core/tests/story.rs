//! The story side of the editor driven headless over a synthetic content folder (neutral ids,
//! numbers and lines): the events of a map, the section editor's edits with undo, the story patch
//! and the lines a save writes, NPC slots added and removed, doors and the toilet.

use std::path::Path;

use dwe_map::save::SaveMode;
use dwe_map::table::MapToilet;
use dwe_pack::GameFolders;
use dwe_story::edit::{StmtPath, call_of_mut, new_command, stmt_at};
use dwe_story::needs::Need;
use dwe_story::{EventKind, Expr};
use glam::Vec2;
use mapper_core::{Action, Mapper, Message, StoryEdit};

fn write(path: &Path, text: &str) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, text).unwrap();
}

/// A map whose row 50 has two step cells (51) and one exit cell, with one NPC (talk section 2).
fn map_yaml() -> String {
    let mut rows = vec![".".repeat(100); 100];
    rows[50] = format!("{}AAD{}", ".".repeat(50), ".".repeat(47));
    let rows: String = rows.iter().map(|row| format!("  - \"{row}\"\n")).collect();
    format!(
        "camera:\n  viewpoint: {{x: 0, y: -3000, z: -4000}}\n  reference: {{x: 0, y: 0, z: 0}}\n  projection: 1000\n\
         tiles:\n  width: 1\n  height: 1\n  rows: [\"0\"]\n\
         warps:\n\
         - exit: 0\n  spawn: {{x: 0, y: 0, z: -300}}\n  rotation: 0\n  target_map: 1\n  target_map_id: \"area_b\"\n  target_exit: 0\n\
         npcs:\n- type_id: 3\n  follow_mode: 1\n  position: {{x: 100, y: 0, z: 100}}\n  rotation: {{x: 0, y: 0, z: 0}}\n  tracking_range: 300\n  script_id: 2\n  hp: 5\n  waypoints: []\n\
         collision:\n  width: 100\n  height: 100\n  encoding: \"legend\"\n  legend:\n\
         \x20 - {{cell: \".\", value: 0}}\n  - {{cell: \"A\", value: 51}}\n  - {{cell: \"D\", value: 110}}\n\
         \x20 rows:\n{rows}"
    )
}

const MAP_HEAD: &str = "-- dwx lua 3: script 0, from script/dg/000
local story <const> = story
local ENDED <const> = story.ENDED
local PLAYER <const> = story.PLAYER
local PARTNER <const> = story.PARTNER

local routines = {}

-- section 0x0000
function routines.at_0010()
  story.set_script(1, 0) -- script, map
  story.load_species_model(3)
  story.spawn_npc(3, 0, 1) -- species, slot, autotalk
  return ENDED -- end_section
end

return {
  script = 0,
  sections = {
    [0x0000] = routines.at_0010,
  },
}
";

const MAP_SCRIPT: &str = "-- dwx lua 3: script 1, from script/dg/001
local story <const> = story
local ENDED <const> = story.ENDED
local PLAYER <const> = story.PLAYER
local PARTNER <const> = story.PARTNER

local routines = {}

-- section 0x0002
function routines.at_0020()
  story.set_dialog_owner(2)
  story.show_textbox(\"story_001.text.0001\")
  return ENDED -- end_section
end

-- section 0x0033
function routines.at_0040()
  if story.trigger(12) then
    return ENDED -- end_section
  end
  story.show_textbox(\"story_001.text.0002\")
  if story.var(3) >= 2 then
    return story.warp_to(1, 0, 255) -- map, exit, section
  end
  return ENDED -- end_section
end

-- section 0x00fe
function routines.at_0060()
  story.set_bgm(1)
  return ENDED -- end_section
end

return {
  script = 1,
  sections = {
    [0x0002] = routines.at_0020,
    [0x0033] = routines.at_0040,
    [0x00fe] = routines.at_0060,
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
         - {id: area_a, index: 0, filename: AREA_A, doors: 0, toilet: 0, loading_name: 0}\n\
         - {id: area_b, index: 1, filename: AREA_B, doors: 0, toilet: null, loading_name: 0}\n\
         doors:\n- index: 0\n  doors:\n  - {model_id: 4, position: {x: 200, y: 0, z: 0}, rotation: 0}\n\
         toilets:\n- {index: 0, x1: -200, z1: 0, x2: -300, z2: 100}\n\
         map_names: [' Plains ']\n",
    );
    write(&base.join("map/area_a/data.yaml"), &map_yaml());
    write(&base.join("map/area_b/data.yaml"), &map_yaml());
    write(&base.join("story/000.lua"), MAP_HEAD);
    write(&base.join("story/001.lua"), MAP_SCRIPT);
    write(
        &base.join("text/en/story_001.yaml"),
        "text.0001: \"Line one\"\ntext.0002: \"Line two\"\n",
    );
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
    mapper.apply(Action::SetCanvasSize(Vec2::new(1000.0, 700.0)));
    mapper.apply(Action::OpenMap("area_a".into()));
    assert!(mapper.messages.is_empty(), "{:?}", mapper.messages);
    mapper
}

fn with_target(root: &Path) -> Mapper {
    let mut mapper = opened(root);
    mapper.apply(Action::CreateModPack {
        folder: root.join("mods"),
        id: "mine".into(),
    });
    assert!(mapper.current_target().is_some());
    mapper.messages.clear();
    mapper
}

fn code(mapper: &Mapper) -> String {
    dwe_story::write::block_text(mapper.open_code().unwrap())
}

#[test]
fn lists_the_events_of_a_map() {
    let dir = tempfile::tempdir().unwrap();
    let mapper = opened(dir.path());
    let events = mapper.events();
    let kinds: Vec<(EventKind, u16, u16)> = events
        .iter()
        .map(|event| (event.kind, event.script, event.section))
        .collect();
    assert_eq!(
        kinds,
        vec![
            (EventKind::MapEntry, 0, 0),
            (EventKind::MapLoaded, 1, 0xfe),
            (EventKind::Step, 1, 51),
            (EventKind::Talk { slot: 0 }, 1, 2),
        ]
    );
    let step = &events[2];
    assert_eq!(step.cells.len(), 2);
    assert!(step.exists);
    assert_eq!(step.jumps.len(), 1);
    assert_eq!(step.jumps[0].to_map.as_deref(), Some("area_b"));
    assert_eq!(mapper.text_of("story_001.text.0002"), Some("Line two"));
}

#[test]
fn edits_a_section_with_undo_and_saves_it_as_a_patch() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = with_target(dir.path());
    mapper.apply(Action::OpenSection {
        script: 1,
        section: 0x33,
    });
    assert_eq!(mapper.open_origin(), None);
    let original = code(&mapper);
    mapper.apply(Action::EditStory(StoryEdit::SetCondition {
        at: StmtPath::top(0),
        arm: 0,
        condition: Expr::story_call("trigger", vec![Expr::Number(13)]),
    }));
    let mut line = stmt_at(mapper.open_code().unwrap(), &StmtPath::top(1))
        .unwrap()
        .clone();
    call_of_mut(&mut line).unwrap().1[0] = Expr::Str(mapper.free_text_key(1));
    mapper.apply(Action::EditStory(StoryEdit::Replace {
        at: StmtPath::top(1),
        stmt: line,
    }));
    let mut set = new_command(dwe_story::commands::command("set_trigger").unwrap());
    call_of_mut(&mut set).unwrap().1[0] = Expr::Number(20);
    mapper.apply(Action::EditStory(StoryEdit::Insert {
        at: StmtPath::top(2),
        stmt: set,
    }));
    mapper.apply(Action::SetText {
        key: "story_001.text.0003".into(),
        text: "A new line".into(),
    });
    mapper.apply(Action::SetText {
        key: "story_001.text.0001".into(),
        text: "Line one, edited".into(),
    });
    assert_eq!(mapper.open_origin(), Some("mine (sin guardar)".into()));
    assert!(code(&mapper).contains("story.trigger(13)"));
    assert!(code(&mapper).contains("story.show_textbox(\"story_001.text.0003\")"));
    assert_eq!(mapper.text_of("story_001.text.0003"), Some("A new line"));
    // The events read the drafts.
    let step = mapper.events().iter().find(|e| e.section == 0x33).unwrap();
    assert_eq!(
        step.patched_by.as_deref(),
        Some("mine:story/patches/mine_001")
    );

    // Undo all, redo all.
    for _ in 0..5 {
        mapper.apply(Action::Undo);
    }
    assert_eq!(code(&mapper), original);
    assert_eq!(mapper.open_origin(), None);
    assert_eq!(mapper.text_of("story_001.text.0001"), Some("Line one"));
    assert!(!mapper.is_dirty());
    for _ in 0..5 {
        mapper.apply(Action::Redo);
    }
    assert!(code(&mapper).contains("story.set_trigger(20)"));

    mapper.apply(Action::Save(SaveMode::Patch));
    assert!(mapper.save_problems.is_empty() && mapper.story_problems.is_empty());
    assert!(!mapper.is_dirty());
    let pack = dir.path().join("mods/mine");
    let patch = std::fs::read_to_string(pack.join("story/patches/mine_001.lua")).unwrap();
    assert!(patch.starts_with("-- story/patches/mine_001.lua: written by dwe-mapper\n"));
    assert!(patch.contains("[0x0033] = function(original)"));
    assert!(!patch.contains("[0x0002]"));
    let lines = std::fs::read_to_string(pack.join("text/en/story_001.yaml")).unwrap();
    assert_eq!(
        lines,
        "text.0001: \"Line one, edited\"\ntext.0003: \"A new line\"\n"
    );
    // Read back from the packs, the section is the patch's.
    assert_eq!(
        mapper.open_origin(),
        Some("mine:story/patches/mine_001".into())
    );
    assert!(code(&mapper).contains("story.set_trigger(20)"));
    assert_eq!(mapper.text_of("story_001.text.0003"), Some("A new line"));
    // The warp moved under another condition: field/script_warps must be made again, and no
    // dwx is set, so the editor says the command.
    assert!(mapper.messages.iter().any(|message| matches!(message,
        Message::Error(text) if text.contains("script-warps") && text.contains("field/script_warps.yaml"))));

    // Reverting the section takes it out of the patch: the file goes away on save.
    mapper.apply(Action::EditStory(StoryEdit::Revert));
    assert_eq!(code(&mapper), original);
    mapper.apply(Action::Save(SaveMode::Patch));
    assert!(!pack.join("story/patches/mine_001.lua").exists());
}

#[test]
fn a_patch_that_does_not_run_is_refused() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = with_target(dir.path());
    mapper.apply(Action::OpenSection {
        script: 1,
        section: 0x33,
    });
    mapper.apply(Action::EditStory(StoryEdit::SetCode(
        "return routines.at_9999()\n".into(),
    )));
    assert!(!mapper.save(SaveMode::Patch));
    assert!(
        mapper
            .story_problems
            .iter()
            .any(|problem| problem.is_error())
    );
    // Code that does not read is a message, not an edit.
    mapper.messages.clear();
    mapper.apply(Action::EditStory(StoryEdit::SetCode("if then".into())));
    assert!(matches!(&mapper.messages[..], [Message::Error(text)] if text.starts_with("line 1")));
}

#[test]
fn a_new_npc_slot_is_spawned_and_the_map_saves_whole() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = with_target(dir.path());
    mapper.apply(Action::AddNpc(None));
    let map = mapper.map.as_ref().unwrap();
    assert_eq!(map.document.npcs.len(), 2);
    assert_eq!(
        map.document.npcs[1]
            .other
            .get("hp")
            .and_then(|v| v.as_i64()),
        Some(5)
    );
    let entry = map.drafts.scripts.get(&0).unwrap().file.write();
    let load = entry.find(
        "story.load_species_model(3)\n      story.spawn_npc(3, 1, 1) -- species, slot, autotalk",
    );
    assert!(
        load.is_some_and(|at| at < entry.rfind("return ENDED").unwrap()),
        "{entry}"
    );
    assert!(
        mapper
            .events()
            .iter()
            .any(|event| event.kind == EventKind::Talk { slot: 1 })
    );
    // One undo step takes both away.
    mapper.apply(Action::Undo);
    assert_eq!(mapper.map.as_ref().unwrap().document.npcs.len(), 1);
    assert!(mapper.map.as_ref().unwrap().drafts.scripts.is_empty());
    mapper.apply(Action::Redo);
    mapper.apply(Action::Save(SaveMode::Patch));
    assert!(
        mapper.save_problems.is_empty() && mapper.story_problems.is_empty(),
        "{:?} {:?}",
        mapper.save_problems,
        mapper.story_problems
    );
    let pack = dir.path().join("mods/mine");
    assert!(pack.join("map/area_a/data.yaml").exists());
    assert!(!pack.join("map/area_a/patch.yaml").exists());
    assert!(pack.join("story/patches/mine_000.lua").exists());
    assert_eq!(mapper.map.as_ref().unwrap().document.npcs.len(), 2);

    // The last slot goes, with its spawn.
    mapper.apply(Action::RemoveNpc);
    assert_eq!(mapper.map.as_ref().unwrap().document.npcs.len(), 1);
    let entry = mapper
        .map
        .as_ref()
        .unwrap()
        .drafts
        .scripts
        .get(&0)
        .unwrap()
        .file
        .write();
    assert!(!entry.contains("spawn_npc(3, 1, 1)"));
    assert!(entry.contains("spawn_npc(3, 0, 1)"));
}

#[test]
fn doors_and_the_toilet_save_into_the_table() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = with_target(dir.path());
    let mut door = mapper.places().doors[0].clone();
    door.position.x = 900;
    mapper.apply(Action::SetDoor { slot: 0, door });
    mapper.apply(Action::SetToilet(Some(MapToilet {
        index: 0,
        x1: 1,
        z1: 2,
        x2: 3,
        z2: 4,
    })));
    assert_eq!(mapper.places().doors.len(), 6);
    assert_eq!(mapper.places().toilet.unwrap().x2, 3);
    mapper.apply(Action::Undo);
    assert_eq!(mapper.places().toilet.unwrap().x2, -300);
    mapper.apply(Action::Redo);
    mapper.apply(Action::Save(SaveMode::Patch));
    assert!(
        mapper.story_problems.is_empty(),
        "{:?}",
        mapper.story_problems
    );
    let table = std::fs::read_to_string(dir.path().join("mods/mine/data/maps.yaml")).unwrap();
    assert!(table.contains("id: area_a"));
    // area_a shared door set 0 with area_b: it got a set of its own.
    let content = mapper.content.as_ref().unwrap();
    assert_eq!(content.table.find("area_a").unwrap().doors, Some(1));
    assert_eq!(content.table.find("area_b").unwrap().doors, Some(0));
    assert_eq!(mapper.places().doors[0].position.x, 900);
    assert_eq!(mapper.places().toilet.unwrap().z2, 4);
}

#[test]
fn plans_an_event_test() {
    let dir = tempfile::tempdir().unwrap();
    let mapper = opened(dir.path());
    let events = mapper.events();
    let step = mapper.test_plan(&events[2]);
    assert_eq!(step.map, "area_a");
    assert_eq!(
        step.writes,
        vec![Need::Trigger {
            number: 12,
            on: false
        }]
    );
    assert!(step.place.as_deref().unwrap().starts_with("player "));
    assert!(!step.press);
    let talk = mapper.test_plan(&events[3]);
    assert!(talk.press);
    assert!(talk.writes.is_empty());
}

#[test]
fn moves_between_maps_and_reverts() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = opened(dir.path());
    mapper.apply(Action::OpenAdjacentMap { later: true });
    assert_eq!(mapper.map.as_ref().unwrap().document.id, "area_b");
    mapper.apply(Action::OpenAdjacentMap { later: true });
    assert_eq!(mapper.map.as_ref().unwrap().document.id, "area_a");
    mapper.apply(Action::AddNpc(None));
    assert!(mapper.is_dirty());
    mapper.apply(Action::Revert);
    assert!(!mapper.is_dirty());
    assert_eq!(mapper.map.as_ref().unwrap().document.npcs.len(), 1);
}

#[test]
fn edits_show_before_a_target_is_chosen() {
    let dir = tempfile::tempdir().unwrap();
    let mut mapper = opened(dir.path());
    mapper.apply(Action::OpenSection {
        script: 1,
        section: 0x33,
    });
    mapper.apply(Action::EditStory(StoryEdit::Remove(StmtPath::top(0))));
    assert!(!code(&mapper).contains("story.trigger(12)"));
    assert_eq!(mapper.open_origin(), Some("mapper (sin guardar)".into()));
}
