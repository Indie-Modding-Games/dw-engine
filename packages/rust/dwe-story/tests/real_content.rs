//! Checks against the player's own extracted content, which never is in the repository: with
//! `DWE_MAPPER_CONTENT` set to a content folder, every story module reads, writes back and reads
//! again as the same tree, every section can be taken into a patch that only reaches its own
//! routines, every section has a timeline (no panic, every length and pose finite) and "play from
//! here" writes a patch that reads back from each of its top-level statements, and every map's
//! events list. Without the variable the tests pass without checking.

use std::path::PathBuf;

use dwe_map::{TriggerBands, load_map, load_map_table};
use dwe_pack::{GameContent, GameFolders};
use dwe_story::edit::StmtPath;
use dwe_story::script::{PATCH_FOLDER, StoryPatches};
use dwe_story::sequence::{self, Sequence, Stage};
use dwe_story::{MapEvents, StoryFile};

fn content() -> Option<GameContent> {
    let folder = PathBuf::from(std::env::var_os("DWE_MAPPER_CONTENT")?);
    Some(
        GameContent::mount(&GameFolders {
            content: folder,
            ..GameFolders::default()
        })
        .expect("the content mounts"),
    )
}

#[test]
fn every_module_round_trips_and_every_section_patches() {
    let Some(content) = content() else {
        eprintln!("DWE_MAPPER_CONTENT is not set: skipped");
        return;
    };
    let mut modules = 0;
    let mut sections = 0;
    let mut same_bytes = 0;
    for id in content.vfs.ids_with_prefix("story/") {
        if id.as_str().starts_with(PATCH_FOLDER) || id.as_str().contains('_') {
            continue;
        }
        let Some((_, file)) = content.vfs.resolve(id) else {
            continue;
        };
        if file.extension().is_none_or(|extension| extension != "lua") {
            continue;
        }
        let text = std::fs::read_to_string(file).unwrap();
        let module = StoryFile::parse(&text).unwrap_or_else(|error| panic!("{id}: {error}"));
        let written = module.write();
        if written == text {
            same_bytes += 1;
        }
        let again =
            StoryFile::parse(&written).unwrap_or_else(|error| panic!("{id} again: {error}"));
        assert_eq!(again, module, "{id} does not read back the same");
        for section in &module.sections {
            let mut patch = StoryFile::new_patch(module.script, "test");
            patch.take_section(&module, section.id).unwrap();
            patch
                .check_patch()
                .unwrap_or_else(|error| panic!("{id} {:#06x}: {error}", section.id));
            let reread = StoryFile::parse(&patch.write()).unwrap();
            assert_eq!(reread, patch, "{id} {:#06x}", section.id);
            sections += 1;
        }
        modules += 1;
    }
    eprintln!("{modules} modules ({same_bytes} byte for byte), {sections} sections");
    assert!(modules > 0);
}

/// The story modules of the content (not the patches), parsed.
fn modules(content: &GameContent) -> Vec<(String, StoryFile)> {
    content
        .vfs
        .ids_with_prefix("story/")
        .filter(|id| !id.as_str().starts_with(PATCH_FOLDER) && !id.as_str().contains('_'))
        .filter_map(|id| {
            let (_, file) = content.vfs.resolve(id)?;
            file.extension()
                .is_some_and(|extension| extension == "lua")
                .then(|| {
                    let text = std::fs::read_to_string(file).unwrap();
                    (id.to_string(), StoryFile::parse(&text).unwrap())
                })
        })
        .collect()
}

#[test]
fn every_section_has_a_timeline_and_plays_from_each_statement() {
    let Some(content) = content() else {
        eprintln!("DWE_MAPPER_CONTENT is not set: skipped");
        return;
    };
    let (mut sections, mut cues, mut starts, mut longest) = (0, 0, 0, 0);
    for (id, module) in modules(&content) {
        let routines = |name: &str| module.routine(name).map(|routine| &routine.body);
        for section in &module.sections {
            let at = format!("{id} {:#06x}", section.id);
            let body = module.section_block(section.id).unwrap();
            let timeline = Sequence::derive(body, &routines, &Stage::default(), None);
            longest = longest.max(timeline.length);
            assert!(
                timeline.length < 20 * 60 * 60,
                "{at}: {} frames",
                timeline.length
            );
            for cue in &timeline.cues {
                assert!(
                    cue.start <= timeline.length && cue.frames < 20 * 60 * 60,
                    "{at}: {cue:?}"
                );
            }
            for track in timeline.tracks() {
                for frame in [
                    0.0,
                    timeline.length as f32 * 0.5,
                    timeline.length as f32 + 1.0,
                ] {
                    if let Some(pose) = timeline.pose_at(track, frame) {
                        assert!(
                            pose.position.is_finite() && pose.yaw.is_finite(),
                            "{at}: {track:?}"
                        );
                    }
                }
            }
            cues += timeline.cues.len();
            for index in 0..body.len() {
                let from = StmtPath::top(index);
                let timeline = Sequence::derive(body, &routines, &Stage::default(), Some(&from));
                let (code, origins) = sequence::play_from(body, &from, &timeline);
                assert_eq!(code.len(), origins.len(), "{at} from {index}");
                let mut patch = StoryFile::new_patch(module.script, "test");
                patch.take_section(&module, section.id).unwrap();
                *patch.section_body_mut(section.id).unwrap() = code;
                patch.drop_unreached_routines();
                patch.has_routines = !patch.routines.is_empty();
                let written = patch.write();
                StoryFile::parse(&written)
                    .unwrap_or_else(|error| panic!("{at} from {index}: {error}\n{written}"));
                starts += 1;
            }
            sections += 1;
        }
    }
    eprintln!("{sections} sections, {cues} cues, longest {longest} frames, {starts} starts");
    assert!(sections > 0);
}

#[test]
fn every_map_lists_its_events() {
    let Some(content) = content() else {
        eprintln!("DWE_MAPPER_CONTENT is not set: skipped");
        return;
    };
    let table = load_map_table(&content.vfs).unwrap();
    let bands = TriggerBands::load(&content.vfs).unwrap_or_default();
    let patches = StoryPatches::load(&content.vfs);
    let (mut maps, mut events, mut missing) = (0, 0, 0);
    for entry in table.maps.iter().filter(|entry| entry.has_file()) {
        let Ok(document) = load_map(&content.vfs, &entry.id) else {
            continue;
        };
        let found = MapEvents::build(
            &content.vfs,
            &patches,
            &table,
            &bands,
            &document,
            Some(entry),
        );
        assert!(
            found.problems.is_empty(),
            "{}: {:?}",
            entry.id,
            found.problems
        );
        assert!(found.map_script.is_some(), "{} has no script", entry.id);
        events += found.events.len();
        missing += found.events.iter().filter(|event| !event.exists).count();
        maps += 1;
    }
    eprintln!("{maps} maps, {events} events, {missing} name a section the script has not");
    assert!(maps > 0);
}
