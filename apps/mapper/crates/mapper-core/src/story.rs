//! The story of the map shown (docs/modulos/apps/mapper.md, "Eventos"): its events, the section
//! open in the section editor and the edits of its code, its dialogue lines, its doors and toilet
//! and its NPC slots, kept as drafts beside the map until a save writes them as a story patch of
//! the target pack ([`dwe_story::save`]).

use std::collections::BTreeMap;

use dwe_map::places::Places;
use dwe_map::table::{MapDoor, MapToilet};
use dwe_map::{MapDocument, Npc};
use dwe_pack::Vfs;
use dwe_story::edit::{self, StmtPath};
use dwe_story::events::MAP_HEAD_SCRIPT;
use dwe_story::file::{Section, SectionBody};
use dwe_story::needs::{Need, conditions, needs};
use dwe_story::save::starting_patch;
use dwe_story::script::{StoryPatches, read_module};
use dwe_story::texts::{ENGLISH, split_key};
use dwe_story::{
    Block, Event, EventKind, Expr, LoadedScript, MapEvents, ScriptDraft, Stmt, StmtKind, StoryFile,
    StoryFileSource, StoryTexts,
};
use glam::Vec2;

use crate::edit::Edit;
use crate::session::{Mapper, Message};
use crate::text;
use crate::tools::Selection;

/// The most NPC slots a map has (the map file holds at most 8 records).
pub const MOST_NPCS: usize = 8;
/// The pack name drafts start with when no target is chosen yet (a save names the file after
/// the target).
const NO_TARGET_PACK: &str = "mapper";

/// The story of the mounted content, read once a mount.
#[derive(Debug, Default)]
pub struct StoryContent {
    pub patches: StoryPatches,
    pub texts: StoryTexts,
    /// The modules read, by script (None: the stack has none).
    modules: BTreeMap<u16, Option<(StoryFileSource, StoryFile)>>,
}

impl StoryContent {
    pub fn load(vfs: &Vfs) -> Self {
        Self {
            patches: StoryPatches::load(vfs),
            texts: StoryTexts::new(ENGLISH),
            modules: BTreeMap::new(),
        }
    }

    /// Reads a script's module and its text table, once.
    fn read(&mut self, vfs: &Vfs, script: u16) -> Vec<String> {
        if self.modules.contains_key(&script) {
            return Vec::new();
        }
        let (module, problems) = read_module(vfs, script);
        self.modules.insert(script, module);
        self.texts.load_table(vfs, &text_table(script));
        problems
    }
}

/// The text table of a script's lines: `story_<nnn>`.
pub fn text_table(script: u16) -> String {
    format!("story_{script:03}")
}

/// The section open in the section editor.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct OpenSection {
    pub script: u16,
    pub section: u16,
    /// A routine of the file that runs the section, shown instead of the section's own code.
    pub routine: Option<String>,
    pub selected: Option<StmtPath>,
}

/// A change to the code shown in the section editor.
#[derive(Clone, Debug, PartialEq)]
pub enum StoryEdit {
    /// Puts a statement before the one at a path (after the last one when past it).
    Insert {
        at: StmtPath,
        stmt: Stmt,
    },
    Replace {
        at: StmtPath,
        stmt: Stmt,
    },
    Remove(StmtPath),
    /// Swaps a statement with the next one or the previous one.
    Move {
        at: StmtPath,
        later: bool,
    },
    /// Sets an arm's condition of the `if` at a path (or a loop's).
    SetCondition {
        at: StmtPath,
        arm: usize,
        condition: Expr,
    },
    /// The code as typed in the Lua editor.
    SetCode(String),
    /// Takes the section out of the draft: it runs as the packs have it.
    Revert,
}

/// What "Probar evento" does in a connected game.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct TestPlan {
    pub map: String,
    /// The story writes that get past the section's early ends.
    pub writes: Vec<Need>,
    /// The conditions none of the writes makes hold, as the editor says them.
    pub others: Vec<String>,
    /// Where the player goes (`place` operands), when the event has a place.
    pub place: Option<String>,
    /// Whether the event runs on the confirm button (a confirm cell or an NPC).
    pub press: bool,
}

impl Mapper {
    /// The pack drafts are for: the target's, or a placeholder.
    pub fn story_pack(&self) -> String {
        self.current_target()
            .map_or_else(|| NO_TARGET_PACK.to_owned(), |target| target.id)
    }

    /// The patches of the stack with the drafts of the map shown in their place.
    fn shown_patches(&self) -> StoryPatches {
        let (Some(content), Some(map)) = (&self.content, &self.map) else {
            return self.story.patches.clone();
        };
        let pack = self.story_pack();
        let mut patches = self.story.patches.clone();
        for draft in map.drafts.scripts.values() {
            patches = patches.with_draft(content.vfs(), &pack, &draft.file);
        }
        patches
    }

    /// The scripts the map shows or the editor opened, as they run with the drafts, and its
    /// events, built again after a change.
    pub(crate) fn refresh_story(&mut self) {
        let Some(content) = &self.content else { return };
        let Some(map) = &self.map else { return };
        let mut wanted: Vec<u16> = vec![MAP_HEAD_SCRIPT];
        wanted.extend(map.events.map_script);
        wanted.extend(map.drafts.scripts.keys().copied());
        wanted.extend(self.section.as_ref().map(|open| open.script));
        let vfs = content.vfs();
        let mut problems = Vec::new();
        for &script in &wanted {
            problems.extend(self.story.read(vfs, script));
        }
        let patches = self.shown_patches();
        let story = &mut self.story;
        let mut scripts = BTreeMap::new();
        let Some(map) = &mut self.map else { return };
        let events = MapEvents::build_with(
            |script| {
                problems.extend(story.read(vfs, script));
                let module = story.modules.get(&script).cloned().flatten();
                let loaded = LoadedScript::with_module(script, module, &patches);
                scripts.insert(script, loaded.clone());
                loaded
            },
            &content.table,
            &self.bands,
            &map.document,
            map.entry.as_ref(),
        );
        for script in wanted {
            scripts.entry(script).or_insert_with(|| {
                let module = story.modules.get(&script).cloned().flatten();
                LoadedScript::with_module(script, module, &patches)
            });
        }
        map.events = events;
        map.scripts = scripts;
        for problem in problems {
            self.messages.push(Message::Error(problem));
        }
    }

    /// A script as it runs with the drafts (the map's, the open one's and the drafted ones).
    pub fn shown_script(&self, script: u16) -> Option<&LoadedScript> {
        self.map.as_ref()?.scripts.get(&script)
    }

    /// The events of the map shown.
    pub fn events(&self) -> &[Event] {
        self.map.as_ref().map_or(&[], |map| &map.events.events)
    }

    /// Opens a section in the section editor (one no file has starts empty when edited).
    pub(crate) fn open_section(&mut self, script: u16, section: u16) {
        self.section = Some(OpenSection {
            script,
            section,
            routine: None,
            selected: None,
        });
        self.refresh_story();
    }

    /// The code the section editor shows: the open routine's or the section's.
    pub fn open_code(&self) -> Option<&Block> {
        let open = self.section.as_ref()?;
        let (_, file, block) = self.shown_script(open.script)?.section(open.section)?;
        match &open.routine {
            Some(name) => file.routine(name).map(|routine| &routine.body),
            None => Some(block),
        }
    }

    /// The file whose code the section editor shows.
    pub fn open_file(&self) -> Option<&StoryFile> {
        let open = self.section.as_ref()?;
        self.shown_script(open.script)?
            .section(open.section)
            .map(|(_, file, _)| file)
    }

    /// Who gives the open section: None for the module, else the patch (the draft says it is
    /// not saved).
    pub fn open_origin(&self) -> Option<String> {
        let open = self.section.as_ref()?;
        let map = self.map.as_ref()?;
        if map
            .drafts
            .scripts
            .get(&open.script)
            .is_some_and(|draft| draft.file.section(open.section).is_some())
        {
            return Some(format!("{} ({})", self.story_pack(), text::NOT_SAVED));
        }
        self.shown_script(open.script)?.patched_by(open.section)
    }

    /// Changes the code shown in the section editor, in the target pack's draft of its script.
    pub(crate) fn edit_story(&mut self, change: StoryEdit) {
        let Some(open) = self.section.clone() else {
            return;
        };
        let pack = self.story_pack();
        let Some(map) = &self.map else { return };
        let before = map.drafts.scripts.get(&open.script).cloned();
        let mut draft = before.clone().unwrap_or_else(|| {
            let base = starting_patch(&self.story.patches, &pack, open.script);
            ScriptDraft {
                file: base.clone(),
                base,
            }
        });
        if draft.file.section(open.section).is_none() {
            if change == StoryEdit::Revert {
                return;
            }
            let shown = self
                .shown_script(open.script)
                .and_then(|script| script.section(open.section))
                .map(|(_, file, _)| file.clone());
            match shown {
                Some(from) => {
                    if let Err(error) = draft.file.take_section(&from, open.section) {
                        self.messages.push(Message::Error(error.to_string()));
                        return;
                    }
                }
                None => draft.file.sections.push(Section {
                    id: open.section,
                    body: SectionBody::Function {
                        params: vec!["original".to_owned()],
                        body: vec![edit::end_script()],
                    },
                }),
            }
        }
        let selected = match apply_change(&mut draft.file, &open, &change) {
            Ok(selected) => selected,
            Err(error) => {
                self.messages.push(Message::Error(error));
                return;
            }
        };
        draft.file.drop_unreached_routines();
        draft.file.has_routines = !draft.file.routines.is_empty();
        let after = (draft.file != draft.base).then(|| Box::new(draft));
        if let Some(open) = &mut self.section {
            open.selected = selected;
            if change == StoryEdit::Revert {
                open.routine = None;
            }
        }
        self.record(Edit::Story {
            script: open.script,
            before: before.map(Box::new),
            after,
        });
    }

    /// A dialogue line as edited, or as the packs have it.
    pub fn text_of(&self, key: &str) -> Option<&str> {
        self.map
            .as_ref()
            .and_then(|map| map.drafts.texts.get(key))
            .map(String::as_str)
            .or_else(|| self.story.texts.text(key))
    }

    /// Sets a dialogue line (in every place its key is shown).
    pub(crate) fn set_text(&mut self, key: &str, line: &str) {
        let Some(map) = &self.map else { return };
        let before = map.drafts.texts.get(key).cloned();
        let after = (self.story.texts.text(key) != Some(line)).then(|| line.to_owned());
        self.record(Edit::Text {
            key: key.to_owned(),
            before,
            after,
        });
    }

    /// A key of a script's table that no line has yet.
    pub fn free_text_key(&self, script: u16) -> String {
        let table = text_table(script);
        let mut keys = self.story.texts.keys(&table);
        if let Some(map) = &self.map {
            keys.extend(map.drafts.texts.keys().cloned());
        }
        let next = keys
            .iter()
            .filter_map(|key| split_key(key).filter(|(owner, _)| *owner == table))
            .filter_map(|(_, inner)| inner.strip_prefix("text."))
            .filter_map(|id| u32::from_str_radix(id, 16).ok())
            .max()
            .map_or(0, |max| max + 1);
        format!("{table}.text.{next:04x}")
    }

    /// The doors and the toilet of the map shown, as edited.
    pub fn places(&self) -> Places {
        self.map.as_ref().map_or_else(Places::default, |map| {
            map.drafts
                .places
                .clone()
                .unwrap_or_else(|| map.places.clone())
        })
    }

    pub(crate) fn set_places(&mut self, places: Places) {
        let Some(map) = &self.map else { return };
        let before = map.drafts.places.clone().map(Box::new);
        let after = (places != map.places).then(|| Box::new(places));
        self.record(Edit::Places { before, after });
    }

    pub(crate) fn set_door(&mut self, slot: usize, door: MapDoor) {
        let mut places = self.places();
        let mut doors = places.door_slots();
        let Some(known) = doors.get_mut(slot) else {
            return;
        };
        *known = door;
        places.doors = doors;
        self.set_places(places);
    }

    pub(crate) fn set_toilet(&mut self, toilet: Option<MapToilet>) {
        let mut places = self.places();
        places.toilet = toilet.map(|toilet| MapToilet {
            index: places.toilet.as_ref().map_or(0, |known| known.index),
            ..toilet
        });
        self.set_places(places);
    }

    /// An NPC record to copy for a new slot: the one selected, the map's last, or the first of
    /// the first map that has one.
    fn npc_template(&self) -> Option<Npc> {
        let map = self.map.as_ref()?;
        let selected = self
            .selection
            .and_then(Selection::npc_slot)
            .and_then(|slot| map.document.npcs.get(slot));
        if let Some(npc) = selected.or(map.document.npcs.last()) {
            return Some(npc.clone());
        }
        let content = self.content.as_ref()?;
        content
            .table
            .maps
            .iter()
            .filter(|entry| content.has_map(&entry.id))
            .find_map(|entry| {
                let document = dwe_map::load_map(content.vfs(), &entry.id).ok()?;
                document.npcs.first().cloned()
            })
    }

    /// Adds an NPC slot (a copy of the selected NPC, or of the map's last) on the ground under a
    /// canvas point, and makes the map's entry section spawn it. The map then saves whole.
    pub(crate) fn add_npc(&mut self, at: Option<Vec2>) {
        let Some(map) = &self.map else { return };
        if map.document.npcs.len() >= MOST_NPCS {
            self.messages
                .push(Message::Error(text::TOO_MANY_NPCS.to_owned()));
            return;
        }
        let Some(mut npc) = self.npc_template() else {
            self.messages
                .push(Message::Error(text::NO_NPC_TEMPLATE.to_owned()));
            return;
        };
        if let Some(point) = at.and_then(|at| self.ground_under(at, npc.position)) {
            npc.position = point;
        }
        npc.waypoints.clear();
        npc.ai_sections = vec![-1; dwe_map::save::NPC_PATH_PARTS];
        let Some(map) = &self.map else { return };
        let before = map.document.npcs.clone();
        let mut after = before.clone();
        after.push(npc.clone());
        let slot = after.len() - 1;
        if let Some(map) = &mut self.map {
            map.history.begin_group();
        }
        self.record(Edit::NpcList { before, after });
        self.edit_entry_section(|block| {
            let at = top_level_end(block);
            let species = Expr::Number(i64::from(npc.type_id));
            let load = Stmt::new(StmtKind::Call(Expr::story_call(
                "load_species_model",
                vec![species.clone()],
            )));
            let spawn = Stmt::with_comment(
                StmtKind::Call(Expr::story_call(
                    "spawn_npc",
                    vec![species, Expr::Number(slot as i64), Expr::Number(1)],
                )),
                "species, slot, autotalk",
            );
            block.insert(at, spawn);
            block.insert(at, load);
        });
        if let Some(map) = &mut self.map {
            map.history.end_group();
        }
        self.selection = Some(Selection::Npc(slot));
    }

    /// Removes the last NPC slot (the others keep their numbers, which the story names) and the
    /// spawns of that slot in the map's entry section.
    pub(crate) fn remove_npc(&mut self) {
        let Some(map) = &self.map else { return };
        let before = map.document.npcs.clone();
        let Some(slot) = before.len().checked_sub(1) else {
            return;
        };
        let mut after = before.clone();
        after.pop();
        if let Some(map) = &mut self.map {
            map.history.begin_group();
        }
        self.record(Edit::NpcList { before, after });
        let spawns = |block: &Block| has_spawn(block, slot);
        if self
            .entry_section_code()
            .is_some_and(|block| spawns(&block))
        {
            self.edit_entry_section(|block| remove_spawns(block, slot));
        }
        if let Some(map) = &mut self.map {
            map.history.end_group();
        }
        if self.selection.and_then(Selection::npc_slot) == Some(slot) {
            self.selection = None;
        }
    }

    /// The code of the map's entry section (script 0, the map's index).
    fn entry_section_code(&self) -> Option<Block> {
        let section = self.entry_section()?;
        self.shown_script(MAP_HEAD_SCRIPT)?
            .section(section)
            .map(|(_, _, block)| block.clone())
    }

    fn entry_section(&self) -> Option<u16> {
        let entry = self.map.as_ref()?.entry.as_ref()?;
        u16::try_from(entry.index)
            .ok()
            .filter(|_| entry.original_index)
    }

    /// Changes the map's entry section through the section editor's path (one undo step with
    /// the edit around it), keeping what the editor had open.
    fn edit_entry_section(&mut self, change: impl FnOnce(&mut Block)) {
        let Some(section) = self.entry_section() else {
            self.messages
                .push(Message::Error(text::NO_ENTRY_SECTION.to_owned()));
            return;
        };
        let Some(mut code) = self.entry_section_code() else {
            return;
        };
        change(&mut code);
        let open = self.section.take();
        self.section = Some(OpenSection {
            script: MAP_HEAD_SCRIPT,
            section,
            routine: None,
            selected: None,
        });
        self.refresh_story();
        let code = dwe_story::write::block_text(&code);
        self.edit_story(StoryEdit::SetCode(code));
        self.section = open;
        self.refresh_story();
    }

    /// What "Probar evento" does for an event of the map shown.
    pub fn test_plan(&self, event: &Event) -> TestPlan {
        let mut plan = TestPlan {
            map: self
                .map
                .as_ref()
                .map(|map| map.document.id.clone())
                .unwrap_or_default(),
            ..TestPlan::default()
        };
        if let Some((_, file, block)) = self
            .shown_script(event.script)
            .and_then(|script| script.section(event.section))
        {
            let found = needs(&conditions(file, block));
            plan.writes = found.writes;
            if let Some(content) = &self.content {
                let names = dwe_story::describe::Names {
                    story: &self.story_names,
                    texts: &self.story.texts,
                    maps: &content.table,
                };
                plan.others = found
                    .others
                    .iter()
                    .map(|condition| names.condition(condition))
                    .collect();
            }
        }
        let document = self.map.as_ref().map(|map| &map.document);
        match event.kind {
            EventKind::Step | EventKind::Confirm => {
                plan.place = event.centre_cell().map(|cell| place_on_cell(cell, 0));
                plan.press = event.kind == EventKind::Confirm;
            }
            EventKind::Talk { slot } => {
                plan.place = document
                    .and_then(|document| document.npcs.get(slot))
                    .map(place_by_npc);
                plan.press = true;
            }
            EventKind::MapEntry | EventKind::MapLoaded => {}
        }
        plan
    }
}

fn apply_change(
    file: &mut StoryFile,
    open: &OpenSection,
    change: &StoryEdit,
) -> Result<Option<StmtPath>, String> {
    if *change == StoryEdit::Revert {
        file.remove_section(open.section);
        return Ok(None);
    }
    let block = match &open.routine {
        Some(name) => file.routine_mut(name).map(|routine| &mut routine.body),
        None => file.section_body_mut(open.section),
    }
    .ok_or_else(|| text::NO_CODE.to_owned())?;
    let selected = match change {
        StoryEdit::Insert { at, stmt } => edit::insert(block, at, stmt.clone()).then(|| at.clone()),
        StoryEdit::Replace { at, stmt } => {
            edit::replace(block, at, stmt.clone()).then(|| at.clone())
        }
        StoryEdit::Remove(at) => edit::remove(block, at).map(|_| None).unwrap_or(None),
        StoryEdit::Move { at, later } => edit::move_stmt(block, at, *later),
        StoryEdit::SetCondition { at, arm, condition } => {
            edit::set_condition(block, at, *arm, condition.clone()).then(|| at.clone())
        }
        StoryEdit::SetCode(code) => {
            *block = dwe_story::parse::parse_chunk(code).map_err(|error| error.to_string())?;
            None
        }
        StoryEdit::Revert => None,
    };
    Ok(selected)
}

/// Where a statement goes to run before a block ends: before its last top-level `return`.
fn top_level_end(block: &Block) -> usize {
    block
        .iter()
        .rposition(|stmt| matches!(stmt.kind, StmtKind::Return(_)))
        .unwrap_or(block.len())
}

fn spawn_slot(stmt: &Stmt) -> Option<i64> {
    match &stmt.kind {
        StmtKind::Call(call) => match call.story_call_name()? {
            ("spawn_npc", [_, slot, ..]) => slot.as_number(),
            _ => None,
        },
        _ => None,
    }
}

fn has_spawn(block: &Block, slot: usize) -> bool {
    block.iter().any(|stmt| {
        spawn_slot(stmt) == Some(slot as i64)
            || edit::children(stmt)
                .into_iter()
                .any(|child| has_spawn(child, slot))
    })
}

fn remove_spawns(block: &mut Block, slot: usize) {
    block.retain(|stmt| spawn_slot(stmt) != Some(slot as i64));
    for stmt in block.iter_mut() {
        match &mut stmt.kind {
            StmtKind::If { arms, otherwise } => {
                for (_, body) in arms.iter_mut() {
                    remove_spawns(body, slot);
                }
                if let Some(body) = otherwise {
                    remove_spawns(body, slot);
                }
            }
            StmtKind::While { body, .. } | StmtKind::Do(body) => remove_spawns(body, slot),
            _ => {}
        }
    }
}

/// The `place` operands that put the player in the middle of a cell.
fn place_on_cell(cell: dwe_map::Cell, rotation: i32) -> String {
    crate::live::place_player(dwe_map::world::cell_center(cell), rotation)
}

/// The `place` operands that put the player in front of an NPC (where it faces), facing it.
fn place_by_npc(npc: &Npc) -> String {
    use dwe_map::world;
    let at = world::to_world(npc.position.into());
    let ahead = world::forward_of_yaw(world::yaw_from_original(npc.rotation.y));
    let player = at + ahead * NPC_TALK_DISTANCE;
    crate::live::place_player(player, world::rotation_facing(at - player))
}

/// How far in front of an NPC "Probar evento" puts the player, in world units.
const NPC_TALK_DISTANCE: f32 = 150.0;

/// Whether a map's document and drafts change the NPC list (the map then saves whole: patches
/// name NPCs by place).
pub fn npc_count_changed(loaded: &MapDocument, document: &MapDocument) -> bool {
    loaded.npcs.len() != document.npcs.len()
}
