//! Saving edited story sections into a pack: one patch file a script
//! (`story/patches/<pack>_<nnn>.lua`, or the pack's own patch of that script when it has one) and
//! the edited dialogue lines in the pack's text tables, merged key by key as the game merges them.

use std::collections::BTreeMap;
use std::path::PathBuf;

use dwe_map::save::FileChange;
use dwe_pack::{AssetId, MountedPack, Vfs};

use crate::events::jumps_of;
use crate::file::StoryFile;
use crate::script::{LoadedScript, PATCH_FOLDER, StoryPatches};
use crate::texts::{ENGLISH, read_table_file, split_key, table_id, write_table};

/// What the editor shows when the code of a patch would not run.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum StoryProblem {
    Error(String),
    Warning(String),
}

impl StoryProblem {
    pub fn is_error(&self) -> bool {
        matches!(self, Self::Error(_))
    }
}

/// An edited script: the patch of the target pack as edited, and as it was when the editing
/// started (to merge with a file that changed since).
#[derive(Clone, Debug, PartialEq)]
pub struct ScriptDraft {
    pub file: StoryFile,
    pub base: StoryFile,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct StorySavePlan {
    pub changes: Vec<FileChange>,
    pub problems: Vec<StoryProblem>,
    /// The scripts whose map changes (`warp_to`) the save changes: `field/script_warps` must be
    /// made again.
    pub warps_changed: Vec<u16>,
}

impl StorySavePlan {
    pub fn has_errors(&self) -> bool {
        self.problems.iter().any(StoryProblem::is_error)
    }
}

/// The id of the patch a pack has for a script, if any (its first by id).
pub fn patch_of_pack(
    patches: &StoryPatches,
    pack: &str,
    script: u16,
) -> Option<(AssetId, PathBuf)> {
    patches
        .of_script(script)
        .find(|(source, _)| source.pack == pack)
        .map(|(source, _)| (source.id.clone(), source.file.clone()))
}

/// The name the editor gives a new patch of a script in a pack.
pub fn new_patch_id(pack: &str, script: u16) -> String {
    let pack: String = pack
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() {
                c.to_ascii_lowercase()
            } else {
                '_'
            }
        })
        .collect();
    format!("{PATCH_FOLDER}{pack}_{script:03}")
}

/// The header comment of a patch the editor writes.
pub fn patch_header(id: &str) -> String {
    format!("{id}.lua: written by dwe-mapper")
}

/// The patch of a script a pack starts editing from: its own, or an empty one.
pub fn starting_patch(patches: &StoryPatches, pack: &str, script: u16) -> StoryFile {
    patches
        .of_script(script)
        .find(|(source, _)| source.pack == pack)
        .map(|(_, file)| file.clone())
        .unwrap_or_else(|| StoryFile::new_patch(script, &patch_header(&new_patch_id(pack, script))))
}

/// Plans the files a save writes into `target`.
pub fn plan_story_save(
    vfs: &Vfs,
    patches: &StoryPatches,
    target: &MountedPack,
    scripts: &BTreeMap<u16, ScriptDraft>,
    texts: &BTreeMap<String, String>,
    language: &str,
) -> StorySavePlan {
    let mut plan = StorySavePlan::default();
    for (&script, draft) in scripts {
        if draft.file == draft.base {
            continue;
        }
        let existing = patches
            .of_script(script)
            .find(|(source, _)| source.pack == target.id());
        let (path, on_disk) = match existing {
            Some((source, file)) => (source.file.clone(), Some(file.clone())),
            None => (
                target
                    .root
                    .join(format!("{}.lua", new_patch_id(target.id(), script))),
                None,
            ),
        };
        let new = on_disk.is_none();
        let mut file = match on_disk {
            // Nobody changed the file since the editing started: the draft is the file.
            Some(on_disk) if on_disk != draft.base => merge(on_disk, draft),
            _ => draft.file.clone(),
        };
        if new {
            // The draft may have started for another pack: the new file names itself.
            let id = new_patch_id(target.id(), script);
            file.preamble = StoryFile::new_patch(script, &patch_header(&id)).preamble;
        }
        file.drop_unreached_routines();
        if file.routines.is_empty() {
            file.has_routines = false;
        }
        if let Err(error) = file.check_patch() {
            plan.problems
                .push(StoryProblem::Error(format!("script {script}: {error}")));
            continue;
        }
        let loaded = LoadedScript::load(vfs, patches, script);
        for section in &file.sections {
            if let Some(crate::script::SectionOrigin::Patch(index)) = loaded.origin(section.id) {
                let (source, _) = &loaded.patches[index];
                if source.pack != target.id() && is_above(vfs, &source.pack, target.id()) {
                    plan.problems.push(StoryProblem::Warning(format!(
                        "script {script} sección {:#06x}: el parche {} está encima y gana",
                        section.id,
                        source.name()
                    )));
                }
            }
        }
        if warps_of(&loaded, &file, &draft.base) {
            plan.warps_changed.push(script);
        }
        plan.changes.push(FileChange {
            path,
            text: (!file.sections.is_empty()).then(|| file.write()),
        });
    }
    plan.changes.extend(text_changes(target, texts, language));
    plan
}

/// Puts the draft's sections into a file that changed on disk since the editing started, and
/// drops the sections the draft dropped.
fn merge(mut on_disk: StoryFile, draft: &ScriptDraft) -> StoryFile {
    for section in &draft.file.sections {
        let _ = on_disk.take_section(&draft.file, section.id);
    }
    for section in &draft.base.sections {
        if draft.file.section(section.id).is_none() {
            on_disk.remove_section(section.id);
        }
    }
    on_disk
}

fn is_above(vfs: &Vfs, pack: &str, other: &str) -> bool {
    let position = |id: &str| vfs.packs().iter().position(|p| p.id() == id);
    matches!((position(pack), position(other)), (Some(a), Some(b)) if a > b)
}

/// Whether the map changes of the sections the save touches change.
fn warps_of(loaded: &LoadedScript, file: &StoryFile, base: &StoryFile) -> bool {
    let table = dwe_map::MapTable::default();
    let mut ids: Vec<u16> = file.sections.iter().map(|s| s.id).collect();
    ids.extend(base.sections.iter().map(|s| s.id));
    ids.sort_unstable();
    ids.dedup();
    ids.into_iter().any(|id| {
        let before = loaded
            .section(id)
            .map(|(_, from, block)| jumps_of(from, block, &table))
            .unwrap_or_default();
        let after = match file.section_block(id) {
            Some(block) => jumps_of(file, block, &table),
            None => loaded
                .module
                .as_ref()
                .and_then(|(_, module)| {
                    module
                        .section_block(id)
                        .map(|b| jumps_of(module, b, &table))
                })
                .unwrap_or_default(),
        };
        before != after
    })
}

/// The text tables of the target with the edited lines, merged into what it has.
fn text_changes(
    target: &MountedPack,
    texts: &BTreeMap<String, String>,
    language: &str,
) -> Vec<FileChange> {
    let language = if language.is_empty() {
        ENGLISH
    } else {
        language
    };
    let mut by_table: BTreeMap<&str, Vec<(&str, &str)>> = BTreeMap::new();
    for (key, text) in texts {
        if let Some((table, inner)) = split_key(key) {
            by_table.entry(table).or_default().push((inner, text));
        }
    }
    let mut changes = Vec::new();
    for (table, lines) in by_table {
        let id = table_id(language, table);
        let known = AssetId::parse(&id)
            .ok()
            .and_then(|id| target.file(&id).map(PathBuf::from));
        let path = known
            .clone()
            .unwrap_or_else(|| target.root.join(format!("{id}.yaml")));
        let mut merged = known
            .and_then(|file| read_table_file(&file).ok())
            .unwrap_or_default();
        for (key, text) in lines {
            merged.insert(key.to_owned(), text.to_owned());
        }
        changes.push(FileChange {
            path,
            text: Some(write_table(&merged)),
        });
    }
    changes
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ast::Expr;
    use crate::edit::{StmtPath, call_of_mut, insert, stmt_at};

    const MODULE: &str = include_str!("../../../session/tests/data/written_story/story/900.lua");

    struct Packs {
        dir: tempfile::TempDir,
    }

    impl Packs {
        fn new() -> Self {
            let packs = Self {
                dir: tempfile::tempdir().unwrap(),
            };
            packs.pack("base", "base", 0);
            packs.file("base/story/900.lua", MODULE);
            packs.file("base/text/en/story_900.yaml", "text.0042: \"Hello\"\n");
            packs.pack("mine", "mods", 0);
            packs
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

    fn target<'a>(vfs: &'a Vfs, id: &str) -> &'a MountedPack {
        vfs.packs().iter().find(|pack| pack.id() == id).unwrap()
    }

    /// Takes section 0x51 of the module into the patch and gives its line another key.
    fn edited(patches: &StoryPatches, vfs: &Vfs) -> ScriptDraft {
        let base = starting_patch(patches, "mine", 900);
        let mut file = base.clone();
        let loaded = LoadedScript::load(vfs, patches, 900);
        let (_, module) = loaded.module.as_ref().unwrap();
        file.take_section(module, 0x51).unwrap();
        let body = file.section_body_mut(0x51).unwrap();
        let mut line = stmt_at(body, &StmtPath::top(2)).unwrap().clone();
        call_of_mut(&mut line).unwrap().1[0] = Expr::Str("story_900.text.0200".to_owned());
        crate::edit::replace(body, &StmtPath::top(2), line);
        ScriptDraft { file, base }
    }

    #[test]
    fn writes_a_new_patch_and_the_line() {
        let packs = Packs::new();
        let vfs = packs.mount(&["base", "mine"]);
        let patches = StoryPatches::load(&vfs);
        let scripts = BTreeMap::from([(900, edited(&patches, &vfs))]);
        let texts = BTreeMap::from([("story_900.text.0200".to_owned(), "Bye".to_owned())]);
        let plan = plan_story_save(&vfs, &patches, target(&vfs, "mine"), &scripts, &texts, "");
        assert!(plan.problems.is_empty(), "{:?}", plan.problems);
        assert!(plan.warps_changed.is_empty());
        let root = packs.dir.path().join("mine");
        assert_eq!(plan.changes.len(), 2);
        assert_eq!(
            plan.changes[0].path,
            root.join("story/patches/mine_900.lua")
        );
        let patch = plan.changes[0].text.as_deref().unwrap();
        assert!(patch.starts_with("-- story/patches/mine_900.lua: written by dwe-mapper\n"));
        assert!(patch.contains("story.show_textbox(\"story_900.text.0200\")"));
        assert!(patch.contains("function routines.at_0100()"));
        assert!(!patch.contains("at_0080"));
        assert_eq!(plan.changes[1].path, root.join("text/en/story_900.yaml"));
        assert_eq!(
            plan.changes[1].text.as_deref(),
            Some("text.0200: \"Bye\"\n")
        );

        // Written, the stack runs the patch's section and the line is the pack's.
        dwe_map::save::write_changes(&plan.changes).unwrap();
        let vfs = packs.mount(&["base", "mine"]);
        let patches = StoryPatches::load(&vfs);
        let loaded = LoadedScript::load(&vfs, &patches, 900);
        assert_eq!(
            loaded.patched_by(0x51),
            Some("mine:story/patches/mine_900".to_owned())
        );
        assert_eq!(loaded.patched_by(0x52), None);
        let mut texts = crate::StoryTexts::new("");
        texts.load_table(&vfs, "story_900");
        assert_eq!(texts.text("story_900.text.0200"), Some("Bye"));
        assert_eq!(texts.text("story_900.text.0042"), Some("Hello"));
    }

    #[test]
    fn a_draft_runs_in_the_packs_place() {
        let packs = Packs::new();
        packs.pack("top", "mods", 5);
        packs.file(
            "top/story/patches/top_900.lua",
            "return {\n  script = 900,\n  sections = {\n    [0x0052] = function(original)\n      return original()\n    end,\n  },\n}\n",
        );
        let vfs = packs.mount(&["base", "mine", "top"]);
        let patches = StoryPatches::load(&vfs);
        let draft = edited(&patches, &vfs);
        let shown = patches.with_draft(&vfs, "mine", &draft.file);
        let loaded = LoadedScript::load(&vfs, &shown, 900);
        assert_eq!(
            loaded.patched_by(0x51),
            Some("mine:story/patches/mine_900".to_owned())
        );
        assert_eq!(
            loaded.patched_by(0x52),
            Some("top:story/patches/top_900".to_owned())
        );
        assert_eq!(loaded.patches[0].0.pack, "mine");
        // A draft for a pack that is not mounted runs on top.
        let shown = patches.with_draft(&vfs, "elsewhere", &draft.file);
        let loaded = LoadedScript::load(&vfs, &shown, 900);
        assert_eq!(loaded.patches.last().unwrap().0.pack, "elsewhere");
        assert_eq!(
            loaded.patched_by(0x51),
            Some("elsewhere:story/patches/elsewhere_900".to_owned())
        );
        // An empty draft leaves the stack as it was.
        let empty = StoryFile::new_patch(900, "x");
        assert_eq!(patches.with_draft(&vfs, "mine", &empty).patches.len(), 1);
    }

    #[test]
    fn merges_with_a_patch_that_changed_since() {
        let packs = Packs::new();
        let vfs = packs.mount(&["base", "mine"]);
        let patches = StoryPatches::load(&vfs);
        let draft = edited(&patches, &vfs);
        // Someone else writes the pack's patch of the script meanwhile, with another section.
        packs.file(
            "mine/story/patches/mine_900.lua",
            "local story <const> = story\nreturn {\n  script = 900,\n  sections = {\n    [0x0052] = function(original)\n      story.set_trigger(9)\n      return original()\n    end,\n  },\n}\n",
        );
        let vfs = packs.mount(&["base", "mine"]);
        let patches = StoryPatches::load(&vfs);
        let scripts = BTreeMap::from([(900, draft)]);
        let plan = plan_story_save(
            &vfs,
            &patches,
            target(&vfs, "mine"),
            &scripts,
            &BTreeMap::new(),
            "",
        );
        assert!(plan.problems.is_empty(), "{:?}", plan.problems);
        let patch = plan.changes[0].text.as_deref().unwrap();
        assert!(patch.contains("[0x0051] = function(original)"));
        assert!(patch.contains("[0x0052] = function(original)"));
        assert!(patch.contains("story.set_trigger(9)"));
    }

    #[test]
    fn warns_of_a_higher_patch_and_notes_warps() {
        let packs = Packs::new();
        packs.pack("top", "mods", 5);
        packs.file(
            "top/story/patches/top_900.lua",
            "return {\n  script = 900,\n  sections = {\n    [0x0051] = function(original)\n      return original()\n    end,\n  },\n}\n",
        );
        let vfs = packs.mount(&["base", "mine", "top"]);
        let patches = StoryPatches::load(&vfs);
        let mut draft = edited(&patches, &vfs);
        let body = draft.file.section_body_mut(0x51).unwrap();
        let mut warp = crate::edit::new_command(crate::commands::command("warp_to").unwrap());
        call_of_mut(&mut warp).unwrap().1[0] = Expr::Number(3);
        insert(body, &StmtPath::top(3), warp);
        let scripts = BTreeMap::from([(900, draft)]);
        let plan = plan_story_save(
            &vfs,
            &patches,
            target(&vfs, "mine"),
            &scripts,
            &BTreeMap::new(),
            "",
        );
        assert!(!plan.has_errors());
        assert!(
            matches!(&plan.problems[..], [StoryProblem::Warning(text)] if text.contains("top:story/patches/top_900"))
        );
        assert_eq!(plan.warps_changed, vec![900]);
    }

    #[test]
    fn a_patch_without_sections_is_removed() {
        let packs = Packs::new();
        packs.file(
            "mine/story/patches/mine_900.lua",
            "return {\n  script = 900,\n  sections = {\n    [0x0052] = function(original)\n      return original()\n    end,\n  },\n}\n",
        );
        let vfs = packs.mount(&["base", "mine"]);
        let patches = StoryPatches::load(&vfs);
        let base = starting_patch(&patches, "mine", 900);
        let mut file = base.clone();
        file.remove_section(0x52);
        let scripts = BTreeMap::from([(900, ScriptDraft { file, base })]);
        let plan = plan_story_save(
            &vfs,
            &patches,
            target(&vfs, "mine"),
            &scripts,
            &BTreeMap::new(),
            "",
        );
        assert_eq!(plan.changes.len(), 1);
        assert_eq!(plan.changes[0].text, None);
    }
}
