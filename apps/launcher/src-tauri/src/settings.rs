//! The launcher's own settings (settings.json in the system's configuration folder, never in the
//! data folder, which the player may change): where the data goes, the language, the release server
//! and the disc the content was read from. The beta key is never here (beta_key.rs).

use serde::{Deserialize, Serialize};
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};

use crate::auto_update::LastCheck;
use crate::failure::Result;
use crate::releases::UpdateChannel;
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
/// The launcher's languages (their first BCP 47 part), in the order the language box lists them;
/// the interface keeps the same list with their names (ui/js/i18n.js, LANGUAGES).
pub const LANGUAGES: [&str; 9] = ["en", "es", "pt", "fr", "de", "ru", "zh", "ko", "ja"];
/// The language of whatever has no text in the player's (the custom images' guide, add-ons).
pub const FALLBACK_LANGUAGE: &str = "en";
/// How to start the game. "auto" probes the GPU and uses the package's software driver only when
/// nothing else works (the default); "skip" starts as-is, without probing or changing drivers;
/// "software" starts on the package's software driver, without probing. The last two are manual
/// ways out of a wrong check: Play never refuses to start in any of them.
pub const LAUNCH_MODES: [&str; 3] = ["auto", "skip", "software"];
/// Limits for the manual launch options, so a saved settings.json cannot turn into a huge command
/// line or environment. The page parses the same text; a bad line is ignored, never fatal.
const MAX_EXTRA_ARGS: usize = 32;
const MAX_EXTRA_ENV: usize = 32;
const MAX_ARG_BYTES: usize = 256;
const MAX_VALUE_BYTES: usize = 2048;

pub fn is_language(code: &str) -> bool {
    LANGUAGES.contains(&code)
}

pub fn is_launch_mode(mode: &str) -> bool {
    LAUNCH_MODES.contains(&mode)
}

fn valid_env_name(name: &str) -> bool {
    let mut bytes = name.bytes();
    bytes.next().is_some_and(|b| b.is_ascii_alphabetic() || b == b'_')
        && bytes.all(|b| b.is_ascii_alphanumeric() || b == b'_')
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct Settings {
    /// None: the game's own data folder (data_folder()).
    pub data_folder: Option<PathBuf>,
    /// One of LANGUAGES, or empty while none was chosen (a new install): the interface then
    /// picks the system's, or English, and saves it.
    pub language: String,
    /// Look for updates every time the launcher opens.
    pub check_updates: bool,
    /// Install a new launcher found by a check without asking (auto_update.rs).
    pub auto_update_launcher: bool,
    /// Install a new game found by a check without asking (auto_update.rs).
    pub auto_update_game: bool,
    /// The releases the updates come from (releases::UpdateChannel). A new install starts on
    /// Stable (Settings::default); a settings file written before the channels existed belongs
    /// to a tester of the first builds, so a missing field reads as Testing (legacy_channel).
    #[serde(default = "legacy_channel")]
    pub update_channel: UpdateChannel,
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
    /// The compatibility mode (launch.rs, system_check.rs): starts the game with `--compatible`,
    /// which accepts a Vulkan 1.2 GPU that offers dynamic rendering and synchronization2 through
    /// their extensions instead of requiring Vulkan 1.3 core. For old cards and drivers.
    pub compatibility_mode: bool,
    /// The low-performance mode: starts the game with `--low-performance`, the lightest graphics
    /// preset (every heavy option off), for weak GPUs and old iGPUs.
    pub low_performance_mode: bool,
    /// The AMD RX 6000 safe mode: starts the game with `--rdna2-safe`, which simplifies the
    /// bindless layout. The game ignores it on other GPUs. Off by default.
    pub rdna2_safe_mode: bool,
    /// One of LAUNCH_MODES. Missing or unknown in an older settings.json: "auto".
    pub launch_mode: String,
    /// Extra game arguments, separated by whitespace, added after the launcher's own ones. Not
    /// passed to the GPU probe: a flag the game accepts but the probe does not would make the
    /// check look wrong.
    pub extra_args: String,
    /// Extra game environment, one NAME=value per line (# starts a comment). Lines the launcher
    /// does not understand are ignored, so a wrong line never blocks the start.
    pub extra_env: String,
    /// The beta key an older launcher kept in the system keyring was removed (lib.rs,
    /// forget_old_beta_key): done once, by the first launcher with the key built in.
    pub old_beta_key_removed: bool,
}

fn default_api_base() -> String {
    DEFAULT_API_BASE.into()
}

fn legacy_channel() -> UpdateChannel {
    UpdateChannel::Testing
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            data_folder: None,
            language: String::new(),
            check_updates: true,
            auto_update_launcher: true,
            auto_update_game: true,
            update_channel: UpdateChannel::Stable,
            last_check: None,
            api_base: DEFAULT_API_BASE.into(),
            repository: DEFAULT_REPOSITORY.into(),
            disc_source: None,
            compatibility_mode: false,
            low_performance_mode: false,
            rdna2_safe_mode: false,
            launch_mode: "auto".into(),
            extra_args: String::new(),
            extra_env: String::new(),
            old_beta_key_removed: false,
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
        if !is_language(&settings.language) {
            settings.language.clear();
        }
        settings
    }

    /// The language to use now: the chosen one, or English before the interface chose one.
    pub fn language_or_fallback(&self) -> &str {
        if self.language.is_empty() { FALLBACK_LANGUAGE } else { &self.language }
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

    /// The data folder: the player's choice or the default one (default_data_folder).
    pub fn data_folder(&self) -> PathBuf {
        self.data_folder.clone().unwrap_or_else(default_data_folder)
    }

    pub fn uses_default_data_folder(&self) -> bool {
        self.data_folder.as_ref().is_none_or(|f| *f == default_data_folder())
    }

    /// A hand-edited settings.json may hold anything; an unknown mode means the usual check.
    pub fn launch_mode_or_default(&self) -> &str {
        if is_launch_mode(&self.launch_mode) { &self.launch_mode } else { "auto" }
    }

    /// The manual arguments, split on whitespace. A command built by Rust gets these as separate
    /// arguments: no shell interprets them, so no quoting or shell escape is needed (or supported).
    pub fn game_args(&self) -> Vec<String> {
        self.extra_args
            .split_whitespace()
            .filter(|arg| arg.len() <= MAX_ARG_BYTES)
            .take(MAX_EXTRA_ARGS)
            .map(str::to_string)
            .collect()
    }

    /// The manual environment variables. The launcher's own (software driver) variables go first
    /// in play(), so these win: a player can name a driver the auto-detection did not find.
    pub fn game_env(&self) -> Vec<(OsString, OsString)> {
        self.extra_env
            .lines()
            .take(MAX_EXTRA_ENV + 1)
            .filter_map(|line| {
                let line = line.trim();
                if line.is_empty() || line.starts_with('#') {
                    return None;
                }
                let (name, value) = line.split_once('=')?;
                let (name, value) = (name.trim(), value.trim());
                if !valid_env_name(name) || value.len() > MAX_VALUE_BYTES {
                    return None;
                }
                Some((OsString::from(name), OsString::from(value)))
            })
            .take(MAX_EXTRA_ENV)
            .collect()
    }
}

/// The launcher's default data folder. On Linux and macOS it is the game's own
/// (game_data_folder), so a game started without the launcher finds the same content. On Windows
/// it is the local application data folder (%LOCALAPPDATA%\indie-modding-games\dwe): the
/// installed versions and the content are hundreds of megabytes, which must not go to the roaming
/// profile. The game still keeps its saves and settings in its own folder there, since the launcher
/// only moves them when the player picks a folder (launch::command).
pub fn default_data_folder() -> PathBuf {
    default_data_folder_for(cfg!(windows), dirs::data_dir(), dirs::data_local_dir())
}

pub fn default_data_folder_for(windows: bool, data: Option<PathBuf>, local: Option<PathBuf>) -> PathBuf {
    let base = if windows { local.or(data) } else { data };
    base.unwrap_or_else(|| PathBuf::from(".")).join("indie-modding-games").join("dwe")
}

/// The game's own user data folder, where it keeps its saves and settings when started without
/// options (SDL's preference folder): $XDG_DATA_HOME/indie-modding-games/dwe on Linux
/// (~/.local/share/...), %APPDATA%\indie-modding-games\dwe on Windows and
/// ~/Library/Application Support/indie-modding-games/dwe on macOS.
pub fn game_data_folder() -> PathBuf {
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
        fs::write(&file, r#"{"language":"xx","check_updates":false}"#).unwrap();
        let read = Settings::load_from(&file);
        assert_eq!(read.language, "");
        assert!(!read.check_updates);
        assert_eq!(read.repository, DEFAULT_REPOSITORY);
        // Settings written before the automatic updates existed turn them on.
        assert!(read.auto_update_launcher && read.auto_update_game);
        // Old settings.json files load the switch off without losing the rest.
        assert!(!read.rdna2_safe_mode);
        let saved = Settings { auto_update_game: false, last_check: Some(LastCheck::checked(5, None, None)), ..Settings::default() };
        saved.save_to(&file).unwrap();
        assert_eq!(Settings::load_from(&file), saved);
    }

    #[test]
    fn manual_launch_options_default_to_auto_and_ignore_bad_lines() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("settings.json");
        // A settings.json from before these options existed, and one edited by hand with a mode we
        // do not know: both start the game the usual way.
        assert_eq!(Settings::load_from(&file).launch_mode_or_default(), "auto");
        fs::write(&file, r#"{"launch_mode":"wrong"}"#).unwrap();
        assert_eq!(Settings::load_from(&file).launch_mode_or_default(), "auto");

        let saved = Settings {
            launch_mode: "skip".into(),
            extra_args: "  --compatible   --foo  ".into(),
            extra_env: "# a comment\nVK_DRIVER_FILES=/x/y.json\nBAD-NAME=1\nNOEQUAL\nGOOD= a = b \n".into(),
            ..Settings::default()
        };
        saved.save_to(&file).unwrap();
        let read = Settings::load_from(&file);
        assert_eq!(read.launch_mode_or_default(), "skip");
        assert_eq!(read.game_args(), ["--compatible", "--foo"]);
        let env = read.game_env();
        assert_eq!(env.len(), 2, "{env:?}");
        assert_eq!(env[0].0.to_string_lossy(), "VK_DRIVER_FILES");
        assert_eq!(env[0].1.to_string_lossy(), "/x/y.json");
        assert_eq!(env[1].0.to_string_lossy(), "GOOD");
        assert_eq!(env[1].1.to_string_lossy(), "a = b");

        // Too many arguments or variables do not fail the start; they are simply left out.
        let many = Settings { extra_args: "x ".repeat(40), extra_env: "A=1\n".repeat(40), ..Settings::default() };
        assert_eq!(many.game_args().len(), 32);
        assert_eq!(many.game_env().len(), 32);
    }

    #[test]
    fn a_new_install_leaves_the_language_to_the_system_and_a_saved_one_stays() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("settings.json");
        // No settings file, or one without a language: not chosen yet, English until it is.
        let new = Settings::load_from(&file);
        assert_eq!(new.language, "");
        assert_eq!(new.language_or_fallback(), FALLBACK_LANGUAGE);
        fs::write(&file, r#"{"check_updates":true}"#).unwrap();
        assert_eq!(Settings::load_from(&file).language, "");
        // Every supported language survives a load; the ones an older launcher saved too.
        for code in LANGUAGES {
            fs::write(&file, format!(r#"{{"language":"{code}"}}"#)).unwrap();
            let read = Settings::load_from(&file);
            assert_eq!(read.language, code);
            assert_eq!(read.language_or_fallback(), code);
        }
        // An unknown or regional code is not a choice: asked again.
        for code in ["pt-BR", "EN", "zh-Hans", "it"] {
            fs::write(&file, format!(r#"{{"language":"{code}"}}"#)).unwrap();
            assert_eq!(Settings::load_from(&file).language, "", "{code}");
        }
        assert!(is_language("es") && is_language("ja") && !is_language(""));
    }

    #[test]
    fn a_new_install_is_stable_and_an_older_one_stays_testing() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("settings.json");
        // No settings file: a new install.
        assert_eq!(Settings::load_from(&file).update_channel, UpdateChannel::Stable);
        // A file written before the channels existed: a tester of the first builds.
        fs::write(&file, r#"{"language":"en","check_updates":true}"#).unwrap();
        assert_eq!(Settings::load_from(&file).update_channel, UpdateChannel::Testing);
        // Either choice survives a save and a load.
        for channel in [UpdateChannel::Stable, UpdateChannel::Testing] {
            let saved = Settings { update_channel: channel, ..Settings::default() };
            saved.save_to(&file).unwrap();
            assert!(fs::read_to_string(&file).unwrap().contains(&format!("\"update_channel\": \"{channel}\"")));
            assert_eq!(Settings::load_from(&file), saved);
        }
        fs::write(&file, r#"{"update_channel":"testing"}"#).unwrap();
        assert_eq!(Settings::load_from(&file).update_channel, UpdateChannel::Testing);
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

    #[test]
    fn windows_keeps_the_data_out_of_the_roaming_profile() {
        let roaming = Some(PathBuf::from(r"C:\Users\p\AppData\Roaming"));
        let local = Some(PathBuf::from(r"C:\Users\p\AppData\Local"));
        let windows = default_data_folder_for(true, roaming.clone(), local.clone());
        assert_eq!(windows, local.clone().unwrap().join("indie-modding-games").join("dwe"));
        assert_eq!(default_data_folder_for(true, roaming.clone(), None), roaming.clone().unwrap().join("indie-modding-games").join("dwe"));
        let unix = Some(PathBuf::from("/home/p/.local/share"));
        assert_eq!(default_data_folder_for(false, unix.clone(), unix.clone()), PathBuf::from("/home/p/.local/share/indie-modding-games/dwe"));
        if cfg!(not(windows)) {
            assert_eq!(default_data_folder(), game_data_folder());
        }
    }
}
