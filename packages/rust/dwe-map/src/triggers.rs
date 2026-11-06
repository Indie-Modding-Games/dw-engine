//! What a collision trigger value does in the field (docs/formats/mapas.md, "Colisión";
//! packages/game/field/src/trigger_bands.cpp): the game pack's `field/triggers` gives the bands,
//! and without it the original's apply: 51-79 run a script section when stepped on, 80-109 when
//! confirm is pressed, 110-119 take the map's exit `value - 110`, 120 is the toilet.

use std::ops::RangeInclusive;

use dwe_pack::{AssetId, Vfs};
use serde_yaml_ng::Value;

use crate::collision::BLOCKED_BIT;
use crate::reader::{Checked, expect_keys, field, integer};
use crate::{Error, Result};

/// The id of the bands' document (content/schemas/field_triggers.schema.json).
pub const TRIGGERS_ID: &str = "field/triggers";

/// The most exits a map has, so the most values of the exits band.
pub const MOST_EXITS: i32 = 10;

/// The trigger bands of the field.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TriggerBands {
    pub step: RangeInclusive<u8>,
    pub confirm: RangeInclusive<u8>,
    /// The first exit trigger; exit `i` is `exit_start + i`.
    pub exit_start: u8,
    pub exit_count: u8,
    pub toilet: u8,
}

impl Default for TriggerBands {
    fn default() -> Self {
        Self {
            step: 51..=79,
            confirm: 80..=109,
            exit_start: 110,
            exit_count: 10,
            toilet: 120,
        }
    }
}

/// The band a trigger value falls in.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum TriggerBand {
    /// Runs the script section of that number when stepped on.
    Step,
    /// Runs the script section of that number when confirm is pressed on the cell.
    Confirm,
    /// Takes the map's exit with this number.
    Exit(u8),
    Toilet,
    /// A value no band has: the field ignores it.
    Other,
}

impl TriggerBands {
    pub fn band(&self, value: u8) -> TriggerBand {
        if self.step.contains(&value) {
            TriggerBand::Step
        } else if self.confirm.contains(&value) {
            TriggerBand::Confirm
        } else if value >= self.exit_start && value - self.exit_start < self.exit_count {
            TriggerBand::Exit(value - self.exit_start)
        } else if value == self.toilet {
            TriggerBand::Toilet
        } else {
            TriggerBand::Other
        }
    }

    /// The trigger value of an exit.
    pub fn exit_trigger(&self, exit: u8) -> Option<u8> {
        (exit < self.exit_count).then(|| self.exit_start + exit)
    }

    /// Parses the text of `field/triggers` with the engine's checks (parseTriggerBands): the
    /// bands may not overlap, stay below the blocked bit and above 0 (a free cell), and give at
    /// most [`MOST_EXITS`] exits. Unknown and missing keys are errors.
    pub fn parse(yaml: &str) -> Result<Self> {
        let invalid = |message: String| Error::Invalid(format!("field triggers: {message}"));
        let tree: Value =
            serde_yaml_ng::from_str(yaml).map_err(|error| invalid(error.to_string()))?;
        read_bands(&tree).map_err(invalid)
    }

    /// Reads `field/triggers` from the packs (the file that wins); without one, the original's
    /// bands (loadTriggerBands).
    pub fn load(vfs: &Vfs) -> Result<Self> {
        let id = AssetId::parse(TRIGGERS_ID)?;
        if vfs.resolve(&id).is_none() {
            return Ok(Self::default());
        }
        Self::parse(&vfs.read_text(&id)?)
    }
}

/// A band as the document gives it, first and last value included.
#[derive(Clone, Copy)]
struct Band {
    first: i32,
    last: i32,
}

fn read_band(node: &Value, what: &str) -> Checked<Band> {
    let mapping = expect_keys(node, what, &["first", "last"], &[])?;
    let band = Band {
        first: integer(field(mapping, "first")?)?,
        last: integer(field(mapping, "last")?)?,
    };
    if band.first > band.last {
        return Err(format!("{what}: first is past last"));
    }
    Ok(band)
}

fn read_bands(tree: &Value) -> Checked<TriggerBands> {
    let root = expect_keys(
        tree,
        "the field triggers",
        &["step", "confirm", "exits", "toilet", "blocked_bit"],
        &[],
    )?;
    let step = read_band(field(root, "step")?, "step")?;
    let confirm = read_band(field(root, "confirm")?, "confirm")?;
    let exits = read_band(field(root, "exits")?, "exits")?;
    let toilet: i32 = integer(field(root, "toilet")?)?;
    let blocked_bit: i32 = integer(field(root, "blocked_bit")?)?;
    // Every band is a cell value of a trigger: above 0 (free) and below the blocked bit; no two
    // share a value; the exits fit a map's warps.
    if blocked_bit != i32::from(BLOCKED_BIT) {
        return Err(format!(
            "blocked_bit is fixed by the map format: it must be {BLOCKED_BIT}"
        ));
    }
    let named = [
        ("step", step),
        ("confirm", confirm),
        ("exits", exits),
        (
            "toilet",
            Band {
                first: toilet,
                last: toilet,
            },
        ),
    ];
    for (name, band) in named {
        if band.first < 1 || band.last >= blocked_bit {
            return Err(format!(
                "{name}: the values must be 1 to {}",
                blocked_bit - 1
            ));
        }
    }
    for (one, (name, a)) in named.iter().enumerate() {
        for (other_name, b) in &named[one + 1..] {
            if a.first <= b.last && b.first <= a.last {
                return Err(format!("{name} and {other_name} share values"));
            }
        }
    }
    if exits.last - exits.first + 1 > MOST_EXITS {
        return Err(format!("exits: a map has at most {MOST_EXITS} exits"));
    }
    // Every value is now 1 to 127.
    let value = |number: i32| number as u8;
    Ok(TriggerBands {
        step: value(step.first)..=value(step.last),
        confirm: value(confirm.first)..=value(confirm.last),
        exit_start: value(exits.first),
        exit_count: value(exits.last - exits.first + 1),
        toilet: value(toilet),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_original_bands() {
        let bands = TriggerBands::default();
        assert_eq!(bands.band(50), TriggerBand::Other);
        assert_eq!(bands.band(51), TriggerBand::Step);
        assert_eq!(bands.band(79), TriggerBand::Step);
        assert_eq!(bands.band(80), TriggerBand::Confirm);
        assert_eq!(bands.band(109), TriggerBand::Confirm);
        assert_eq!(bands.band(110), TriggerBand::Exit(0));
        assert_eq!(bands.band(119), TriggerBand::Exit(9));
        assert_eq!(bands.band(120), TriggerBand::Toilet);
        assert_eq!(bands.band(121), TriggerBand::Other);
        assert_eq!(bands.exit_trigger(3), Some(113));
        assert_eq!(bands.exit_trigger(10), None);
    }

    const ORIGINAL: &str = "step:\n  first: 51\n  last: 79\nconfirm:\n  first: 80\n  last: 109\n\
                            exits:\n  first: 110\n  last: 119\ntoilet: 120\nblocked_bit: 128\n";

    /// The original's document with one part replaced.
    fn with(part: &str, replacement: &str) -> String {
        assert!(ORIGINAL.contains(part), "{part}");
        ORIGINAL.replacen(part, replacement, 1)
    }

    fn error_of(yaml: &str) -> String {
        TriggerBands::parse(yaml).unwrap_err().to_string()
    }

    #[test]
    fn the_document_of_the_original_bands_gives_the_default() {
        assert_eq!(
            TriggerBands::parse(ORIGINAL).unwrap(),
            TriggerBands::default()
        );
    }

    #[test]
    fn the_bands_read_from_a_document_move_the_cells_meaning() {
        let bands = TriggerBands::parse(&with(
            "  first: 110\n  last: 119\n",
            "  first: 20\n  last: 24\n",
        ))
        .unwrap();
        assert_eq!(bands.band(22), TriggerBand::Exit(2));
        assert_eq!(bands.band(110), TriggerBand::Other);
        assert_eq!(bands.exit_trigger(4), Some(24));
        assert_eq!(bands.exit_trigger(5), None);
        assert_eq!(bands.band(60), TriggerBand::Step);
    }

    #[test]
    fn refuses_bands_that_are_no_trigger_cells_or_overlap() {
        let cases = [
            (
                with("first: 51\n  last: 79", "first: 79\n  last: 51"),
                "step: first is past last",
            ),
            (
                with("first: 80\n  last: 109", "first: 70\n  last: 109"),
                "step and confirm share values",
            ),
            (
                with("toilet: 120", "toilet: 115"),
                "exits and toilet share values",
            ),
            (
                with("toilet: 120", "toilet: 128"),
                "toilet: the values must be 1 to 127",
            ),
            (
                with("first: 51", "first: 0"),
                "step: the values must be 1 to 127",
            ),
            (
                with("last: 119\ntoilet: 120", "last: 121\ntoilet: 122"),
                "exits: a map has at most 10 exits",
            ),
            (
                with("blocked_bit: 128", "blocked_bit: 64"),
                "blocked_bit is fixed by the map format: it must be 128",
            ),
            (
                with("toilet: 120", "toilet: 120\nwater: 121"),
                "unknown key 'water'",
            ),
            (with("toilet: 120\n", ""), "has no 'toilet'"),
        ];
        for (yaml, expected) in cases {
            let error = error_of(&yaml);
            assert!(error.starts_with("field triggers: "), "{error}");
            assert!(error.contains(expected), "{error} lacks {expected}");
        }
    }

    #[test]
    fn loads_the_winning_document_or_the_default() {
        let dir = tempfile::tempdir().unwrap();
        let pack = |name: &str, layer: &str, priority: i32| {
            let root = dir.path().join(name);
            std::fs::create_dir_all(root.join("field")).unwrap();
            std::fs::write(
                root.join("pack.yaml"),
                format!("id: {name}\nversion: 1.0.0\nlayer: {layer}\npriority: {priority}\n"),
            )
            .unwrap();
            dwe_pack::PackLocation {
                root,
                origin: dwe_pack::PackOrigin::Engine,
            }
        };
        let base = pack("base", "base", 0);
        let vfs = Vfs::mount(std::slice::from_ref(&base)).unwrap();
        assert_eq!(TriggerBands::load(&vfs).unwrap(), TriggerBands::default());

        let game = pack("game", "base", 1);
        std::fs::write(
            game.root.join("field/triggers.yaml"),
            with("toilet: 120", "toilet: 121"),
        )
        .unwrap();
        let vfs = Vfs::mount(&[base.clone(), game.clone()]).unwrap();
        assert_eq!(TriggerBands::load(&vfs).unwrap().toilet, 121);

        let higher = pack("higher", "mods", 0);
        std::fs::write(
            higher.root.join("field/triggers.yaml"),
            with("toilet: 120", "toilet: 122"),
        )
        .unwrap();
        let vfs = Vfs::mount(&[base.clone(), game.clone(), higher.clone()]).unwrap();
        assert_eq!(TriggerBands::load(&vfs).unwrap().toilet, 122);

        std::fs::write(higher.root.join("field/triggers.yaml"), "step: 1\n").unwrap();
        let vfs = Vfs::mount(&[base, game, higher]).unwrap();
        assert!(TriggerBands::load(&vfs).is_err());
    }
}
