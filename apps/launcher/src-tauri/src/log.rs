//! The launcher's own log, <data>/logs/launcher.log: one line per step, never a token. The page's
//! "Open logs" shows that folder, where the game's output (game.log) goes too.

use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

static LOG: Mutex<Option<File>> = Mutex::new(None);

/// A log grows up to this size; at the next start it moves to launcher.old.log.
const MAX_LOG_BYTES: u64 = 1024 * 1024;

pub fn logs_folder(data: &Path) -> PathBuf {
    data.join("logs")
}

/// Opens (or reopens, after the data folder changed) the log in the data folder.
pub fn open(data: &Path) {
    let folder = logs_folder(data);
    let _ = fs::create_dir_all(&folder);
    let path = folder.join("launcher.log");
    if fs::metadata(&path).map(|m| m.len() > MAX_LOG_BYTES).unwrap_or(false) {
        let _ = fs::rename(&path, folder.join("launcher.old.log"));
    }
    let file = OpenOptions::new().create(true).append(true).open(&path).ok();
    *LOG.lock().unwrap_or_else(|e| e.into_inner()) = file;
}

pub fn line(text: &str) {
    let stamp = time::OffsetDateTime::now_utc()
        .format(&time::macros::format_description!("[year]-[month]-[day] [hour]:[minute]:[second]"))
        .unwrap_or_default();
    let text = redact(text);
    eprintln!("dwe-launcher: {text}");
    if let Some(file) = LOG.lock().unwrap_or_else(|e| e.into_inner()).as_mut() {
        let _ = writeln!(file, "{stamp} {text}");
    }
}

/// Hides the beta key and anything that looks like a GitHub token, in case one ever reaches a
/// message.
pub fn redact(text: &str) -> String {
    const PREFIXES: [&str; 5] = ["ghp_", "gho_", "ghu_", "ghs_", "github_pat_"];
    let text = crate::beta_key::hide(text);
    let mut out = String::with_capacity(text.len());
    for (i, word) in text.split(' ').enumerate() {
        if i > 0 {
            out.push(' ');
        }
        if PREFIXES.iter().any(|p| word.contains(p)) {
            out.push_str("[token]");
        } else {
            out.push_str(word);
        }
    }
    out
}

#[macro_export]
macro_rules! log_line {
    ($($arg:tt)*) => { $crate::log::line(&format!($($arg)*)) };
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tokens_never_reach_the_log() {
        assert_eq!(redact("auth with ghp_abc123 failed"), "auth with [token] failed");
        assert_eq!(redact("github_pat_11AA_x"), "[token]");
        assert_eq!(redact("nothing to hide"), "nothing to hide");
    }
}
