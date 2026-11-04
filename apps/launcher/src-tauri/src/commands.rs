//! The commands the launcher's page calls. They are the launcher's privileged surface (disc,
//! keyring, downloads, starting programs): nothing else in the page can do these things, and a
//! future add-on bridge must not forward them.

use base64::Engine;
use serde::Serialize;
use std::path::{Path, PathBuf};
use std::sync::atomic::Ordering;
use tauri::{AppHandle, Emitter, Manager, State};
use tauri_plugin_opener::OpenerExt;
use tauri_plugin_updater::UpdaterExt;

use crate::content::{self, Identity};
use crate::disc::{self, Pick};
use crate::failure::{code, Failure, Result};
use crate::launch;
use crate::log_line;
use crate::manifest;
use crate::releases::{Client, Release};
use crate::store::{Game, GameStore};
use crate::token;
use crate::updates::{self, Stage};
use crate::verify;
use crate::AppState;

#[derive(Serialize)]
pub struct Formats {
    images: &'static [&'static str],
    archives: &'static [&'static str],
    unsupported: &'static [&'static str],
}

#[derive(Serialize)]
pub struct Status {
    language: String,
    launcher_version: &'static str,
    platform: String,
    data_folder: String,
    own_data_folder: bool,
    check_updates: bool,
    game: Option<Game>,
    has_previous: bool,
    content_installed: bool,
    content_outdated: bool,
    content_extractor_version: Option<i32>,
    disc_source: Option<String>,
    disc_source_exists: bool,
    /// "saved", "none" or "unavailable" (no system keyring).
    token: &'static str,
    signing_ready: bool,
    busy: bool,
    game_running: bool,
    formats: Formats,
}

#[derive(Serialize, Clone)]
struct TaskEvent {
    /// unpack, download, verify, install, identify, extract.
    task: &'static str,
    percent: Option<f64>,
    step: Option<String>,
    done: u64,
    total: u64,
}

fn emit(app: &AppHandle, task: &'static str, percent: Option<f64>, step: Option<String>, done: u64, total: u64) {
    let _ = app.emit("task", TaskEvent { task, percent, step, done, total });
}

#[derive(Serialize, Clone)]
struct GameExit {
    code: Option<i32>,
    failed: bool,
    log: Vec<String>,
}

#[derive(Serialize)]
pub struct UpdateInfo {
    release_name: String,
    tag: String,
    notes: String,
    published_at: Option<String>,
    prerelease: bool,
    game_version: String,
    game_update: bool,
    download_bytes: Option<u64>,
    /// The update reads the disc again (another extractor version).
    reads_disc_again: bool,
    launcher_update: Option<String>,
}

/// The public key the build trusts (the updater's, in tauri.conf.json); none in a development
/// build, which then refuses every update.
fn public_key_text(app: &AppHandle) -> Option<String> {
    app.config().plugins.0.get("updater")?.get("pubkey")?.as_str().map(str::to_string)
}

fn public_key(app: &AppHandle) -> Option<minisign_verify::PublicKey> {
    verify::public_key(&public_key_text(app)?)
}

fn data_folder(state: &AppState) -> PathBuf {
    state.settings.lock().unwrap().data_folder()
}

fn status_of(app: &AppHandle, state: &AppState) -> Status {
    let settings = state.settings.lock().unwrap().clone();
    let data = settings.data_folder();
    let store = GameStore::new(&data);
    let game = store.current();
    let content = content::content_folder(&data);
    let installed = content::has_base_pack(&content);
    let record = content::read_record(&content);
    let token = match state.tokens.get() {
        Ok(Some(_)) => "saved",
        Ok(None) => "none",
        Err(_) => "unavailable",
    };
    Status {
        language: settings.language.clone(),
        launcher_version: env!("CARGO_PKG_VERSION"),
        platform: manifest::platform_key(),
        data_folder: data.display().to_string(),
        own_data_folder: settings.uses_default_data_folder(),
        check_updates: settings.check_updates,
        has_previous: store.has_previous(),
        content_installed: installed,
        content_outdated: game.as_ref().is_some_and(|g| content::is_outdated(&content, g.extractor_version)),
        content_extractor_version: record.map(|r| r.extractor_version),
        disc_source_exists: settings.disc_source.as_ref().is_some_and(|d| d.is_file()),
        disc_source: settings.disc_source.as_ref().map(|d| d.display().to_string()),
        game,
        token,
        signing_ready: public_key(app).is_some(),
        busy: state.busy.load(Ordering::SeqCst),
        game_running: state.game_running.load(Ordering::SeqCst),
        formats: Formats {
            images: &disc::IMAGE_EXTENSIONS,
            archives: &disc::ARCHIVE_EXTENSIONS,
            unsupported: &disc::UNSUPPORTED_EXTENSIONS,
        },
    }
}

#[tauri::command]
pub fn status(app: AppHandle, state: State<'_, AppState>) -> Status {
    status_of(&app, &state)
}

/// Images of the player's own extracted content that make a good background, first found wins.
const BACKGROUNDS: [&str; 3] = ["map/gias05/background_day.png", "map/mayo06/background_day.png", "texture/etcna/title2.png"];

/// A still from the player's own content (never shipped, never uploaded), as a data URL.
#[tauri::command]
pub fn background(state: State<'_, AppState>) -> Option<String> {
    let base = content::content_folder(&data_folder(&state)).join(content::BASE_PACK);
    BACKGROUNDS.iter().find_map(|relative| {
        let bytes = std::fs::read(base.join(relative)).ok()?;
        Some(format!("data:image/png;base64,{}", base64::engine::general_purpose::STANDARD.encode(bytes)))
    })
}

fn client(state: &AppState) -> Result<Client> {
    let settings = state.settings.lock().unwrap().clone();
    let token = state.tokens.get().ok().flatten();
    Client::new(&settings.api_base, &settings.repository, token)
}

async fn latest_release(state: &AppState) -> Result<Release> {
    let release = client(state)?.latest().await?;
    *state.release.lock().unwrap() = Some(release.clone());
    Ok(release)
}

async fn cached_or_latest(state: &AppState) -> Result<Release> {
    let cached = state.release.lock().unwrap().clone();
    match cached {
        Some(release) => Ok(release),
        None => latest_release(state).await,
    }
}

#[tauri::command]
pub async fn check_updates(state: State<'_, AppState>) -> Result<UpdateInfo> {
    let release = latest_release(&state).await?;
    let data = data_folder(&state);
    let game = GameStore::new(&data).current();
    let mut launcher_update = None;
    if let Some(asset) = release.launcher_manifest() {
        if let Ok(text) = client(&state)?.fetch_text(asset).await {
            launcher_update = serde_json::from_str::<serde_json::Value>(&text)
                .ok()
                .and_then(|v| v.get("version")?.as_str().map(str::to_string))
                .filter(|v| manifest::is_newer(v, Some(env!("CARGO_PKG_VERSION"))));
        }
    }
    let game_update = updates::is_update(&release, game.as_ref());
    log_line!("release {}: game {} (update: {game_update})", release.tag, release.manifest.version);
    Ok(UpdateInfo {
        release_name: release.name.clone(),
        tag: release.tag.clone(),
        notes: release.notes.clone(),
        published_at: release.published_at.clone(),
        prerelease: release.prerelease,
        game_version: release.manifest.version.clone(),
        game_update,
        download_bytes: updates::download_size(&release),
        reads_disc_again: game.as_ref().is_some_and(|g| g.extractor_version != Some(release.manifest.extractor_version)),
        launcher_update,
    })
}

async fn download_game(app: &AppHandle, state: &AppState, release: &Release) -> Result<Game> {
    let data = data_folder(state);
    let key = public_key(app);
    let client = client(state)?;
    let app = app.clone();
    updates::install_game(&client, release, &data, key.as_ref(), &state.cancel, move |stage, done, total| {
        let task = match stage {
            Stage::Download => "download",
            Stage::Verify => "verify",
            Stage::Install => "install",
        };
        let percent = (total > 0 && stage == Stage::Download).then(|| done as f64 * 100.0 / total as f64);
        emit(&app, task, percent, None, done, total);
    })
    .await
}

/// The installed game, or the newest release's when there is none yet.
async fn ensure_game(app: &AppHandle, state: &AppState) -> Result<Game> {
    if let Some(game) = GameStore::new(&data_folder(state)).current() {
        return Ok(game);
    }
    let release = cached_or_latest(state)
        .await
        .map_err(|f| if f.code == code::NETWORK || f.code == code::NO_RELEASE { Failure::new(code::NO_GAME, f.detail) } else { f })?;
    download_game(app, state, &release).await
}

fn joined<T>(result: std::result::Result<Result<T>, tauri::Error>) -> Result<T> {
    result.map_err(|e| Failure::new(code::IO, e.to_string()))?
}

/// Reads the disc (or the archive holding it) into the content folder.
async fn install_from(app: &AppHandle, state: &AppState, source: &Path) -> Result<()> {
    let data = data_folder(state);
    let source = std::path::absolute(source).unwrap_or_else(|_| source.to_path_buf());
    let pick = disc::classify(&source)?;
    let unpacked = data.join("tmp").join("disc");
    let _ = std::fs::remove_dir_all(&unpacked);
    let result = async {
        let image = match &pick {
            Pick::Image(path) => path.clone(),
            archive => {
                emit(app, "unpack", None, None, 0, 0);
                let (archive, target) = (archive.clone(), unpacked.clone());
                joined(tauri::async_runtime::spawn_blocking(move || disc::unpack(&archive, &target)).await)?
            }
        };
        if let Some(missing) = disc::missing_cue_files(&image).into_iter().next() {
            return Err(Failure::new(code::MISSING_FILE, missing));
        }
        let game = ensure_game(app, state).await?;
        emit(app, "identify", None, None, 0, 0);
        let (dwx, probe, cancel) = (game.dwx(), image.clone(), state.cancel.clone());
        let identity = joined(tauri::async_runtime::spawn_blocking(move || content::identify(&dwx, &probe, &cancel)).await)?;
        match identity {
            Identity::Supported(version) => log_line!("the disc is the supported release ({version})"),
            Identity::OtherVersion(version) => return Err(Failure::new(code::OTHER_REGION, version)),
            Identity::Unknown => return Err(Failure::new(code::NOT_THE_GAME, "")),
        }
        emit(app, "extract", Some(0.0), Some("read".into()), 0, 0);
        let (dwx, cancel, handle) = (game.dwx(), state.cancel.clone(), app.clone());
        let (image, recorded, version, folder) = (image.clone(), source.clone(), game.extractor_version, data.clone());
        joined(
            tauri::async_runtime::spawn_blocking(move || {
                content::extract(&dwx, &image, &recorded, version, &folder, &cancel, |p| {
                    emit(&handle, "extract", Some(f64::from(p.percent)), Some(p.step), 0, 0);
                })
            })
            .await,
        )?;
        let mut settings = state.settings.lock().unwrap();
        settings.disc_source = Some(source.clone());
        settings.save()
    }
    .await;
    let _ = std::fs::remove_dir_all(&unpacked);
    result
}

#[tauri::command]
pub async fn install_disc(app: AppHandle, state: State<'_, AppState>, path: String) -> Result<Status> {
    let _busy = state.begin()?;
    log_line!("the player picked {path}");
    install_from(&app, &state, Path::new(&path)).await?;
    Ok(status_of(&app, &state))
}

/// Reads the disc again: from the given file, or from the one the content was read from.
#[tauri::command]
pub async fn reextract(app: AppHandle, state: State<'_, AppState>, path: Option<String>) -> Result<Status> {
    let _busy = state.begin()?;
    let source = match path {
        Some(path) => PathBuf::from(path),
        None => remembered_disc(&state)?,
    };
    install_from(&app, &state, &source).await?;
    Ok(status_of(&app, &state))
}

fn remembered_disc(state: &AppState) -> Result<PathBuf> {
    let source = state.settings.lock().unwrap().disc_source.clone();
    match source {
        Some(path) if path.is_file() => Ok(path),
        Some(path) => Err(Failure::new(code::DISC_MOVED, path.display().to_string())),
        None => Err(Failure::new(code::DISC_MOVED, "")),
    }
}

/// Installs the newest game; when it reads discs differently, reads the remembered disc again
/// (or says the disc moved, and the page asks for it).
#[tauri::command]
pub async fn update_game(app: AppHandle, state: State<'_, AppState>) -> Result<Status> {
    let _busy = state.begin()?;
    let release = cached_or_latest(&state).await?;
    let game = download_game(&app, &state, &release).await?;
    let content = content::content_folder(&data_folder(&state));
    if content::is_outdated(&content, game.extractor_version) {
        log_line!("the new game reads discs differently: reading the disc again");
        let source = remembered_disc(&state)?;
        install_from(&app, &state, &source).await?;
    }
    Ok(status_of(&app, &state))
}

#[tauri::command]
pub fn rollback_game(app: AppHandle, state: State<'_, AppState>) -> Result<Status> {
    let _busy = state.begin()?;
    let game = GameStore::new(&data_folder(&state)).rollback()?;
    log_line!("back to the game {}", game.version);
    Ok(status_of(&app, &state))
}

#[tauri::command]
pub fn cancel(state: State<'_, AppState>) {
    state.cancel.store(true, Ordering::SeqCst);
}

/// Starts the game, hides the launcher while it runs and shows it again when it ends, with the
/// end of the game's log when it failed.
#[tauri::command]
pub fn play(app: AppHandle, state: State<'_, AppState>) -> Result<()> {
    if state.game_running.load(Ordering::SeqCst) {
        return Ok(());
    }
    let (data, own) = {
        let settings = state.settings.lock().unwrap();
        (settings.data_folder(), settings.uses_default_data_folder())
    };
    let game = GameStore::new(&data).current().ok_or_else(|| Failure::new(code::NO_GAME, ""))?;
    let content = content::content_folder(&data);
    if !content::has_base_pack(&content) {
        return Err(Failure::new(code::MISSING_FILE, content.display().to_string()));
    }
    let mut child = launch::start(&game, &data, own)?;
    log_line!("the game {} started", game.version);
    state.game_running.store(true, Ordering::SeqCst);
    if let Some(window) = app.get_webview_window("main") {
        let _ = window.hide();
    }
    let log = launch::game_log(&data);
    std::thread::spawn(move || {
        let code = child.wait().ok().and_then(|s| s.code());
        let failed = code != Some(0);
        log_line!("the game ended (exit {code:?})");
        app.state::<AppState>().game_running.store(false, Ordering::SeqCst);
        if let Some(window) = app.get_webview_window("main") {
            let _ = window.show();
            let _ = window.unminimize();
            let _ = window.set_focus();
        }
        let tail = if failed { launch::tail(&log, 30) } else { Vec::new() };
        let _ = app.emit("game-exited", GameExit { code, failed, log: tail });
    });
    Ok(())
}

#[tauri::command]
pub fn set_language(app: AppHandle, state: State<'_, AppState>, language: String) -> Result<Status> {
    if language != "es" && language != "en" {
        return Err(Failure::new(code::IO, language));
    }
    {
        let mut settings = state.settings.lock().unwrap();
        settings.language = language;
        settings.save()?;
    }
    Ok(status_of(&app, &state))
}

#[tauri::command]
pub fn set_check_updates(app: AppHandle, state: State<'_, AppState>, enabled: bool) -> Result<Status> {
    {
        let mut settings = state.settings.lock().unwrap();
        settings.check_updates = enabled;
        settings.save()?;
    }
    Ok(status_of(&app, &state))
}

/// Moves where the launcher keeps the game, the content, the saves and the logs from now on
/// (nothing is moved: the new folder starts empty unless it already has them). None: back to the
/// game's own folder.
#[tauri::command]
pub fn set_data_folder(app: AppHandle, state: State<'_, AppState>, path: Option<String>) -> Result<Status> {
    let _busy = state.begin()?;
    {
        let mut settings = state.settings.lock().unwrap();
        settings.data_folder = path.map(PathBuf::from).filter(|p| p.is_absolute());
        settings.save()?;
        crate::log::open(&settings.data_folder());
        log_line!("data folder: {}", settings.data_folder().display());
    }
    *state.release.lock().unwrap() = None;
    Ok(status_of(&app, &state))
}

#[tauri::command]
pub fn save_token(app: AppHandle, state: State<'_, AppState>, token: String) -> Result<Status> {
    match token::clean(&token) {
        Some(token) => state.tokens.set(&token)?,
        None => state.tokens.clear()?,
    }
    log_line!("the GitHub token was saved in the system keyring");
    *state.release.lock().unwrap() = None;
    Ok(status_of(&app, &state))
}

#[tauri::command]
pub fn clear_token(app: AppHandle, state: State<'_, AppState>) -> Result<Status> {
    state.tokens.clear()?;
    log_line!("the GitHub token was removed");
    Ok(status_of(&app, &state))
}

#[tauri::command]
pub fn open_logs(app: AppHandle, state: State<'_, AppState>) -> Result<()> {
    let folder = crate::log::logs_folder(&data_folder(&state));
    std::fs::create_dir_all(&folder)?;
    app.opener()
        .open_path(folder.display().to_string(), None::<&str>)
        .map_err(|e| Failure::new(code::IO, e.to_string()))
}

/// Replaces the launcher with the release's one (the Tauri updater: latest.json, signed with
/// the same key) and restarts it.
#[tauri::command]
pub async fn install_launcher_update(app: AppHandle, state: State<'_, AppState>) -> Result<()> {
    let _busy = state.begin()?;
    let key = public_key_text(&app)
        .filter(|k| verify::public_key(k).is_some())
        .ok_or_else(|| Failure::new(code::NO_SIGNING_KEY, ""))?;
    let release = cached_or_latest(&state).await?;
    let asset = release.launcher_manifest().ok_or_else(|| Failure::new(code::NO_RELEASE, "latest.json"))?;
    let url = asset.url.parse().map_err(|_| Failure::new(code::BAD_MANIFEST, asset.url.clone()))?;
    let updater_failure = |e: tauri_plugin_updater::Error| Failure::new(code::NETWORK, crate::log::redact(&e.to_string()));
    let mut builder = app.updater_builder().pubkey(key).endpoints(vec![url]).map_err(updater_failure)?;
    builder = builder.header("Accept", "application/octet-stream").map_err(updater_failure)?;
    for (name, value) in client(&state)?.auth_headers() {
        builder = builder.header(name, value).map_err(updater_failure)?;
    }
    let update = builder.build().map_err(updater_failure)?.check().await.map_err(updater_failure)?;
    let Some(update) = update else {
        return Ok(());
    };
    log_line!("installing the launcher {}", update.version);
    let handle = app.clone();
    update
        .download_and_install(
            move |chunk, total| {
                emit(&handle, "launcher", None, None, chunk as u64, total.unwrap_or(0));
            },
            || {},
        )
        .await
        .map_err(updater_failure)?;
    app.restart();
}
