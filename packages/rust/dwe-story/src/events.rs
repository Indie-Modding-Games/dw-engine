//! What can run a story section on a map (docs/architecture/interprete-historia.md §Secciones y
//! disparadores): entering it (`MAPHEAD`, script 0, section = the map's original index, whose
//! `set_script` names the map's script, then that script's `0xFE`), stepping on a cell of the step
//! band or pressing confirm on one of the confirm band (the map's script, section = the cell's
//! value; packages/game/field/src/field.cpp: checkTriggers, checkConfirm) and talking to an NPC
//! (section = its `script_id`). For each, the map changes its code makes (`warp_to`) with the
//! conditions around them, read from the Lua as the game runs it (patches applied).

use std::collections::{BTreeMap, BTreeSet};

use dwe_map::{Cell, MapDocument, MapEntry, MapTable, TriggerBand, TriggerBands};
use dwe_pack::Vfs;

use crate::ast::{Block, Expr, StmtKind};
use crate::file::StoryFile;
use crate::script::{LoadedScript, StoryPatches};
use crate::write::expr_text;

/// The script the game loads first on every map change (`MAPHEAD`).
pub const MAP_HEAD_SCRIPT: u16 = 0;
/// The section of the map's script that runs once the map is loaded.
pub const MAP_LOADED_SECTION: u16 = 0xFE;
/// A `warp_to` section operand that runs nothing.
pub const NO_SECTION: i64 = 255;

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum EventKind {
    /// MAPHEAD's section for the map.
    MapEntry,
    /// The map script's 0xFE.
    MapLoaded,
    /// Cells of the step band with this section's value.
    Step,
    Confirm,
    /// The NPC in this slot.
    Talk {
        slot: usize,
    },
}

/// A `warp_to` (or `jump_to_file`) a section's code reaches, with the conditions on its way.
#[derive(Clone, Debug, PartialEq)]
pub struct Jump {
    /// `warp_to`: the map's original index, the exit and the section that runs after (255: none).
    /// `jump_to_file`: the script and the section, with `to_map` None.
    pub to_map: Option<String>,
    pub map_index: i64,
    pub exit: i64,
    pub section: i64,
    pub file_jump: bool,
    /// The conditions that hold on the way, as Lua (`not story.trigger(4)` for an else).
    pub guards: Vec<Expr>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Event {
    pub kind: EventKind,
    pub script: u16,
    pub section: u16,
    /// The cells that run it (step and confirm).
    pub cells: Vec<Cell>,
    /// Whether the script has the section (a cell or an NPC may name one it has not).
    pub exists: bool,
    pub patched_by: Option<String>,
    pub jumps: Vec<Jump>,
}

impl Event {
    /// Where its label goes on the canvas: the middle of its cells.
    pub fn centre_cell(&self) -> Option<Cell> {
        let count = self.cells.len() as i32;
        if count == 0 {
            return None;
        }
        let (columns, rows) = self
            .cells
            .iter()
            .fold((0, 0), |(c, r), cell| (c + cell.column, r + cell.row));
        let centre = Cell::new(columns / count, rows / count);
        // The cell of the group nearest the average, so the label sits on the group.
        self.cells
            .iter()
            .min_by_key(|cell| {
                (cell.column - centre.column).pow(2) + (cell.row - centre.row).pow(2)
            })
            .copied()
    }
}

/// The events of one map.
#[derive(Clone, Debug, Default)]
pub struct MapEvents {
    /// The script `set_script` loads for the map (the first one MAPHEAD's section names).
    pub map_script: Option<u16>,
    /// Other scripts MAPHEAD's section may load on other paths.
    pub other_scripts: Vec<u16>,
    pub events: Vec<Event>,
    pub map_head: LoadedScript,
    pub script: Option<LoadedScript>,
    pub problems: Vec<String>,
}

impl MapEvents {
    /// Lists a map's events from its document (as edited) and the story of the packs.
    pub fn build(
        vfs: &Vfs,
        patches: &StoryPatches,
        table: &MapTable,
        bands: &TriggerBands,
        document: &MapDocument,
        entry: Option<&MapEntry>,
    ) -> Self {
        Self::build_with(
            |script| LoadedScript::load(vfs, patches, script),
            table,
            bands,
            document,
            entry,
        )
    }

    /// The same with the scripts from somewhere else (an editor's, with its drafts).
    pub fn build_with(
        mut load: impl FnMut(u16) -> LoadedScript,
        table: &MapTable,
        bands: &TriggerBands,
        document: &MapDocument,
        entry: Option<&MapEntry>,
    ) -> Self {
        let map_head = load(MAP_HEAD_SCRIPT);
        let mut events = Self {
            problems: map_head.problems.clone(),
            ..Self::default()
        };
        let head_section = entry
            .filter(|entry| entry.original_index)
            .and_then(|entry| u16::try_from(entry.index).ok());
        if let Some(section) = head_section {
            let scripts = loaded_scripts(&map_head, section, entry.map(|e| e.index));
            events.map_script = scripts.first().copied();
            events.other_scripts = scripts.iter().skip(1).copied().collect();
            events.events.push(event_of(
                &map_head,
                table,
                EventKind::MapEntry,
                section,
                Vec::new(),
            ));
        }
        events.map_head = map_head;
        let Some(script) = events.map_script else {
            return events;
        };
        let loaded = load(script);
        events.problems.extend(loaded.problems.iter().cloned());
        if loaded.origin(MAP_LOADED_SECTION).is_some() {
            events.events.push(event_of(
                &loaded,
                table,
                EventKind::MapLoaded,
                MAP_LOADED_SECTION,
                Vec::new(),
            ));
        }
        let mut cells: BTreeMap<(EventKind, u8), Vec<Cell>> = BTreeMap::new();
        for (cell, value) in document.collision.cells() {
            let kind = match bands.band(value) {
                TriggerBand::Step => EventKind::Step,
                TriggerBand::Confirm => EventKind::Confirm,
                _ => continue,
            };
            cells.entry((kind, value)).or_default().push(cell);
        }
        for ((kind, value), cells) in cells {
            events
                .events
                .push(event_of(&loaded, table, kind, u16::from(value), cells));
        }
        for (slot, npc) in document.npcs.iter().enumerate() {
            let Ok(section) = u16::try_from(npc.script_id) else {
                continue;
            };
            events.events.push(event_of(
                &loaded,
                table,
                EventKind::Talk { slot },
                section,
                Vec::new(),
            ));
        }
        events.script = Some(loaded);
        events
    }

    /// The event a cell runs.
    pub fn at_cell(&self, cell: Cell) -> Option<&Event> {
        self.events.iter().find(|event| event.cells.contains(&cell))
    }
}

fn event_of(
    script: &LoadedScript,
    table: &MapTable,
    kind: EventKind,
    section: u16,
    cells: Vec<Cell>,
) -> Event {
    let found = script.section(section);
    Event {
        kind,
        script: script.script,
        section,
        cells,
        exists: found.is_some(),
        patched_by: script.patched_by(section),
        jumps: found
            .map(|(_, file, block)| jumps_of(file, block, table))
            .unwrap_or_default(),
    }
}

/// The scripts `set_script` loads in a MAPHEAD section for a map (by its index).
fn loaded_scripts(map_head: &LoadedScript, section: u16, map_index: Option<i32>) -> Vec<u16> {
    let Some((_, file, block)) = map_head.section(section) else {
        return Vec::new();
    };
    let mut scripts = Vec::new();
    for_each_reached_call(file, block, &mut |name, arguments| {
        if name != "set_script" {
            return;
        }
        let script = arguments.first().and_then(Expr::as_number);
        let map = arguments.get(1).and_then(Expr::as_number);
        if let Some(script) = script.and_then(|s| u16::try_from(s).ok())
            && map_index.is_none_or(|index| map.is_none_or(|m| m == i64::from(index)))
            && !scripts.contains(&script)
        {
            scripts.push(script);
        }
    });
    scripts
}

/// Calls `visit` with each story call a block runs, following the routines it calls.
fn for_each_reached_call(file: &StoryFile, block: &Block, visit: &mut impl FnMut(&str, &[Expr])) {
    let mut seen = BTreeSet::new();
    let mut pending = vec![block];
    while let Some(block) = pending.pop() {
        crate::ast::walk_exprs(block, &mut |expr| {
            if let Some((name, arguments)) = expr.story_call_name() {
                visit(name, arguments);
            }
            if let Some(routine) = expr.routine_call_name()
                && seen.insert(routine.to_owned())
                && let Some(routine) = file.routine(routine)
            {
                pending.push(&routine.body);
            }
        });
    }
}

/// The map changes and script jumps a section's code reaches, with the conditions on the way.
pub fn jumps_of(file: &StoryFile, block: &Block, table: &MapTable) -> Vec<Jump> {
    let mut jumps = Vec::new();
    let mut seen = BTreeSet::new();
    walk_jumps(file, block, table, &mut Vec::new(), &mut seen, &mut jumps);
    jumps
}

fn walk_jumps(
    file: &StoryFile,
    block: &Block,
    table: &MapTable,
    guards: &mut Vec<Expr>,
    seen: &mut BTreeSet<String>,
    jumps: &mut Vec<Jump>,
) {
    for stmt in block {
        match &stmt.kind {
            StmtKind::If { arms, otherwise } => {
                for (condition, _) in arms {
                    follow_routine(file, condition, table, guards, seen, jumps);
                }
                let depth = guards.len();
                for (condition, body) in arms {
                    guards.push(condition.clone());
                    walk_jumps(file, body, table, guards, seen, jumps);
                    guards.pop();
                    guards.push(negate(condition));
                }
                if let Some(body) = otherwise {
                    walk_jumps(file, body, table, guards, seen, jumps);
                }
                // When every arm ends the code, what follows runs only when none held.
                let arms_end = otherwise.is_none() && arms.iter().all(|(_, body)| ends(body));
                if !arms_end {
                    guards.truncate(depth);
                }
            }
            StmtKind::While { body, .. } | StmtKind::Do(body) => {
                walk_jumps(file, body, table, guards, seen, jumps)
            }
            StmtKind::Return(values) => {
                for value in values {
                    jump_of(value, table, guards, jumps);
                    follow_routine(file, value, table, guards, seen, jumps);
                }
            }
            StmtKind::Call(call) => {
                jump_of(call, table, guards, jumps);
                follow_routine(file, call, table, guards, seen, jumps);
            }
            _ => {}
        }
    }
}

/// Whether a block's last statement leaves it (a return).
fn ends(block: &Block) -> bool {
    block
        .iter()
        .rev()
        .find(|stmt| !matches!(stmt.kind, StmtKind::Comment(_)))
        .is_some_and(|stmt| matches!(stmt.kind, StmtKind::Return(_)))
}

/// Follows a `routines.<name>()` in an expression into its body, once.
fn follow_routine(
    file: &StoryFile,
    expr: &Expr,
    table: &MapTable,
    guards: &mut Vec<Expr>,
    seen: &mut BTreeSet<String>,
    jumps: &mut Vec<Jump>,
) {
    let mut called = Vec::new();
    crate::ast::walk_exprs(
        &vec![crate::ast::Stmt::new(StmtKind::Call(expr.clone()))],
        &mut |e| {
            if let Some(name) = e.routine_call_name() {
                called.push(name.to_owned());
            }
        },
    );
    for name in called {
        if seen.insert(name.clone())
            && let Some(routine) = file.routine(&name)
        {
            walk_jumps(file, &routine.body, table, guards, seen, jumps);
        }
    }
}

fn jump_of(expr: &Expr, table: &MapTable, guards: &[Expr], jumps: &mut Vec<Jump>) {
    let Some((name, arguments)) = expr.story_call_name() else {
        return;
    };
    let number = |index: usize| arguments.get(index).and_then(Expr::as_number).unwrap_or(-1);
    match name {
        "warp_to" => jumps.push(Jump {
            to_map: i32::try_from(number(0))
                .ok()
                .and_then(|index| table.find_by_original_index(index))
                .map(|entry| entry.id.clone()),
            map_index: number(0),
            exit: number(1),
            section: number(2),
            file_jump: false,
            guards: guards.to_vec(),
        }),
        "jump_to_file" => jumps.push(Jump {
            to_map: None,
            map_index: number(0),
            exit: -1,
            section: number(1),
            file_jump: true,
            guards: guards.to_vec(),
        }),
        _ => {}
    }
}

/// The condition that holds when one does not.
pub fn negate(condition: &Expr) -> Expr {
    match condition.unparen() {
        Expr::Not(inner) => inner.as_ref().clone(),
        Expr::Binary(op, left, right) if op.is_comparison() => Expr::Binary(
            op.negated().expect("a comparison"),
            left.clone(),
            right.clone(),
        ),
        other @ (Expr::Call(..) | Expr::Name(_) | Expr::Field(..)) => {
            Expr::Not(Box::new(other.clone()))
        }
        other => Expr::Not(Box::new(Expr::Paren(Box::new(other.clone())))),
    }
}

/// The guards of a jump as one line of Lua (for a tooltip).
pub fn guards_text(guards: &[Expr]) -> String {
    guards
        .iter()
        .map(expr_text)
        .collect::<Vec<_>>()
        .join(" and ")
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn finds_warps_with_their_guards() {
        let file = StoryFile::parse(
            "local routines = {}\nfunction routines.at_0001()\n  if story.trigger(4) then\n    return story.warp_to(3, 1, 255)\n  elseif story.var(2) >= 3 then\n    return routines.at_0002()\n  end\n  return story.jump_to_file(9, 81)\nend\nfunction routines.at_0002()\n  return story.warp_to(5, 0, 81)\nend\nreturn { script = 1, sections = { [0x51] = routines.at_0001 } }\n",
        )
        .unwrap();
        let block = file.section_block(0x51).unwrap();
        let jumps = jumps_of(&file, block, &MapTable::default());
        assert_eq!(jumps.len(), 3);
        assert_eq!(guards_text(&jumps[0].guards), "story.trigger(4)");
        assert_eq!(jumps[1].map_index, 5);
        assert_eq!(
            guards_text(&jumps[1].guards),
            "not story.trigger(4) and story.var(2) >= 3"
        );
        assert!(jumps[2].file_jump);
        assert_eq!(
            guards_text(&jumps[2].guards),
            "not story.trigger(4) and story.var(2) < 3"
        );
    }
}
