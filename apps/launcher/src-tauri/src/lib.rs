//! dwe's desktop launcher (docs/modulos/apps/launcher.md): reads the player's disc with dwx,
//! keeps the game up to date from the repository's releases and starts it. Everything that
//! touches the disc, the keyring, the network or other programs is a command here; the page only
//! shows state and asks for these commands.

pub mod content;
pub mod disc;
pub mod failure;
pub mod launch;
pub mod log;
pub mod manifest;
pub mod releases;
pub mod settings;
pub mod store;
pub mod token;
pub mod updates;
pub mod verify;

mod commands;

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use releases::Release;
use settings::Settings;
use token::TokenStore;

pub struct AppState {
    pub settings: Mutex<Settings>,
    /// One task (install, update, extraction) at a time.
    pub busy: AtomicBool,
    /// Set by the page's Stop: the running task kills dwx or drops its download.
    pub cancel: Arc<AtomicBool>,
    /// The newest release, as the last check saw it.
    pub release: Mutex<Option<Release>>,
    pub tokens: Box<dyn TokenStore>,
    pub game_running: AtomicBool,
}

/// Held while a task runs; the next one is refused until it drops.
pub struct BusyGuard<'a>(&'a AtomicBool);

impl AppState {
    pub fn begin(&self) -> failure::Result<BusyGuard<'_>> {
        if self.busy.swap(true, Ordering::SeqCst) {
            return Err(failure::Failure::new(failure::code::BUSY, ""));
        }
        self.cancel.store(false, Ordering::SeqCst);
        Ok(BusyGuard(&self.busy))
    }
}

impl Drop for BusyGuard<'_> {
    fn drop(&mut self) {
        self.0.store(false, Ordering::SeqCst);
    }
}

pub fn run() {
    let settings = Settings::load();
    log::open(&settings.data_folder());
    log_line!("dwe-launcher {} starting", env!("CARGO_PKG_VERSION"));
    let state = AppState {
        settings: Mutex::new(settings),
        busy: AtomicBool::new(false),
        cancel: Arc::new(AtomicBool::new(false)),
        release: Mutex::new(None),
        tokens: Box::new(token::Keyring),
        game_running: AtomicBool::new(false),
    };
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .manage(state)
        .invoke_handler(tauri::generate_handler![
            commands::status,
            commands::background,
            commands::check_updates,
            commands::install_disc,
            commands::reextract,
            commands::update_game,
            commands::rollback_game,
            commands::cancel,
            commands::play,
            commands::set_language,
            commands::set_check_updates,
            commands::set_data_folder,
            commands::save_token,
            commands::clear_token,
            commands::open_logs,
            commands::install_launcher_update,
        ])
        .run(tauri::generate_context!())
        .expect("the launcher window opens");
}
