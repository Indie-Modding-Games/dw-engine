//! The graphics step Play starts on: an early failure (exit 114, a device lost or a hang) moves
//! the next start one step down; the settings or a new GPU driver put it back on the card.

use serde::{Deserialize, Serialize};
use std::time::Duration;

use crate::launch::ExitReason;
use crate::system_check::Gpu;

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum GraphicsStep {
    /// The card, on the path the game picks.
    #[default]
    Card,
    /// The card, never on Vulkan 1.4 (--vulkan13).
    Vulkan13,
    /// The card in the 1.2 compatible mode (--compatible).
    Compatible,
    /// The package's software driver: no card at all.
    Software,
}

/// A device lost or a hang before this counts as the step not working on this computer, not as a
/// crash during play.
pub const EARLY_FAILURE: Duration = Duration::from_secs(90);

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct StepOptions {
    /// The game knows --vulkan13 and the GPU offers 1.4 (otherwise the card already runs 1.3).
    pub vulkan13: bool,
    /// The game knows --compatible.
    pub compatible: bool,
    /// The package brings the software driver.
    pub software: bool,
}

/// The step after `step` that is open here; None at the bottom.
pub fn next_step(step: GraphicsStep, options: StepOptions) -> Option<GraphicsStep> {
    let open = |candidate: GraphicsStep| match candidate {
        GraphicsStep::Card => true,
        GraphicsStep::Vulkan13 => options.vulkan13,
        GraphicsStep::Compatible => options.compatible,
        GraphicsStep::Software => options.software,
    };
    [GraphicsStep::Vulkan13, GraphicsStep::Compatible, GraphicsStep::Software]
        .into_iter()
        .find(|&candidate| candidate > step && open(candidate))
}

pub fn failed_early(reason: Option<ExitReason>, ran: Duration) -> bool {
    match reason {
        Some(ExitReason::GraphicsStart) => true,
        Some(ExitReason::DeviceLost | ExitReason::WindowsHang) => ran < EARLY_FAILURE,
        None => false,
    }
}

/// The GPU and driver a step was taken on: a different one (a driver update, a new card) gets the
/// card again.
pub fn gpu_key(gpu: Option<&Gpu>) -> String {
    gpu.map(|g| format!("{} | {}", g.name, g.driver)).unwrap_or_default()
}

pub fn still_holds(step: GraphicsStep, saved_key: &str, gpu: Option<&Gpu>) -> bool {
    step == GraphicsStep::Card || gpu.is_none() || gpu_key(gpu) == saved_key
}

#[cfg(test)]
mod tests {
    use super::*;

    const ALL: StepOptions = StepOptions { vulkan13: true, compatible: true, software: true };

    fn gpu(driver: &str) -> Gpu {
        Gpu { name: "Radeon".into(), vendor_id: 0x1002, kind: "discrete".into(), api: "1.4.0".into(), driver: driver.into() }
    }

    #[test]
    fn the_steps_go_down_in_order() {
        assert_eq!(next_step(GraphicsStep::Card, ALL), Some(GraphicsStep::Vulkan13));
        assert_eq!(next_step(GraphicsStep::Vulkan13, ALL), Some(GraphicsStep::Compatible));
        assert_eq!(next_step(GraphicsStep::Compatible, ALL), Some(GraphicsStep::Software));
        assert_eq!(next_step(GraphicsStep::Software, ALL), None);
    }

    #[test]
    fn a_step_this_computer_lacks_is_skipped() {
        let no_14 = StepOptions { vulkan13: false, ..ALL };
        assert_eq!(next_step(GraphicsStep::Card, no_14), Some(GraphicsStep::Compatible));
        let no_software = StepOptions { software: false, ..ALL };
        assert_eq!(next_step(GraphicsStep::Compatible, no_software), None);
        assert_eq!(next_step(GraphicsStep::Card, StepOptions::default()), None);
        let only_software = StepOptions { software: true, ..StepOptions::default() };
        assert_eq!(next_step(GraphicsStep::Card, only_software), Some(GraphicsStep::Software));
    }

    #[test]
    fn only_an_early_gpu_failure_steps_down() {
        let soon = Duration::from_secs(5);
        let later = Duration::from_secs(600);
        assert!(failed_early(Some(ExitReason::GraphicsStart), later));
        assert!(failed_early(Some(ExitReason::DeviceLost), soon));
        assert!(!failed_early(Some(ExitReason::DeviceLost), later), "a GPU lost after an hour of play is not the step");
        assert!(failed_early(Some(ExitReason::WindowsHang), soon));
        assert!(!failed_early(None, soon), "a crash for another reason keeps the step");
    }

    #[test]
    fn a_new_driver_gets_the_card_again() {
        let old = gpu("AMD 23.1");
        let key = gpu_key(Some(&old));
        assert!(still_holds(GraphicsStep::Compatible, &key, Some(&old)));
        assert!(!still_holds(GraphicsStep::Compatible, &key, Some(&gpu("AMD 25.4"))));
        assert!(still_holds(GraphicsStep::Compatible, &key, None), "no answer from the check keeps it");
        assert!(still_holds(GraphicsStep::Card, "", Some(&old)));
    }

    #[test]
    fn the_steps_are_saved_by_name() {
        assert_eq!(serde_json::to_string(&GraphicsStep::Vulkan13).unwrap(), r#""vulkan13""#);
        assert_eq!(serde_json::to_string(&GraphicsStep::Compatible).unwrap(), r#""compatible""#);
        assert_eq!(serde_json::from_str::<GraphicsStep>(r#""software""#).unwrap(), GraphicsStep::Software);
    }
}
