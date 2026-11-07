//! The story patch the editor writes, against the engine: edits the synthetic module of
//! packages/session/tests/data/written_story (condition 71 → 72, the text key, one command added,
//! the shared tail's value) and checks the patch is byte for byte the fixture's
//! story/patches/mapper_900.lua, which packages/session/tests/story_patch_tests.cpp runs through
//! the game's StoryLuaHost and reads with dwx's reader. `DWE_MAPPER_BLESS=1` writes it again after
//! a deliberate change.

use std::path::PathBuf;

use dwe_story::commands::command;
use dwe_story::edit::{StmtPath, call_of_mut, insert, new_command, set_condition, stmt_at};
use dwe_story::{Expr, StoryFile};

fn fixture() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../session/tests/data/written_story/story")
}

#[test]
fn the_written_patch_is_the_one_the_engine_runs() {
    let module =
        StoryFile::parse(&std::fs::read_to_string(fixture().join("900.lua")).unwrap()).unwrap();
    let mut patch =
        StoryFile::new_patch(900, "story/patches/mapper_900.lua: written by dwe-mapper");
    patch.take_section(&module, 0x51).unwrap();
    let body = patch.section_body_mut(0x51).unwrap();
    set_condition(
        body,
        &StmtPath::top(0),
        0,
        Expr::story_call("trigger", vec![Expr::Number(72)]),
    );
    let mut text = stmt_at(body, &StmtPath::top(2)).unwrap().clone();
    call_of_mut(&mut text).unwrap().1[0] = Expr::Str("story_900.text.0200".to_owned());
    dwe_story::edit::replace(body, &StmtPath::top(2), text);
    let mut set = new_command(command("set_trigger").unwrap());
    call_of_mut(&mut set).unwrap().1[0] = Expr::Number(73);
    insert(body, &StmtPath::top(3), set);
    let shared = &mut patch.routine_mut("at_0100").unwrap().body;
    call_of_mut(&mut shared[0]).unwrap().1[1] = Expr::Number(2);
    patch.check_patch().unwrap();
    let written = patch.write();
    let expected = fixture().join("patches/mapper_900.lua");
    if std::env::var_os("DWE_MAPPER_BLESS").is_some() {
        std::fs::write(&expected, &written).unwrap();
    }
    assert_eq!(written, std::fs::read_to_string(expected).unwrap());
}
