//! Starting the game: dwe-game --content <data>/content from the game's folder (it finds its
//! team packs next to bin/), plus --mods with the player's own images while they are on
//! (custom_images.rs), with its output in <data>/logs/game.log so a failed start can show what the
//! game said.

use std::ffi::OsString;
use std::fs::{self, File};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};

use crate::content::content_folder;
use crate::custom_images::CustomImages;
use crate::failure::{code, Failure, Result};
use crate::log::logs_folder;
use crate::store::Game;

pub fn game_log(data: &Path) -> PathBuf {
    logs_folder(data).join("game.log")
}

/// The game's command line. A data folder the player picked also gets the saves and the reports
/// there, so everything the player chose to move stays together; the default one leaves them in the
/// game's own folder (settings::default_data_folder).
pub fn command(game: &Game, data: &Path, own_data_folder: bool) -> Command {
    let mut command = Command::new(game.game_program());
    command.arg("--content").arg(content_folder(data));
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
/// driver's, system_check.rs).
pub fn start(game: &Game, data: &Path, own_data_folder: bool, env: &[(OsString, OsString)]) -> Result<Child> {
    let log = game_log(data);
    fs::create_dir_all(logs_folder(data))?;
    if log.exists() {
        let _ = fs::rename(&log, log.with_extension("prev.log"));
    }
    let out = File::create(&log)?;
    let err = out.try_clone()?;
    let mut command = command(game, data, own_data_folder);
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
        let own = command(&game, data, true);
        let args: Vec<_> = own.get_args().map(|a| a.to_string_lossy().into_owned()).collect();
        assert_eq!(args, ["--content".into(), at("content")]);
        let moved = command(&game, data, false);
        let args: Vec<_> = moved.get_args().map(|a| a.to_string_lossy().into_owned()).collect();
        assert_eq!(args, ["--content".into(), at("content"), "--saves".into(), at("saves"), "--reports".into(), at("reports")]);
        let program = own.get_program().to_string_lossy().into_owned();
        assert!(program.ends_with(&format!("dwe-game{}", std::env::consts::EXE_SUFFIX)));
    }

    #[test]
    fn the_players_images_are_mounted_while_they_are_on() {
        let game = Game { dir: "/g".into(), version: "1.0.0".into(), commit: String::new(), extractor_version: None };
        let data = tempfile::tempdir().unwrap();
        let images = CustomImages::new(data.path());
        let args = || -> Vec<String> {
            command(&game, data.path(), true).get_args().map(|a| a.to_string_lossy().into_owned()).collect()
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
