//! What the launcher does by itself when a check finds a new launcher or a new game (Settings →
//! Updates): the decision for each one, and the last check's result, kept in settings.json so the
//! page can say when it last looked and what happened.
//!
//! The rules: with the option on, an update found is downloaded, verified and installed without
//! asking, but never while the game runs (it waits for the game to end) or while another task
//! runs. With the option off, or when this copy cannot replace itself (a launcher not started from
//! its AppImage, a build without the signing key), the update is only offered with a button. A
//! failed update keeps the current version (the installs are atomic) and is reported.

use serde::{Deserialize, Serialize};

use crate::failure::Failure;

/// What to do with one update (the launcher's or the game's).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum Action {
    /// Nothing new.
    None,
    /// Show the button: the player decides.
    Offer,
    /// Download, verify and install it now.
    Install,
    /// Install it as soon as the game (or the running task) ends.
    Wait,
    /// Already installed; it takes effect when the launcher restarts.
    Ready,
}

#[derive(Debug, Clone, Copy, Default)]
pub struct Facts {
    /// The check found a newer version.
    pub found: bool,
    /// The player's option (on by default).
    pub automatic: bool,
    /// This copy can install it by itself (signing key, an installed game or a self-replaceable
    /// launcher).
    pub can_install: bool,
    pub game_running: bool,
    /// Another task (a download, an extraction) is running.
    pub busy: bool,
    /// That version was already installed in this session.
    pub installed: bool,
}

pub fn plan(facts: Facts) -> Action {
    if !facts.found {
        Action::None
    } else if facts.installed {
        Action::Ready
    } else if !facts.automatic || !facts.can_install {
        Action::Offer
    } else if facts.game_running || facts.busy {
        Action::Wait
    } else {
        Action::Install
    }
}

/// The last check, as Settings → Updates shows it.
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct LastCheck {
    /// Seconds since 1970 (UTC).
    pub at: u64,
    /// "latest", "available", "updated" or "failed".
    pub outcome: String,
    /// The launcher version installed (updated) or found (available).
    pub launcher: Option<String>,
    /// The game version installed (updated) or found (available).
    pub game: Option<String>,
    /// For "failed": the failure code and detail, as the page translates them.
    pub code: Option<String>,
    pub detail: Option<String>,
}

pub fn now() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_secs())
}

impl LastCheck {
    /// A check that answered: what it found (None: up to date).
    pub fn checked(at: u64, launcher: Option<String>, game: Option<String>) -> Self {
        let outcome = if launcher.is_some() || game.is_some() { "available" } else { "latest" };
        Self { at, outcome: outcome.into(), launcher, game, code: None, detail: None }
    }

    pub fn failed(at: u64, failure: &Failure) -> Self {
        Self { at, outcome: "failed".into(), code: Some(failure.code.into()), detail: Some(failure.detail.clone()), ..Self::default() }
    }

    /// An update was installed after this check: the launcher's or the game's (both can happen
    /// after one check; the other one's result is kept).
    pub fn updated(&self, at: u64, launcher: Option<String>, game: Option<String>) -> Self {
        let earlier = self.outcome == "updated";
        Self {
            at,
            outcome: "updated".into(),
            launcher: launcher.or_else(|| self.launcher.clone().filter(|_| earlier)),
            game: game.or_else(|| self.game.clone().filter(|_| earlier)),
            code: None,
            detail: None,
        }
    }
}

/// Whether this launcher can replace itself: on Linux only when it runs from its AppImage (a
/// package manager's copy is updated by the package manager).
pub fn launcher_replaceable() -> bool {
    if cfg!(target_os = "linux") {
        std::env::var_os("APPIMAGE").is_some_and(|p| !p.is_empty())
    } else {
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::failure::code;

    fn facts(found: bool, automatic: bool, game_running: bool) -> Facts {
        Facts { found, automatic, can_install: true, game_running, busy: false, installed: false }
    }

    #[test]
    fn option_on_installs_unless_the_game_runs() {
        assert_eq!(plan(facts(true, true, false)), Action::Install);
        assert_eq!(plan(facts(true, true, true)), Action::Wait);
        assert_eq!(plan(Facts { busy: true, ..facts(true, true, false) }), Action::Wait);
    }

    #[test]
    fn option_off_only_offers() {
        assert_eq!(plan(facts(true, false, false)), Action::Offer);
        assert_eq!(plan(facts(true, false, true)), Action::Offer);
    }

    #[test]
    fn nothing_found_does_nothing() {
        for automatic in [false, true] {
            for running in [false, true] {
                assert_eq!(plan(facts(false, automatic, running)), Action::None);
            }
        }
    }

    #[test]
    fn a_copy_that_cannot_install_offers() {
        assert_eq!(plan(Facts { can_install: false, ..facts(true, true, false) }), Action::Offer);
    }

    #[test]
    fn an_installed_update_is_not_installed_again() {
        assert_eq!(plan(Facts { installed: true, ..facts(true, true, false) }), Action::Ready);
        assert_eq!(plan(Facts { installed: true, ..facts(true, false, true) }), Action::Ready);
    }

    #[test]
    fn last_check_results() {
        let latest = LastCheck::checked(10, None, None);
        assert_eq!(latest.outcome, "latest");
        let found = LastCheck::checked(20, Some("0.2.0".into()), Some("0.1.0-beta.2".into()));
        assert_eq!(found.outcome, "available");
        let launcher = found.updated(21, Some("0.2.0".into()), None);
        assert_eq!((launcher.outcome.as_str(), launcher.launcher.as_deref(), launcher.game.as_deref()), ("updated", Some("0.2.0"), None));
        let both = launcher.updated(22, None, Some("0.1.0-beta.2".into()));
        assert_eq!((both.launcher.as_deref(), both.game.as_deref()), (Some("0.2.0"), Some("0.1.0-beta.2")));
        let failed = LastCheck::failed(30, &Failure::new(code::BAD_SIGNATURE, "x"));
        assert_eq!((failed.outcome.as_str(), failed.code.as_deref()), ("failed", Some(code::BAD_SIGNATURE)));
        assert_eq!(failed.launcher, None);
    }
}
