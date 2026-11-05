//! The launcher's own settings (settings.json in the system's configuration folder, never in the
//! data folder, which the player may change): where the data goes, the language, the release server
//! and the disc the content was read from. The beta key is not here: it lives in the system keyring
//! (beta_key.rs).

use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};

use crate::auto_update::LastCheck;
use crate::failure::Result;
use crate::store::write_atomically;

/// The release server: the closed beta's gateway (services/beta-gateway), whose URL the release
/// build sets with DWE_LAUNCHER_GATEWAY. Without it the build points at a placeholder that never
/// answers (.invalid names do not exist), so a development build never talks to a real server.
pub const DEFAULT_API_BASE: &str = match option_env!("DWE_LAUNCHER_GATEWAY") {
    Some(url) if !url.is_empty() => url,
    _ => PLACEHOLDER_GATEWAY,
};
pub const PLACEHOLDER_GATEWAY: &str = "https://beta-gateway.invalid";
/// The repository whose releases carry the game, as the gateway's paths name it.
pub const DEFAULT_REPOSITORY: &str = "Indie-Modding-Games/dw-engine";
/// Overrides the release server for one run (tests and local tries against a fake one).
pub const API_BASE_ENV: &str = "DWE_LAUNCHER_API";

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct Settings {
    /// None: the game's own data folder (data_folder()).
    pub data_folder: Option<PathBuf>,
    /// "es" or "en".
    pub language: String,
    /// Look for updates every time the launcher opens.
    pub check_updates: bool,
    /// Install a new launcher found by a check without asking (auto_update.rs).
    pub auto_update_launcher: bool,
    /// Install a new game found by a check without asking (auto_update.rs).
    pub auto_update_game: bool,
    /// When the last check ran and what it did.
    pub last_check: Option<LastCheck>,
    /// The build's gateway, or DWE_LAUNCHER_API for this run: never saved, so a new launcher's
    /// gateway always wins over an older one's.
    #[serde(skip, default = "default_api_base")]
    pub api_base: String,
    pub repository: String,
    /// What the player picked last: a disc image or an archive holding one. An update whose
    /// extractor reads discs differently reads it again from here.
    pub disc_source: Option<PathBuf>,
}

fn default_api_base() -> String {
    DEFAULT_API_BASE.into()
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            data_folder: None,
            language: "es".into(),
            check_updates: true,
            auto_update_launcher: true,
            auto_update_game: true,
            last_check: None,
            api_base: DEFAULT_API_BASE.into(),
            repository: DEFAULT_REPOSITORY.into(),
            disc_source: None,
        }
    }
}

impl Settings {
    pub fn file() -> Option<PathBuf> {
        dirs::config_dir().map(|d| d.join("indie-modding-games").join("dwe-launcher").join("settings.json"))
    }

    pub fn load_from(path: &Path) -> Settings {
        let mut settings: Settings = fs::read_to_string(path)
            .ok()
            .and_then(|text| serde_json::from_str(&text).ok())
            .unwrap_or_default();
        if settings.language != "es" && settings.language != "en" {
            settings.language = "es".into();
        }
        settings
    }

    pub fn load() -> Settings {
        let mut settings = Self::file().map(|f| Self::load_from(&f)).unwrap_or_default();
        if let Ok(api) = std::env::var(API_BASE_ENV) {
            if !api.is_empty() {
                settings.api_base = api;
            }
        }
        settings
    }

    pub fn save_to(&self, path: &Path) -> Result<()> {
        let text = serde_json::to_vec_pretty(self).expect("settings serialize");
        write_atomically(path, &text)
    }

    pub fn save(&self) -> Result<()> {
        match Self::file() {
            Some(file) => self.save_to(&file),
            None => Ok(()),
        }
    }

    /// The data folder: the player's choice or the game's own (so a game started without the
    /// launcher finds the same content, saves and settings).
    pub fn data_folder(&self) -> PathBuf {
        self.data_folder.clone().unwrap_or_else(default_data_folder)
    }

    pub fn uses_default_data_folder(&self) -> bool {
        self.data_folder.as_ref().is_none_or(|f| *f == default_data_folder())
    }
}

/// The game's user data folder: $XDG_DATA_HOME/indie-modding-games/dwe on Linux
/// (~/.local/share/...), the roaming application data folder on Windows.
pub fn default_data_folder() -> PathBuf {
    dirs::data_dir().unwrap_or_else(|| PathBuf::from(".")).join("indie-modding-games").join("dwe")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn settings_round_trip_and_fall_back() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("settings.json");
        assert_eq!(Settings::load_from(&file), Settings::default());
        let settings = Settings {
            language: "en".into(),
            disc_source: Some("/discs/game.cue".into()),
            ..Settings::default()
        };
        settings.save_to(&file).unwrap();
        assert_eq!(Settings::load_from(&file), settings);
        fs::write(&file, r#"{"language":"fr","check_updates":false}"#).unwrap();
        let read = Settings::load_from(&file);
        assert_eq!(read.language, "es");
        assert!(!read.check_updates);
        assert_eq!(read.repository, DEFAULT_REPOSITORY);
        // Settings written before the automatic updates existed turn them on.
        assert!(read.auto_update_launcher && read.auto_update_game);
        let saved = Settings { auto_update_game: false, last_check: Some(LastCheck::checked(5, None, None)), ..Settings::default() };
        saved.save_to(&file).unwrap();
        assert_eq!(Settings::load_from(&file), saved);
    }

    #[test]
    fn the_release_server_is_the_builds_never_a_saved_one() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("settings.json");
        fs::write(&file, r#"{"api_base":"https://api.github.com","language":"en"}"#).unwrap();
        let read = Settings::load_from(&file);
        assert_eq!(read.api_base, DEFAULT_API_BASE);
        assert_eq!(read.language, "en");
        Settings { api_base: "http://127.0.0.1:1".into(), ..read }.save_to(&file).unwrap();
        assert!(!fs::read_to_string(&file).unwrap().contains("api_base"));
        assert!(DEFAULT_API_BASE.starts_with("https://"));
    }
}
