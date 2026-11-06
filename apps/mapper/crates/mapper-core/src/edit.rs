//! The edits of a map document and their history. Every change to the document is an [`Edit`]
//! that knows the values before and after it, so it can be undone and done again; a [`History`]
//! groups the edits of one gesture (a brush stroke, a drag) into one step.

use std::collections::BTreeMap;

use dwe_map::{Cell, MapDocument, Npc, Warp};

/// One cell's value before and after an edit.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CellChange {
    pub cell: Cell,
    pub before: u8,
    pub after: u8,
}

/// A change to a map document.
#[derive(Clone, Debug, PartialEq)]
pub enum Edit {
    /// Collision cells set to new values.
    Cells(Vec<CellChange>),
    /// An exit's record replaced (by its `exit`).
    Warp { before: Warp, after: Warp },
    /// An NPC slot replaced.
    Npc {
        slot: usize,
        before: Box<Npc>,
        after: Box<Npc>,
    },
}

impl Edit {
    /// The edit that undoes this one.
    pub fn inverse(&self) -> Edit {
        match self {
            Self::Cells(cells) => Self::Cells(
                cells
                    .iter()
                    .map(|change| CellChange {
                        cell: change.cell,
                        before: change.after,
                        after: change.before,
                    })
                    .collect(),
            ),
            Self::Warp { before, after } => Self::Warp {
                before: after.clone(),
                after: before.clone(),
            },
            Self::Npc {
                slot,
                before,
                after,
            } => Self::Npc {
                slot: *slot,
                before: after.clone(),
                after: before.clone(),
            },
        }
    }

    /// Puts the edit's "after" values in the document.
    pub fn apply(&self, document: &mut MapDocument) {
        match self {
            Self::Cells(cells) => {
                for change in cells {
                    document.collision.set(change.cell, change.after);
                }
            }
            Self::Warp { after, .. } => {
                if let Some(warp) = document
                    .warps
                    .iter_mut()
                    .find(|warp| warp.exit == after.exit)
                {
                    *warp = after.clone();
                }
            }
            Self::Npc { slot, after, .. } => {
                if let Some(npc) = document.npcs.get_mut(*slot) {
                    *npc = (**after).clone();
                }
            }
        }
    }

    /// Whether the edit changes nothing.
    pub fn is_empty(&self) -> bool {
        match self {
            Self::Cells(cells) => cells.iter().all(|change| change.before == change.after),
            Self::Warp { before, after } => before == after,
            Self::Npc { before, after, .. } => before == after,
        }
    }

    /// Folds a later edit of the same thing into this one, keeping this one's "before".
    fn absorb(&mut self, later: &Edit) -> bool {
        match (self, later) {
            (Self::Cells(cells), Self::Cells(more)) => {
                let mut by_cell: BTreeMap<(i32, i32), CellChange> = cells
                    .iter()
                    .map(|change| ((change.cell.row, change.cell.column), *change))
                    .collect();
                for change in more {
                    by_cell
                        .entry((change.cell.row, change.cell.column))
                        .and_modify(|known| known.after = change.after)
                        .or_insert(*change);
                }
                *cells = by_cell.into_values().collect();
                true
            }
            (Self::Warp { before, after }, Self::Warp { after: next, .. })
                if before.exit == next.exit =>
            {
                *after = next.clone();
                true
            }
            (
                Self::Npc { slot, after, .. },
                Self::Npc {
                    slot: next_slot,
                    after: next,
                    ..
                },
            ) if slot == next_slot => {
                *after = next.clone();
                true
            }
            _ => false,
        }
    }
}

/// The edits of one undo step.
#[derive(Clone, Debug, PartialEq)]
struct Step {
    id: u64,
    edits: Vec<Edit>,
}

/// The undo and redo stacks of one map.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct History {
    undo: Vec<Step>,
    redo: Vec<Step>,
    /// Whether the last step is still taking the edits of a gesture.
    open: bool,
    next_id: u64,
    /// The step that was on top when the map was last saved or opened (0: none).
    clean: u64,
}

impl History {
    /// Starts a step that takes every edit until [`History::end_group`].
    pub fn begin_group(&mut self) {
        self.open = false;
        self.push_step();
        self.open = true;
    }

    /// Closes the open step; an empty one goes away.
    pub fn end_group(&mut self) {
        self.open = false;
        if self.undo.last().is_some_and(|step| step.edits.is_empty()) {
            self.undo.pop();
        }
    }

    pub fn is_grouping(&self) -> bool {
        self.open
    }

    fn push_step(&mut self) {
        self.next_id += 1;
        self.undo.push(Step {
            id: self.next_id,
            edits: Vec::new(),
        });
    }

    /// Applies an edit to the document and records it: in the open step, or as a step of its own.
    /// An edit that changes nothing is left out. Recording drops the steps to redo.
    pub fn record(&mut self, document: &mut MapDocument, edit: Edit) -> bool {
        if edit.is_empty() {
            return false;
        }
        edit.apply(document);
        if !self.open {
            self.push_step();
        }
        self.redo.clear();
        let step = self.undo.last_mut().expect("a step was pushed");
        if !step.edits.last_mut().is_some_and(|last| last.absorb(&edit)) {
            step.edits.push(edit);
        }
        true
    }

    /// Undoes the last step; false when there is none.
    pub fn undo(&mut self, document: &mut MapDocument) -> bool {
        self.end_group();
        let Some(step) = self.undo.pop() else {
            return false;
        };
        for edit in step.edits.iter().rev() {
            edit.inverse().apply(document);
        }
        self.redo.push(step);
        true
    }

    /// Does again the last step undone; false when there is none.
    pub fn redo(&mut self, document: &mut MapDocument) -> bool {
        self.end_group();
        let Some(step) = self.redo.pop() else {
            return false;
        };
        for edit in &step.edits {
            edit.apply(document);
        }
        self.undo.push(step);
        true
    }

    pub fn can_undo(&self) -> bool {
        !self.undo.is_empty()
    }

    pub fn can_redo(&self) -> bool {
        !self.redo.is_empty()
    }

    fn top(&self) -> u64 {
        self.undo.last().map_or(0, |step| step.id)
    }

    /// Whether the document differs from when it was last saved or opened.
    pub fn is_dirty(&self) -> bool {
        self.top() != self.clean
    }

    /// The document as it is now is the saved one.
    pub fn mark_clean(&mut self) {
        self.end_group();
        self.clean = self.top();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn document() -> MapDocument {
        let mut rows = vec![".".repeat(100); 100];
        rows[0] = "#".repeat(100);
        let rows: String = rows.iter().map(|row| format!("  - \"{row}\"\n")).collect();
        let yaml = format!(
            "camera: {{viewpoint: {{x: 0, y: -3000, z: -4000}}, reference: {{x: 0, y: 0, z: 0}}, projection: 1000}}\n\
             tiles: {{width: 1, height: 1, rows: [\"0\"]}}\n\
             warps:\n- {{exit: 0, spawn: {{x: 0, y: 0, z: 0}}, rotation: 0, target_map: -1, target_map_id: null, target_exit: -1}}\n\
             npcs:\n- {{type_id: 3, follow_mode: 1, position: {{x: 1, y: 0, z: 2}}, rotation: {{x: 0, y: 0, z: 0}}, tracking_range: 300, script_id: 2}}\n\
             collision:\n  width: 100\n  height: 100\n  encoding: legend\n  legend:\n  - {{cell: \".\", value: 0}}\n  - {{cell: \"#\", value: 128}}\n  rows:\n{rows}"
        );
        MapDocument::parse("area_a", &yaml).unwrap()
    }

    fn paint(document: &MapDocument, cells: &[(i32, i32)], value: u8) -> Edit {
        Edit::Cells(
            cells
                .iter()
                .map(|&(column, row)| {
                    let cell = Cell::new(column, row);
                    CellChange {
                        cell,
                        before: document.collision.value(cell),
                        after: value,
                    }
                })
                .collect(),
        )
    }

    #[test]
    fn every_edit_undoes_and_redoes() {
        let original = document();
        let mut map = original.clone();
        let mut history = History::default();
        assert!(!history.is_dirty());
        let edit = paint(&map, &[(3, 4), (5, 6)], 51);
        assert!(history.record(&mut map, edit));
        let mut warp = map.warps[0].clone();
        warp.target_exit = 4;
        let edit = Edit::Warp {
            before: map.warps[0].clone(),
            after: warp,
        };
        history.record(&mut map, edit);
        let mut npc = map.npcs[0].clone();
        npc.position.x = 700;
        let edit = Edit::Npc {
            slot: 0,
            before: Box::new(map.npcs[0].clone()),
            after: Box::new(npc),
        };
        history.record(&mut map, edit);
        let edited = map.clone();
        assert!(history.is_dirty());
        assert_eq!(map.collision.value(Cell::new(5, 6)), 51);
        assert_eq!(map.warps[0].target_exit, 4);
        assert_eq!(map.npcs[0].position.x, 700);
        while history.undo(&mut map) {}
        assert_eq!(map, original);
        assert!(!history.is_dirty());
        while history.redo(&mut map) {}
        assert_eq!(map, edited);
        // An edit after an undo drops what was to redo.
        history.undo(&mut map);
        history.record(&mut map, paint(&original, &[(9, 9)], 128));
        assert!(!history.can_redo());
    }

    #[test]
    fn a_gesture_is_one_step_and_keeps_the_first_before() {
        let original = document();
        let mut map = original.clone();
        let mut history = History::default();
        history.begin_group();
        for value in [51, 52, 53] {
            let edit = paint(&map, &[(10, 10), (11, 10)], value);
            history.record(&mut map, edit);
        }
        let edit = paint(&map, &[(12, 10)], 53);
        history.record(&mut map, edit);
        history.end_group();
        assert_eq!(map.collision.value(Cell::new(10, 10)), 53);
        assert!(history.undo(&mut map));
        assert_eq!(map, original);
        assert!(!history.can_undo());
        // An empty gesture leaves no step; an edit that changes nothing is left out.
        history.begin_group();
        history.end_group();
        assert!(!history.can_undo());
        assert!(!history.record(&mut map, paint(&original, &[(0, 0)], 128)));
    }

    #[test]
    fn saving_marks_the_document_clean() {
        let mut map = document();
        let mut history = History::default();
        history.record(&mut map, paint(&document(), &[(1, 1)], 51));
        history.mark_clean();
        assert!(!history.is_dirty());
        history.undo(&mut map);
        assert!(history.is_dirty());
        history.redo(&mut map);
        assert!(!history.is_dirty());
    }
}
