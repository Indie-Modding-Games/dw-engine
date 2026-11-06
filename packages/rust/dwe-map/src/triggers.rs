//! What a collision trigger value does in the field (docs/formats/mapas.md, "Colisión";
//! packages/engine/core/include/dwe/core/collision_triggers.hpp): 51-79 run a script section when
//! stepped on, 80-109 when confirm is pressed, 110-119 take the map's exit `value - 110`, 120 is
//! the toilet. The engine still hard-codes these bands; when the team pack ships them as data
//! the tools should read them from there instead.

use std::ops::RangeInclusive;

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
}
