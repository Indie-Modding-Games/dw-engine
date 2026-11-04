//! Starting the game: dwe-game --content <data>/content from the game's folder (it finds its
//! team packs next to bin/), with its output in <data>/logs/game.log so a failed start can show
//! what the game said.

use std::fs::{self, File};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};

use crate::content::content_folder;
use crate::failure::{code, Failure, Result};
use crate::log::logs_folder;
use crate::store::Game;

pub fn game_log(data: &Path) -> PathBuf {
    logs_folder(data).join("game.log")
}

/// The game's command line. A data folder other than the game's own also gets the saves and the
/// reports there, so everything the player chose to move stays together.
pub fn command(game: &Game, data: &Path, own_data_folder: bool) -> Command {
    let mut command = Command::new(game.game_program());
    command.arg("--content").arg(content_folder(data));
    if !own_data_folder {
        command.arg("--saves").arg(data.join("saves")).arg("--reports").arg(data.join("reports"));
    }
    command.current_dir(&game.dir);
    command
}

pub fn start(game: &Game, data: &Path, own_data_folder: bool) -> Result<Child> {
    let log = game_log(data);
    fs::create_dir_all(logs_folder(data))?;
    if log.exists() {
        let _ = fs::rename(&log, log.with_extension("prev.log"));
    }
    let out = File::create(&log)?;
    let err = out.try_clone()?;
    command(game, data, own_data_folder)
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
        let own = command(&game, Path::new("/d"), true);
        let args: Vec<_> = own.get_args().map(|a| a.to_string_lossy().into_owned()).collect();
        assert_eq!(args, ["--content", "/d/content"]);
        let moved = command(&game, Path::new("/d"), false);
        let args: Vec<_> = moved.get_args().map(|a| a.to_string_lossy().into_owned()).collect();
        assert_eq!(args, ["--content", "/d/content", "--saves", "/d/saves", "--reports", "/d/reports"]);
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
