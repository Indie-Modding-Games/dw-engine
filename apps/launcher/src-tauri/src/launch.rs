//! Starting the game: dwe-game --content <data>/content from the game's folder (it finds its
//! team packs next to bin/), plus --mods with the player's own images while they are on
//! (custom_images.rs), with its output in <data>/logs/game.log so a failed start can show what the
//! game said.

use std::ffi::OsString;
use std::fs::{self, File};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};

use semver::Version;
use serde::Serialize;

use crate::content::content_folder;
use crate::custom_images::CustomImages;
use crate::failure::{code, Failure, Result};
use crate::graphics_step::GraphicsStep;
use crate::log::logs_folder;
use crate::store::Game;

pub fn game_log(data: &Path) -> PathBuf {
    logs_folder(data).join("game.log")
}

/// The graphics switches from the launcher's settings, as game flags.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct GraphicsFlags {
    pub compatibility: bool,
    pub low_performance: bool,
    /// Keeps the game off Vulkan 1.4 (graphics_step.rs).
    pub vulkan13: bool,
}

pub const COMPATIBLE_FLAG: &str = "--compatible";
pub const LOW_PERFORMANCE_FLAG: &str = "--low-performance";
pub const VULKAN13_FLAG: &str = "--vulkan13";
/// The first releases whose game knows each flag; an older game refuses the whole command line.
const FIRST_WITH_MODES: &str = "0.1.0-beta.1127";
const FIRST_WITH_VULKAN13: &str = "0.1.0-beta.1136";

/// A development build ("dev", or anything that is not a version) knows every flag.
fn knows(game: &Game, since: &str) -> bool {
    match (Version::parse(&game.version), Version::parse(since)) {
        (Ok(version), Ok(since)) => version >= since,
        _ => true,
    }
}

pub fn knows_modes(game: &Game) -> bool {
    knows(game, FIRST_WITH_MODES)
}

pub fn knows_vulkan13(game: &Game) -> bool {
    knows(game, FIRST_WITH_VULKAN13)
}

/// DeviceLostExitCode in the game's common.hpp.
pub const DEVICE_LOST_EXIT_CODE: i32 = 113;
/// GraphicsStartExitCode in the game's common.hpp: no GPU path could make the device. No older
/// game exits with it.
pub const GRAPHICS_START_EXIT_CODE: i32 = 114;
/// Windows closes a program that stopped responding with 0xCFFFFFFF.
pub const WINDOWS_HANG_EXIT_CODE: i32 = 0xCFFF_FFFF_u32 as i32;
/// An older game never returned it on purpose.
const FIRST_WITH_DEVICE_LOST_CODE: &str = "0.1.0-beta.1133";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum ExitReason {
    DeviceLost,
    WindowsHang,
    GraphicsStart,
}

pub fn exit_reason(game: &Game, code: Option<i32>) -> Option<ExitReason> {
    match code? {
        DEVICE_LOST_EXIT_CODE if knows(game, FIRST_WITH_DEVICE_LOST_CODE) => Some(ExitReason::DeviceLost),
        WINDOWS_HANG_EXIT_CODE => Some(ExitReason::WindowsHang),
        GRAPHICS_START_EXIT_CODE => Some(ExitReason::GraphicsStart),
        _ => None,
    }
}

impl GraphicsFlags {
    /// The switches plus what the graphics step adds (graphics_step.rs); the software step changes
    /// the driver, not the flags.
    pub fn with_step(self, step: GraphicsStep) -> Self {
        Self {
            compatibility: self.compatibility || step == GraphicsStep::Compatible,
            vulkan13: self.vulkan13 || step == GraphicsStep::Vulkan13,
            ..self
        }
    }

    /// The flags that are on and that this game knows.
    pub fn args(&self, game: &Game) -> Vec<&'static str> {
        [
            (self.compatibility, COMPATIBLE_FLAG, FIRST_WITH_MODES),
            (self.low_performance, LOW_PERFORMANCE_FLAG, FIRST_WITH_MODES),
            (self.vulkan13, VULKAN13_FLAG, FIRST_WITH_VULKAN13),
        ]
        .into_iter()
        .filter(|&(on, _, since)| on && knows(game, since))
        .map(|(_, flag, _)| flag)
        .collect()
    }
}

/// The game's command line. A data folder the player picked also gets the saves and the reports
/// there, so everything the player chose to move stays together; the default one leaves them in the
/// game's own folder (settings::default_data_folder). The player's own arguments go last.
pub fn command(game: &Game, data: &Path, own_data_folder: bool, graphics: GraphicsFlags, extra: &[String]) -> Command {
    let mut command = Command::new(game.game_program());
    command.arg("--content").arg(content_folder(data));
    command.args(graphics.args(game));
    command.args(extra);
    if !own_data_folder {
        command.arg("--saves").arg(data.join("saves")).arg("--reports").arg(data.join("reports"));
    }
    if let Some(pack) = CustomImages::new(data).active_pack() {
        command.arg("--mods").arg(pack);
    }
    command.current_dir(&game.dir);
    command
}

/// `env`: what the game starts with besides the launcher's own environment (the software Vulkan
/// driver's, system_check.rs, then the player's own variables).
pub fn start(game: &Game, data: &Path, own_data_folder: bool, env: &[(OsString, OsString)], graphics: GraphicsFlags, extra_args: &[String]) -> Result<Child> {
    let log = game_log(data);
    fs::create_dir_all(logs_folder(data))?;
    if log.exists() {
        let _ = fs::rename(&log, log.with_extension("prev.log"));
    }
    let out = File::create(&log)?;
    let err = out.try_clone()?;
    let mut command = command(game, data, own_data_folder, graphics, extra_args);
    crate::system_check::outside_appimage(&mut command);
    command.envs(env.iter().map(|(k, v)| (k, v)));
    // No console window next to the game on Windows: its output goes to the log.
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        command.creation_flags(CREATE_NO_WINDOW);
    }
    command
        .stdin(Stdio::null())
        .stdout(out)
        .stderr(err)
        .spawn()
        .map_err(|e| Failure::new(code::GAME_FAILED, format!("{}: {e}", game.game_program().display())))
}

/// The last lines of a log.
pub fn tail(path: &Path, lines: usize) -> Vec<String> {
    let text = fs::read_to_string(path).unwrap_or_default();
    let all: Vec<&str> = text.lines().collect();
    all[all.len().saturating_sub(lines)..].iter().map(|l| l.to_string()).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_command_mounts_the_content() {
        let game = Game { dir: "/g".into(), version: "1.0.0".into(), commit: String::new(), extractor_version: None };
        let data = Path::new("/d");
        let at = |name: &str| data.join(name).to_string_lossy().into_owned();
        let own = command(&game, data, true, GraphicsFlags::default(), &[]);
        let args: Vec<_> = own.get_args().map(|a| a.to_string_lossy().into_owned()).collect();
        assert_eq!(args, ["--content".into(), at("content")]);
        let moved = command(&game, data, false, GraphicsFlags::default(), &[]);
        let args: Vec<_> = moved.get_args().map(|a| a.to_string_lossy().into_owned()).collect();
        assert_eq!(args, ["--content".into(), at("content"), "--saves".into(), at("saves"), "--reports".into(), at("reports")]);
        let program = own.get_program().to_string_lossy().into_owned();
        assert!(program.ends_with(&format!("dwe-game{}", std::env::consts::EXE_SUFFIX)));
    }

    #[test]
    fn the_graphics_switches_add_their_flags() {
        let game = Game { dir: "/g".into(), version: "1.0.0".into(), commit: String::new(), extractor_version: None };
        let data = Path::new("/d");
        let args = |graphics: GraphicsFlags| -> Vec<String> {
            command(&game, data, true, graphics, &[]).get_args().map(|a| a.to_string_lossy().into_owned()).collect()
        };
        assert!(args(GraphicsFlags { compatibility: true, ..Default::default() }).contains(&"--compatible".to_string()));
        assert!(args(GraphicsFlags { low_performance: true, ..Default::default() }).contains(&"--low-performance".to_string()));
        assert_eq!(args(GraphicsFlags { compatibility: true, low_performance: true, vulkan13: false }).len(), 4, "both flags, next to --content");
        assert!(args(GraphicsFlags { vulkan13: true, ..Default::default() }).contains(&"--vulkan13".to_string()));
    }

    #[test]
    fn the_graphics_step_adds_its_flag() {
        let switches = GraphicsFlags { low_performance: true, ..Default::default() };
        assert_eq!(switches.with_step(GraphicsStep::Card), switches);
        assert!(switches.with_step(GraphicsStep::Vulkan13).vulkan13);
        let compatible = switches.with_step(GraphicsStep::Compatible);
        assert!(compatible.compatibility && compatible.low_performance && !compatible.vulkan13);
        assert_eq!(switches.with_step(GraphicsStep::Software), switches);
    }

    #[test]
    fn an_older_game_gets_only_the_flags_it_knows() {
        let all = GraphicsFlags { compatibility: true, low_performance: true, vulkan13: true };
        let game = |version: &str| Game { dir: "/g".into(), version: version.into(), commit: String::new(), extractor_version: None };
        assert!(all.args(&game("0.1.0-beta.1126")).is_empty());
        assert_eq!(all.args(&game("0.1.0-beta.1127")), ["--compatible", "--low-performance"]);
        assert_eq!(all.args(&game("0.1.0-beta.1136")), ["--compatible", "--low-performance", "--vulkan13"]);
        assert_eq!(all.args(&game("0.2.0")).len(), 3);
        assert_eq!(all.args(&game("dev")).len(), 3);
        assert!(GraphicsFlags::default().args(&game("dev")).is_empty());
    }

    #[test]
    fn the_exit_code_names_a_lost_gpu_or_a_windows_hang() {
        let game = |version: &str| Game { dir: "/g".into(), version: version.into(), commit: String::new(), extractor_version: None };
        for new in ["0.1.0-beta.1133", "0.2.0", "dev"] {
            assert_eq!(exit_reason(&game(new), Some(113)), Some(ExitReason::DeviceLost), "{new}");
        }
        assert_eq!(exit_reason(&game("0.1.0-beta.1132"), Some(113)), None, "an older game's 113 is no promise");
        assert_eq!(exit_reason(&game("dev"), Some(3)), None, "3 is what raise(SIGABRT) gives on Windows");
        assert_eq!(exit_reason(&game("0.1.0-beta.1100"), Some(-805_306_369)), Some(ExitReason::WindowsHang));
        assert_eq!(exit_reason(&game("dev"), Some(1)), None);
        assert_eq!(exit_reason(&game("dev"), Some(2)), None);
        assert_eq!(exit_reason(&game("dev"), None), None, "a signal is no exit code");
        assert_eq!(serde_json::to_string(&ExitReason::DeviceLost).unwrap(), r#""device-lost""#);
        assert_eq!(exit_reason(&game("0.1.0-beta.1100"), Some(114)), Some(ExitReason::GraphicsStart));
        assert_eq!(serde_json::to_string(&ExitReason::GraphicsStart).unwrap(), r#""graphics-start""#);
    }

    #[test]
    fn the_manual_arguments_go_after_the_launchers() {
        let game = Game { dir: "/g".into(), version: "1.0.0".into(), commit: String::new(), extractor_version: None };
        let data = Path::new("/d");
        let extra = vec!["--manual".to_string(), "with space".to_string()];
        let args: Vec<String> = command(&game, data, true, GraphicsFlags { compatibility: true, ..Default::default() }, &extra)
            .get_args()
            .map(|a| a.to_string_lossy().into_owned())
            .collect();
        assert_eq!(args.last(), Some(&"with space".to_string()), "one argument, not two");
        assert!(args.windows(2).any(|pair| pair == ["--compatible", "--manual"]));
    }

    #[test]
    fn the_players_images_are_mounted_while_they_are_on() {
        let game = Game { dir: "/g".into(), version: "1.0.0".into(), commit: String::new(), extractor_version: None };
        let data = tempfile::tempdir().unwrap();
        let images = CustomImages::new(data.path());
        let args = || -> Vec<String> {
            command(&game, data.path(), true, GraphicsFlags::default(), &[]).get_args().map(|a| a.to_string_lossy().into_owned()).collect()
        };
        assert_eq!(args().len(), 2, "no pack, no --mods");
        let image = images.pack().join("map/mayo01/background_day.png");
        fs::create_dir_all(image.parent().unwrap()).unwrap();
        fs::write(&image, b"png").unwrap();
        assert_eq!(args()[2..], ["--mods".to_string(), images.pack().to_string_lossy().into_owned()]);
        images.set_enabled(false).unwrap();
        assert_eq!(args().len(), 2, "turned off");
    }

    #[test]
    fn the_tail_of_a_log() {
        let dir = tempfile::tempdir().unwrap();
        let log = dir.path().join("game.log");
        fs::write(&log, "a\nb\nc\n").unwrap();
        assert_eq!(tail(&log, 2), ["b", "c"]);
        assert!(tail(&dir.path().join("none"), 2).is_empty());
    }
}
