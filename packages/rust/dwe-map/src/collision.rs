//! The 100 x 100 collision grid of a map (docs/formats/mapas.md, "Colisión"), read from and
//! written to the document's `collision` section exactly as `dwx` writes it
//! (packages/formats/src/map/map_yaml.cpp: collisionToYaml).

use std::collections::BTreeSet;

use serde::{Deserialize, Serialize};

use crate::world::{Cell, GRID_SIZE};
use crate::{Error, Result};

/// The value of a free cell.
pub const FREE: u8 = 0;
/// The bit that blocks a cell (map_collision.c: checkMapCollisionX/Z).
pub const BLOCKED_BIT: u8 = 0x80;
/// The value `dwx` writes as `#`.
const BLOCKED: u8 = BLOCKED_BIT;
const FREE_SYMBOL: char = '.';
const BLOCKED_SYMBOL: char = '#';
/// The symbols of the trigger values, in value order.
const TRIGGER_SYMBOLS: &str = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

const CELL_COUNT: usize = (GRID_SIZE * GRID_SIZE) as usize;

/// What a collision cell does.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum CellKind {
    Free,
    /// Any value with bit 0x80.
    Blocked,
    /// Any other value: the trigger id.
    Trigger(u8),
}

/// The `collision` section of a map document, as the YAML has it.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CollisionSection {
    pub width: i32,
    pub height: i32,
    /// `legend` (one character per cell) or `hex` (two hex digits per cell).
    pub encoding: String,
    pub legend: Vec<LegendEntry>,
    pub rows: Vec<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct LegendEntry {
    pub cell: String,
    pub value: u8,
}

/// The grid's raw values, row by row.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Collision {
    values: Vec<u8>,
}

impl Default for Collision {
    fn default() -> Self {
        Self {
            values: vec![FREE; CELL_COUNT],
        }
    }
}

impl Collision {
    /// A grid from 100 x 100 raw values, row by row.
    pub fn from_values(values: Vec<u8>) -> Result<Self> {
        if values.len() != CELL_COUNT {
            return Err(Error::Invalid(format!(
                "a collision grid has {CELL_COUNT} cells, not {}",
                values.len()
            )));
        }
        Ok(Self { values })
    }

    /// Reads a document's section with the engine's checks (map_document.cpp: readCollision).
    pub fn from_section(section: &CollisionSection) -> Result<Self> {
        let invalid = |message: String| Err(Error::Invalid(format!("collision: {message}")));
        if section.width != GRID_SIZE || section.height != GRID_SIZE {
            return invalid(format!("the grid must be {GRID_SIZE} x {GRID_SIZE}"));
        }
        let hex = match section.encoding.as_str() {
            "hex" => true,
            "legend" => false,
            _ => return invalid("the encoding must be 'legend' or 'hex'".to_owned()),
        };
        let mut legend = [None; 256];
        for entry in &section.legend {
            let mut chars = entry.cell.chars();
            match (chars.next(), chars.next()) {
                (Some(cell), None) if cell.is_ascii() => legend[cell as usize] = Some(entry.value),
                _ => {
                    return invalid(format!(
                        "legend cell '{}' must be one character",
                        entry.cell
                    ));
                }
            }
        }
        if section.rows.len() != GRID_SIZE as usize {
            return invalid(format!(
                "it needs {GRID_SIZE} rows, not {}",
                section.rows.len()
            ));
        }
        let row_length = if hex { 2 } else { 1 } * GRID_SIZE as usize;
        let mut values = Vec::with_capacity(CELL_COUNT);
        for (number, row) in section.rows.iter().enumerate() {
            if row.len() != row_length || !row.is_ascii() {
                return invalid(format!(
                    "row {number} must have {row_length} characters, not {}",
                    row.chars().count()
                ));
            }
            if hex {
                for pair in row.as_bytes().chunks(2) {
                    let text = std::str::from_utf8(pair).unwrap_or("");
                    let Ok(value) = u8::from_str_radix(text, 16) else {
                        return invalid(format!("row {number}: '{text}' is not a hex value"));
                    };
                    values.push(value);
                }
            } else {
                for cell in row.chars() {
                    let Some(value) = legend[cell as usize] else {
                        return invalid(format!(
                            "row {number}: cell '{cell}' is not in the legend"
                        ));
                    };
                    values.push(value);
                }
            }
        }
        Self::from_values(values)
    }

    /// The section `dwx` would write: a legend in value order (`.` free, `#` 128, then `A`-`Z`,
    /// `a`-`z`, `0`-`9`), or hex when the triggers do not fit.
    pub fn to_section(&self) -> CollisionSection {
        let distinct: BTreeSet<u8> = self.values.iter().copied().collect();
        let mut symbols = [None; 256];
        let mut triggers = TRIGGER_SYMBOLS.chars();
        let mut fits = true;
        for &value in &distinct {
            symbols[value as usize] = match value {
                FREE => Some(FREE_SYMBOL),
                BLOCKED => Some(BLOCKED_SYMBOL),
                _ => triggers.next(),
            };
            fits &= symbols[value as usize].is_some();
        }
        let legend = if fits {
            distinct
                .iter()
                .map(|&value| LegendEntry {
                    cell: symbols[value as usize].unwrap_or(FREE_SYMBOL).to_string(),
                    value,
                })
                .collect()
        } else {
            Vec::new()
        };
        let rows = self
            .values
            .chunks(GRID_SIZE as usize)
            .map(|row| {
                row.iter()
                    .map(|&value| match symbols[value as usize] {
                        Some(symbol) if fits => symbol.to_string(),
                        _ => format!("{value:02X}"),
                    })
                    .collect()
            })
            .collect();
        CollisionSection {
            width: GRID_SIZE,
            height: GRID_SIZE,
            encoding: if fits { "legend" } else { "hex" }.to_owned(),
            legend,
            rows,
        }
    }

    pub fn values(&self) -> &[u8] {
        &self.values
    }

    /// A cell's raw value; cells outside the grid are blocked.
    pub fn value(&self, cell: Cell) -> u8 {
        if cell.is_inside() {
            self.values[index_of(cell)]
        } else {
            BLOCKED_BIT
        }
    }

    pub fn kind(&self, cell: Cell) -> CellKind {
        kind_of(self.value(cell))
    }

    /// Sets a cell inside the grid; outside it does nothing.
    pub fn set(&mut self, cell: Cell, value: u8) {
        if cell.is_inside() {
            self.values[index_of(cell)] = value;
        }
    }

    /// Every cell with its value, row by row.
    pub fn cells(&self) -> impl Iterator<Item = (Cell, u8)> + '_ {
        self.values.iter().enumerate().map(|(index, &value)| {
            let index = index as i32;
            (Cell::new(index % GRID_SIZE, index / GRID_SIZE), value)
        })
    }
}

/// What a raw value does.
pub fn kind_of(value: u8) -> CellKind {
    if value == FREE {
        CellKind::Free
    } else if value & BLOCKED_BIT != 0 {
        CellKind::Blocked
    } else {
        CellKind::Trigger(value)
    }
}

fn index_of(cell: Cell) -> usize {
    (cell.row * GRID_SIZE + cell.column) as usize
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Collision {
        let mut collision = Collision::default();
        for column in 0..GRID_SIZE {
            collision.set(Cell::new(column, 0), BLOCKED);
        }
        collision.set(Cell::new(3, 4), 111);
        collision.set(Cell::new(4, 4), 51);
        collision.set(Cell::new(5, 4), 0x81);
        collision
    }

    #[test]
    fn the_legend_round_trips_like_dwx_writes_it() {
        let collision = sample();
        let section = collision.to_section();
        assert_eq!(section.encoding, "legend");
        let legend: Vec<_> = section
            .legend
            .iter()
            .map(|entry| (entry.cell.as_str(), entry.value))
            .collect();
        // Value order: 0, 51, 111, 128, 129; only 128 is '#'.
        assert_eq!(
            legend,
            vec![(".", 0), ("A", 51), ("B", 111), ("#", 128), ("C", 129)]
        );
        assert_eq!(&section.rows[4][..6], "...BAC");
        assert_eq!(Collision::from_section(&section).unwrap(), collision);
        let yaml = serde_yaml_ng::to_string(&section).unwrap();
        let back: CollisionSection = serde_yaml_ng::from_str(&yaml).unwrap();
        assert_eq!(back, section);
    }

    #[test]
    fn more_triggers_than_symbols_turn_to_hex() {
        let mut collision = Collision::default();
        for value in 1..=70u8 {
            collision.set(Cell::new(i32::from(value), 1), value);
        }
        let section = collision.to_section();
        assert_eq!(section.encoding, "hex");
        assert!(section.legend.is_empty());
        assert_eq!(&section.rows[1][..6], "000102");
        assert_eq!(Collision::from_section(&section).unwrap(), collision);
    }

    #[test]
    fn kinds_and_the_outside() {
        let collision = sample();
        assert_eq!(collision.kind(Cell::new(0, 0)), CellKind::Blocked);
        assert_eq!(collision.kind(Cell::new(5, 4)), CellKind::Blocked);
        assert_eq!(collision.kind(Cell::new(3, 4)), CellKind::Trigger(111));
        assert_eq!(collision.kind(Cell::new(3, 5)), CellKind::Free);
        assert_eq!(collision.kind(Cell::new(-1, 5)), CellKind::Blocked);
    }

    #[test]
    fn refuses_bad_sections() {
        let mut section = sample().to_section();
        section.rows[2].push('.');
        assert!(Collision::from_section(&section).is_err());
        let mut section = sample().to_section();
        section.rows[2].replace_range(0..1, "Z");
        assert!(Collision::from_section(&section).is_err());
        let mut section = sample().to_section();
        section.encoding = "rle".to_owned();
        assert!(Collision::from_section(&section).is_err());
    }
}
