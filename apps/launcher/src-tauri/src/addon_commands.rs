//! The add-on manager's commands (installing, turning on and off, safe mode), called by the
//! launcher's own page only, and `addon_call`, through which the page forwards an add-on's
//! backend calls (addons/bridge.rs). Add-ons never reach these commands themselves: their iframes
//! are sandboxed on another origin, without the launcher's IPC.

use serde::Serialize;
use serde_json::Value;
use std::path::Path;
use std::sync::atomic::Ordering;
use tauri::State;

use crate::addons::{bridge, AddonStore, Listed, Staged};
use crate::content;
use crate::failure::{code, Failure, Result};
use crate::log_line;
use crate::manifest;
use crate::store::GameStore;
use crate::AppState;

#[derive(Serialize)]
pub struct AddonsView {
    safe_mode: bool,
    crashed_last_time: bool,
    addons: Vec<Listed>,
}

fn store(state: &AppState) -> AddonStore {
    AddonStore::new(&state.settings.lock().unwrap().data_folder())
}

fn view(state: &AppState) -> AddonsView {
    let safe_mode = state.addons.safe_mode();
    AddonsView {
        safe_mode,
        crashed_last_time: state.addons.crashed_last_time.load(Ordering::SeqCst),
        addons: store(state).list(safe_mode),
    }
}

#[tauri::command]
pub fn addons_list(state: State<'_, AppState>) -> AddonsView {
    view(&state)
}

/// Unpacks and checks an add-on (a .zip, a folder or its addon.json) and says what it asks for;
/// nothing is installed until `addon_install` with the ticket.
#[tauri::command]
pub async fn addon_inspect(state: State<'_, AppState>, path: String) -> Result<Staged> {
    let store = store(&state);
    let ticket = state.addons.ticket();
    log_line!("checking the add-on {path}");
    let source = path.clone();
    let staged = tauri::async_runtime::spawn_blocking(move || store.stage(Path::new(&source), &ticket))
        .await
        .map_err(|e| Failure::new(code::IO, e.to_string()))?
        .inspect_err(|f| log_line!("the add-on was refused: {f}"))?;
    state.addons.staged.lock().unwrap().insert(staged.ticket.clone(), staged.clone());
    Ok(staged)
}

/// Installs a checked add-on when the player accepted its permissions, or throws it away.
#[tauri::command]
pub fn addon_install(state: State<'_, AppState>, ticket: String, accept: bool) -> Result<AddonsView> {
    let staged = state.addons.staged.lock().unwrap().remove(&ticket).ok_or_else(|| Failure::new(code::ADDON_NOT_FOUND, ticket))?;
    let store = store(&state);
    if accept {
        store.install(&staged)?;
        log_line!("installed the add-on {} {}", staged.manifest.id, staged.manifest.version);
    } else {
        store.discard(&staged);
        log_line!("the player declined the add-on {}", staged.manifest.id);
    }
    Ok(view(&state))
}

#[tauri::command]
pub fn addon_set_enabled(state: State<'_, AppState>, id: String, enabled: bool) -> Result<AddonsView> {
    store(&state).set_enabled(&id, enabled)?;
    log_line!("the add-on {id} is {}", if enabled { "on" } else { "off" });
    Ok(view(&state))
}

#[tauri::command]
pub fn addon_uninstall(state: State<'_, AppState>, id: String) -> Result<AddonsView> {
    store(&state).uninstall(&id)?;
    log_line!("removed the add-on {id}");
    Ok(view(&state))
}

/// Safe mode for this run: no add-on is loaded or served.
#[tauri::command]
pub fn addons_safe_mode(state: State<'_, AppState>, on: bool) -> AddonsView {
    state.addons.safe_mode.store(on, Ordering::SeqCst);
    state.addons.crashed_last_time.store(false, Ordering::SeqCst);
    log_line!("safe mode {}", if on { "on" } else { "off" });
    view(&state)
}

#[tauri::command]
pub fn addons_dismiss_crash(state: State<'_, AppState>) -> AddonsView {
    state.addons.crashed_last_time.store(false, Ordering::SeqCst);
    view(&state)
}

pub fn facts(state: &AppState) -> bridge::Facts {
    let settings = state.settings.lock().unwrap().clone();
    let data = settings.data_folder();
    bridge::Facts {
        language: settings.language,
        game_version: GameStore::new(&data).current().map(|g| g.version),
        content_installed: content::has_base_pack(&content::content_folder(&data)),
        platform: manifest::platform_key(),
    }
}

/// An add-on's backend call, forwarded by the page with the id of the iframe it came from.
#[tauri::command]
pub async fn addon_call(state: State<'_, AppState>, addon: String, method: String, params: Value) -> Result<Value> {
    let call = bridge::parse(&method, &params)?;
    let data = state.settings.lock().unwrap().data_folder();
    let active = AddonStore::new(&data).active(&addon, state.addons.safe_mode());
    bridge::run(&data, &addon, active, call, facts(&state)).await
}
