//! dwe's desktop launcher (docs/modulos/apps/launcher.md): reads the player's disc with dwx,
//! keeps the game up to date from the repository's releases and starts it. Everything that
//! touches the disc, the network or other programs is a command here; the page only
//! shows state and asks for these commands.

pub mod addons;
pub mod auto_update;
pub mod beta_key;
pub mod content;
pub mod custom_images;
pub mod disc;
pub mod failure;
pub mod launch;
pub mod links;
pub mod log;
pub mod manifest;
pub mod releases;
pub mod settings;
pub mod store;
pub mod system_check;
pub mod updates;
pub mod verify;

mod addon_commands;
mod commands;
mod image_commands;

use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use releases::Release;
use settings::Settings;

pub struct AppState {
    pub settings: Mutex<Settings>,
    /// One task (install, update, extraction) at a time.
    pub busy: AtomicBool,
    /// Set by the page's Stop: the running task kills dwx or drops its download.
    pub cancel: Arc<AtomicBool>,
    /// The newest release, as the last check saw it.
    pub release: Mutex<Option<Release>>,
    /// The key the release server asks for (beta_key.rs).
    pub beta_key: beta_key::BetaKey,
    pub game_running: AtomicBool,
    /// Community add-ons: safe mode, the last run's crash, installs waiting for the player.
    pub addons: addons::Runtime,
    /// Removed when the launcher ends cleanly; found at start, the last run crashed.
    pub run_marker: Option<addons::RunMarker>,
    /// The launcher version installed in this session (it runs from the next start).
    pub launcher_ready: Mutex<Option<String>>,
    /// Windows only: the downloaded, verified installer, run when the launcher restarts or closes
    /// (running it closes the launcher).
    pub launcher_pending: Mutex<Option<(tauri_plugin_updater::Update, Vec<u8>)>>,
    /// The last GPU check (system_check.rs) and the game folder it was made for; held while a
    /// check runs, so Play waits for the one the page started.
    pub graphics: tokio::sync::Mutex<Option<(std::path::PathBuf, system_check::GraphicsCheck)>>,
}

/// Runs the launcher installer kept for the restart or the exit (Windows; elsewhere the new
/// launcher was already put in place).
pub fn install_pending_launcher(state: &AppState) {
    let pending = state.launcher_pending.lock().unwrap().take();
    if let Some((update, bytes)) = pending {
        if let Err(error) = update.install(&bytes) {
            log_line!("the launcher {} was not installed: {error}", update.version);
        }
    }
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

/// Starts with every add-on off for this run (also a button after a crash).
pub const SAFE_MODE_FLAG: &str = "--safe-mode";

/// Serves an add-on's files (addons/protocol.rs) to its sandboxed iframe.
fn serve_addon(app: &tauri::AppHandle, path: &str) -> tauri::http::Response<Vec<u8>> {
    use tauri::Manager;
    let state = app.state::<AppState>();
    let store = addons::AddonStore::new(&state.settings.lock().unwrap().data_folder());
    let safe_mode = state.addons.safe_mode();
    let served = addons::protocol::serve(path, |id| store.active(id, safe_mode).map(|_| store.folder(id)));
    let mut response = tauri::http::Response::builder()
        .status(served.status)
        .header("Content-Type", served.content_type)
        .header("X-Content-Type-Options", "nosniff")
        .header("Cache-Control", "no-store")
        .header("Referrer-Policy", "no-referrer");
    if let Some(csp) = served.csp {
        response = response.header("Content-Security-Policy", csp);
    }
    response.body(served.body).expect("a response with plain headers")
}

/// A launcher with the beta key built in no longer needs the one an older launcher kept in the
/// system keyring: removed once (Settings::old_beta_key_removed), on a thread of its own so the
/// window never waits on the keyring.
fn forget_old_beta_key(settings: &mut Settings) {
    if beta_key::BUILT_IN.is_none() || settings.old_beta_key_removed {
        return;
    }
    settings.old_beta_key_removed = true;
    if let Err(failure) = settings.save() {
        log_line!("the settings were not saved: {}", failure.detail);
    }
    std::thread::spawn(|| beta_key::remove_stored(&beta_key::Keyring));
}

pub fn run() {
    let mut settings = Settings::load();
    log::open(&settings.data_folder());
    forget_old_beta_key(&mut settings);
    let safe_mode = std::env::args().any(|a| a == SAFE_MODE_FLAG);
    let addon_store = addons::AddonStore::new(&settings.data_folder());
    addon_store.clean_staging();
    let (run_marker, crashed) = match Settings::file().and_then(|f| f.parent().map(Path::to_path_buf)) {
        Some(folder) => {
            let (marker, crashed) = addons::RunMarker::create(&folder);
            (Some(marker), crashed && addon_store.any_enabled())
        }
        None => (None, false),
    };
    if safe_mode {
        log_line!("safe mode: no add-on is loaded");
    } else if crashed {
        log_line!("the last run did not end cleanly while add-ons were on");
    }
    let state = AppState {
        settings: Mutex::new(settings),
        busy: AtomicBool::new(false),
        cancel: Arc::new(AtomicBool::new(false)),
        release: Mutex::new(None),
        beta_key: beta_key::BetaKey::from_build(),
        game_running: AtomicBool::new(false),
        addons: addons::Runtime::new(safe_mode, crashed),
        run_marker,
        launcher_ready: Mutex::new(None),
        launcher_pending: Mutex::new(None),
        graphics: tokio::sync::Mutex::new(None),
    };
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .register_uri_scheme_protocol(addons::protocol::SCHEME, |ctx, request| serve_addon(ctx.app_handle(), request.uri().path()))
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
            commands::open_logs,
            commands::install_launcher_update,
            commands::restart_launcher,
            commands::set_auto_update,
            commands::set_update_channel,
            commands::check_system,
            commands::open_driver_page,
            addon_commands::addons_list,
            addon_commands::addon_inspect,
            addon_commands::addon_install,
            addon_commands::addon_set_enabled,
            addon_commands::addon_uninstall,
            addon_commands::addons_safe_mode,
            addon_commands::addons_dismiss_crash,
            addon_commands::addon_call,
            image_commands::images_overview,
            image_commands::images_prepare,
            image_commands::images_inspect,
            image_commands::images_apply,
            image_commands::images_assign,
            image_commands::images_reset,
            image_commands::images_thumbnail,
            image_commands::images_set_enabled,
            image_commands::images_remove_all,
            image_commands::images_export,
        ])
        .setup(|app| {
            log_line!("dwe-launcher {} starting", app.package_info().version);
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("the launcher window opens")
        .run(|app, event| {
            if let tauri::RunEvent::Exit = event {
                use tauri::Manager;
                let state = app.state::<AppState>();
                if let Some(marker) = &state.run_marker {
                    marker.clear();
                }
                install_pending_launcher(&state);
            }
        });
}
