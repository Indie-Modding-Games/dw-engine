//! The editing tools: what the pointer does on the canvas, what is selected, the value the
//! collision tools paint, and the edits of an NPC's path that keep `ai_sections` and `waypoints`
//! in step.

use dwe_map::collision::{BLOCKED_BIT, FREE};
use dwe_map::document::RawVec3;
use dwe_map::save::NPC_PATH_PARTS;
use dwe_map::{Cell, Npc, TriggerBands};
use glam::Vec2;

/// What the pointer does on the canvas.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum Tool {
    /// Selects and drags exits' arrivals, NPCs and their path points.
    #[default]
    Select,
    /// Paints the value on a square of cells under the pointer.
    Brush,
    /// Paints the value on the rectangle dragged.
    Rectangle,
    /// Paints the value on the cells joined to the one clicked that share its value.
    Fill,
    /// Takes the value of the cell clicked.
    Eyedropper,
}

impl Tool {
    pub const ALL: [Tool; 5] = [
        Tool::Select,
        Tool::Brush,
        Tool::Rectangle,
        Tool::Fill,
        Tool::Eyedropper,
    ];

    /// Whether the tool works on the collision grid.
    pub fn paints(self) -> bool {
        matches!(self, Self::Brush | Self::Rectangle | Self::Fill)
    }
}

/// The brush sizes, in cells per side.
pub const BRUSH_SIZES: [i32; 3] = [1, 3, 5];

/// The keys held with a pointer action.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Modifiers {
    pub shift: bool,
    pub alt: bool,
    pub ctrl: bool,
}

/// What is selected on the map.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Selection {
    /// An exit, by its `exit`.
    Exit(i32),
    /// An NPC slot.
    Npc(usize),
    /// A part of an NPC's path (its place in `ai_sections` / `waypoints`).
    Waypoint { slot: usize, part: usize },
    /// A door slot of the map's set.
    Door(usize),
    /// One of the toilet's two points.
    Toilet(usize),
}

impl Selection {
    pub fn npc_slot(self) -> Option<usize> {
        match self {
            Self::Npc(slot) | Self::Waypoint { slot, .. } => Some(slot),
            Self::Exit(_) | Self::Door(_) | Self::Toilet(_) => None,
        }
    }
}

/// What a drag on the canvas is doing.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Drag {
    /// A brush stroke; the cell painted last, to join the next one with a line.
    Stroke { last: Cell },
    /// A rectangle from its first corner to the cell under the pointer.
    Rectangle { from: Cell, to: Cell },
    /// Moves the selection along the ground at its own height.
    Move(Selection),
    /// Turns the selection to face the pointer.
    Turn(Selection),
    /// Pans the view; the pointer's last canvas point.
    Pan { last: Vec2 },
}

/// The cells a brush of `size` covers around a cell.
pub fn brush_cells(center: Cell, size: i32) -> Vec<Cell> {
    let reach = size / 2;
    let mut cells = Vec::new();
    for row in center.row - reach..=center.row + reach {
        for column in center.column - reach..=center.column + reach {
            let cell = Cell::new(column, row);
            if cell.is_inside() {
                cells.push(cell);
            }
        }
    }
    cells
}

/// The cells on the line between two cells, both included (Bresenham).
pub fn line_cells(from: Cell, to: Cell) -> Vec<Cell> {
    let (mut x, mut y) = (from.column, from.row);
    let dx = (to.column - x).abs();
    let dy = -(to.row - y).abs();
    let step_x = if x < to.column { 1 } else { -1 };
    let step_y = if y < to.row { 1 } else { -1 };
    let mut error = dx + dy;
    let mut cells = vec![Cell::new(x, y)];
    while (x, y) != (to.column, to.row) {
        let doubled = 2 * error;
        if doubled >= dy {
            error += dy;
            x += step_x;
        }
        if doubled <= dx {
            error += dx;
            y += step_y;
        }
        cells.push(Cell::new(x, y));
    }
    cells
}

/// The cells of the rectangle with these two corners.
pub fn rectangle_cells(from: Cell, to: Cell) -> Vec<Cell> {
    let (left, right) = (from.column.min(to.column), from.column.max(to.column));
    let (top, bottom) = (from.row.min(to.row), from.row.max(to.row));
    (top..=bottom)
        .flat_map(|row| (left..=right).map(move |column| Cell::new(column, row)))
        .filter(|cell| cell.is_inside())
        .collect()
}

/// The cells joined to `start` (side by side) that have its value.
pub fn fill_cells(value_of: impl Fn(Cell) -> u8, start: Cell) -> Vec<Cell> {
    if !start.is_inside() {
        return Vec::new();
    }
    let wanted = value_of(start);
    let size = dwe_map::world::GRID_SIZE;
    let mut seen = vec![false; (size * size) as usize];
    let mut pending = vec![start];
    let mut cells = Vec::new();
    while let Some(cell) = pending.pop() {
        if !cell.is_inside() {
            continue;
        }
        let index = (cell.row * size + cell.column) as usize;
        if seen[index] || value_of(cell) != wanted {
            continue;
        }
        seen[index] = true;
        cells.push(cell);
        for (column, row) in [(1, 0), (-1, 0), (0, 1), (0, -1)] {
            pending.push(Cell::new(cell.column + column, cell.row + row));
        }
    }
    cells
}

/// A group of values the collision tools paint, as the picker shows them.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ValueGroup {
    Wall,
    Free,
    /// Step triggers: run a script section when stepped on.
    Step(Vec<u8>),
    /// Confirm triggers: run a script section when confirm is pressed.
    Confirm(Vec<u8>),
    /// An exit's trigger.
    Exit {
        exit: u8,
        value: u8,
    },
    Toilet(u8),
}

/// The values to paint, grouped by the bands of `field/triggers`.
pub fn value_groups(bands: &TriggerBands) -> Vec<ValueGroup> {
    let mut groups = vec![
        ValueGroup::Wall,
        ValueGroup::Free,
        ValueGroup::Step(bands.step.clone().collect()),
        ValueGroup::Confirm(bands.confirm.clone().collect()),
    ];
    groups.extend((0..bands.exit_count).filter_map(|exit| {
        Some(ValueGroup::Exit {
            exit,
            value: bands.exit_trigger(exit)?,
        })
    }));
    groups.push(ValueGroup::Toilet(bands.toilet));
    groups
}

/// The value a wall is painted with.
pub const WALL: u8 = BLOCKED_BIT;
/// The value of a free cell.
pub const FLOOR: u8 = FREE;

/// The kinds of a path part (`ai_sections`).
pub const PART_WAIT: i32 = 0;
pub const PART_WALK: i32 = 1;
pub const PART_RUN: i32 = 2;
pub const PART_END: i32 = -1;

/// How many parts an NPC's path has: those before the first end mark.
pub fn path_length(npc: &Npc) -> usize {
    npc.ai_sections
        .iter()
        .position(|&part| part == PART_END)
        .unwrap_or(npc.ai_sections.len())
}

/// Pads `ai_sections` with end marks to its fixed size and `waypoints` with zeros to cover the
/// path, so each part has its point.
fn normalize(npc: &mut Npc) {
    let length = path_length(npc);
    if npc.ai_sections.len() < NPC_PATH_PARTS {
        npc.ai_sections.resize(NPC_PATH_PARTS, PART_END);
    }
    if npc.waypoints.len() < length {
        npc.waypoints.resize(length, RawVec3::default());
    }
}

/// Adds a walk to `point` at the end of the path; None when the path is full.
pub fn add_part(npc: &Npc, point: RawVec3) -> Option<Npc> {
    let length = path_length(npc);
    if length >= NPC_PATH_PARTS {
        return None;
    }
    let mut npc = npc.clone();
    normalize(&mut npc);
    npc.ai_sections[length] = PART_WALK;
    npc.waypoints.truncate(length);
    npc.waypoints.push(point);
    Some(npc)
}

/// Removes a part of the path; the later ones move up and an end mark fills the last place.
pub fn remove_part(npc: &Npc, part: usize) -> Option<Npc> {
    if part >= path_length(npc) {
        return None;
    }
    let mut npc = npc.clone();
    normalize(&mut npc);
    npc.ai_sections.remove(part);
    npc.ai_sections.push(PART_END);
    npc.waypoints.remove(part);
    Some(npc)
}

/// Swaps a part of the path with the next (`later`) or the previous one.
pub fn move_part(npc: &Npc, part: usize, later: bool) -> Option<Npc> {
    let length = path_length(npc);
    let other = if later {
        part + 1
    } else {
        part.checked_sub(1)?
    };
    if part >= length || other >= length {
        return None;
    }
    let mut npc = npc.clone();
    normalize(&mut npc);
    npc.ai_sections.swap(part, other);
    npc.waypoints.swap(part, other);
    Some(npc)
}

/// Gives a part of the path a kind (wait, walk, run).
pub fn set_part_kind(npc: &Npc, part: usize, kind: i32) -> Option<Npc> {
    if part >= path_length(npc) || !(PART_WAIT..=PART_RUN).contains(&kind) {
        return None;
    }
    let mut npc = npc.clone();
    normalize(&mut npc);
    npc.ai_sections[part] = kind;
    Some(npc)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_yaml_ng::Mapping;

    fn npc(ai_sections: Vec<i32>, waypoints: Vec<i32>) -> Npc {
        Npc {
            type_id: 0,
            follow_mode: 0,
            position: RawVec3::default(),
            rotation: RawVec3::default(),
            tracking_range: 0,
            script_id: 0,
            ai_sections,
            waypoints: waypoints
                .into_iter()
                .map(|x| RawVec3 { x, y: 0, z: 0 })
                .collect(),
            other: Mapping::new(),
        }
    }

    fn xs(npc: &Npc) -> Vec<i32> {
        npc.waypoints.iter().map(|point| point.x).collect()
    }

    #[test]
    fn path_edits_keep_the_parts_and_points_in_step() {
        let start = npc(vec![1, 0, -1, -1, -1, -1, -1, -1], vec![5, 30]);
        let added = add_part(&start, RawVec3 { x: 9, y: 0, z: 0 }).unwrap();
        assert_eq!(added.ai_sections, [1, 0, 1, -1, -1, -1, -1, -1]);
        assert_eq!(xs(&added), [5, 30, 9]);
        let moved = move_part(&added, 2, false).unwrap();
        assert_eq!(moved.ai_sections, [1, 1, 0, -1, -1, -1, -1, -1]);
        assert_eq!(xs(&moved), [5, 9, 30]);
        assert!(move_part(&moved, 2, true).is_none());
        let removed = remove_part(&moved, 0).unwrap();
        assert_eq!(removed.ai_sections, [1, 0, -1, -1, -1, -1, -1, -1]);
        assert_eq!(xs(&removed), [9, 30]);
        let run = set_part_kind(&removed, 0, PART_RUN).unwrap();
        assert_eq!(run.ai_sections[0], PART_RUN);
        // A full path takes no more; a short record is padded first.
        let full = npc(vec![1; 8], vec![1; 8]);
        assert!(add_part(&full, RawVec3::default()).is_none());
        let short = add_part(&npc(vec![], vec![]), RawVec3 { x: 4, y: 0, z: 0 }).unwrap();
        assert_eq!(short.ai_sections, [1, -1, -1, -1, -1, -1, -1, -1]);
        assert_eq!(xs(&short), [4]);
    }

    #[test]
    fn cell_shapes() {
        assert_eq!(brush_cells(Cell::new(0, 0), 3).len(), 4);
        assert_eq!(brush_cells(Cell::new(10, 10), 5).len(), 25);
        assert_eq!(
            line_cells(Cell::new(0, 0), Cell::new(3, 1)),
            [
                Cell::new(0, 0),
                Cell::new(1, 0),
                Cell::new(2, 1),
                Cell::new(3, 1)
            ]
        );
        assert_eq!(rectangle_cells(Cell::new(4, 4), Cell::new(2, 3)).len(), 6);
        // A fill stops at other values.
        let value = |cell: Cell| u8::from(cell.column == 5);
        let filled = fill_cells(value, Cell::new(0, 0));
        assert_eq!(filled.len(), 5 * 100);
        assert!(filled.iter().all(|cell| cell.column < 5));
    }

    #[test]
    fn values_grouped_by_band() {
        let groups = value_groups(&TriggerBands::default());
        assert_eq!(groups[0], ValueGroup::Wall);
        assert!(matches!(&groups[2], ValueGroup::Step(values) if values.len() == 29));
        assert!(groups.contains(&ValueGroup::Exit {
            exit: 3,
            value: 113
        }));
        assert_eq!(groups.last(), Some(&ValueGroup::Toilet(120)));
    }
}
