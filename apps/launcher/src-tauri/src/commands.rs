//! The commands the launcher's page calls. They are the launcher's privileged surface (disc,
//! downloads, starting programs): nothing else in the page can do these things, and a
//! future add-on bridge must not forward them.

use base64::Engine;
use serde::Serialize;
use std::path::{Path, PathBuf};
use std::sync::atomic::Ordering;
use tauri::{AppHandle, Emitter, Manager, State};
use tauri_plugin_opener::OpenerExt;
use tauri_plugin_updater::UpdaterExt;

use crate::auto_update::{self, Action, LastCheck};
use crate::content::{self, Identity};
use crate::disc::{self, Pick};
use crate::failure::{code, Failure, Result};
use crate::launch;
use crate::log_line;
use crate::manifest;
use crate::releases::{self, Client, Release, UpdateChannel};
use crate::store::{Game, GameStore};
use crate::system_check::{self, Graphics, GraphicsCheck, Space};
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
    launcher_version: String,
    platform: String,
    data_folder: String,
    own_data_folder: bool,
    check_updates: bool,
    auto_update_launcher: bool,
    auto_update_game: bool,
    /// "stable" or "testing" (releases::UpdateChannel).
    update_channel: UpdateChannel,
    last_check: Option<LastCheck>,
    /// A new launcher installed in this session: it runs after a restart.
    launcher_ready: Option<String>,
    /// This copy can replace itself (auto_update::launcher_replaceable).
    launcher_replaceable: bool,
    game: Option<Game>,
    has_previous: bool,
    content_installed: bool,
    content_outdated: bool,
    content_extractor_version: Option<i32>,
    disc_source: Option<String>,
    disc_source_exists: bool,
    signing_ready: bool,
    busy: bool,
    game_running: bool,
    formats: Formats,
    /// The main screen's links, built in (links.rs); none by default.
    links: Vec<crate::links::Link>,
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
    /// What to do with each (auto_update.rs): the page follows it.
    launcher_plan: Action,
    game_plan: Action,
}

/// The running launcher's version (the configuration's, which a test build may change).
fn launcher_version(app: &AppHandle) -> String {
    app.package_info().version.to_string()
}

/// Keeps the last check's result in settings.json.
fn record(state: &AppState, next: impl FnOnce(&LastCheck) -> LastCheck) {
    let mut settings = state.settings.lock().unwrap();
    let previous = settings.last_check.clone().unwrap_or_default();
    settings.last_check = Some(next(&previous));
    if let Err(failure) = settings.save() {
        log_line!("the last check was not saved: {failure}");
    }
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
    Status {
        language: settings.language.clone(),
        launcher_version: launcher_version(app),
        platform: manifest::platform_key(),
        data_folder: data.display().to_string(),
        own_data_folder: settings.uses_default_data_folder(),
        check_updates: settings.check_updates,
        auto_update_launcher: settings.auto_update_launcher,
        auto_update_game: settings.auto_update_game,
        update_channel: settings.update_channel,
        last_check: settings.last_check.clone(),
        launcher_ready: state.launcher_ready.lock().unwrap().clone(),
        launcher_replaceable: auto_update::launcher_replaceable(),
        has_previous: store.has_previous(),
        content_installed: installed,
        content_outdated: game.as_ref().is_some_and(|g| content::is_outdated(&content, g.extractor_version)),
        content_extractor_version: record.map(|r| r.extractor_version),
        disc_source_exists: settings.disc_source.as_ref().is_some_and(|d| d.is_file()),
        disc_source: settings.disc_source.as_ref().map(|d| d.display().to_string()),
        game,
        signing_ready: public_key(app).is_some(),
        busy: state.busy.load(Ordering::SeqCst),
        game_running: state.game_running.load(Ordering::SeqCst),
        formats: Formats {
            images: &disc::IMAGE_EXTENSIONS,
            archives: &disc::ARCHIVE_EXTENSIONS,
            unsupported: &disc::UNSUPPORTED_EXTENSIONS,
        },
        links: crate::links::built_in(),
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
    Client::new(&settings.api_base, &settings.repository, state.beta_key.get())
}

fn channel(state: &AppState) -> UpdateChannel {
    state.settings.lock().unwrap().update_channel
}

/// The newest release of the player's channel, kept for the update that follows (unless the
/// channel changed meanwhile: set_update_channel drops what was kept).
async fn latest_release(state: &AppState) -> Result<Release> {
    let asked = channel(state);
    let release = client(state)?.latest(asked).await?;
    if channel(state) == asked {
        *state.release.lock().unwrap() = Some(release.clone());
    }
    Ok(release)
}

/// The kept release and whether it was kept (true) or just read (false).
async fn cached_or_latest(state: &AppState) -> Result<(Release, bool)> {
    let cached = state.release.lock().unwrap().clone();
    match cached {
        Some(release) => Ok((release, true)),
        None => latest_release(state).await.map(|release| (release, false)),
    }
}

/// Drops the kept release and reads the listing again, after a download from the kept one
/// failed (releases::worth_reading_again): a file uploaded again since has a new address.
async fn read_releases_again(state: &AppState, failure: &Failure) -> Result<Release> {
    log_line!("the download failed ({}): reading the releases again", failure.code);
    *state.release.lock().unwrap() = None;
    latest_release(state).await
}

/// Looks for a new launcher and a new game, says what to do with each (auto_update.rs) and keeps
/// the result (record: false for the quiet look after an update, which keeps "updated").
#[tauri::command]
pub async fn check_updates(app: AppHandle, state: State<'_, AppState>, record_result: Option<bool>) -> Result<UpdateInfo> {
    let keep = record_result.unwrap_or(true);
    let checked = look_for_updates(&app, &state).await;
    if keep {
        let at = auto_update::now();
        match &checked {
            Ok(info) => {
                let game = info.game_update.then(|| info.game_version.clone());
                record(&state, |_| LastCheck::checked(at, info.launcher_update.clone(), game));
            }
            Err(failure) => record(&state, |_| LastCheck::failed(at, failure)),
        }
    }
    checked
}

async fn look_for_updates(app: &AppHandle, state: &AppState) -> Result<UpdateInfo> {
    let release = latest_release(state).await?;
    let data = data_folder(state);
    let game = GameStore::new(&data).current();
    let current = launcher_version(app);
    let mut launcher_update = None;
    if let Some(asset) = release.launcher_manifest() {
        if let Ok(text) = client(state)?.fetch_text(asset).await {
            launcher_update = serde_json::from_str::<serde_json::Value>(&text)
                .ok()
                .and_then(|v| v.get("version")?.as_str().map(str::to_string))
                .filter(|v| manifest::is_newer(v, Some(&current)));
        }
    }
    let game_update = updates::is_update(&release, game.as_ref());
    log_line!(
        "release {} ({} channel): game {} (update: {game_update}), launcher {launcher_update:?}",
        release.tag,
        channel(state),
        release.manifest.version
    );
    let settings = state.settings.lock().unwrap().clone();
    let signing = public_key(app).is_some();
    let content = content::content_folder(&data);
    let (running, busy) = (state.game_running.load(Ordering::SeqCst), state.busy.load(Ordering::SeqCst));
    let ready = state.launcher_ready.lock().unwrap().clone();
    let launcher_plan = auto_update::plan(auto_update::Facts {
        found: launcher_update.is_some(),
        automatic: settings.auto_update_launcher,
        can_install: signing && auto_update::launcher_replaceable(),
        game_running: running,
        busy,
        installed: ready.is_some() && ready == launcher_update,
    });
    let game_plan = auto_update::plan(auto_update::Facts {
        found: game_update,
        automatic: settings.auto_update_game,
        // The first install goes through the disc screen; an outdated content is read first.
        can_install: signing && game.is_some() && content::has_base_pack(&content) && !game.as_ref().is_some_and(|g| content::is_outdated(&content, g.extractor_version)),
        game_running: running,
        busy,
        installed: false,
    });
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
        launcher_plan,
        game_plan,
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

/// The game that reads the disc: the newest release's, downloaded first when the installed one is
/// older (its dwx may carry a fix for reading discs), or the installed one when the newest cannot
/// be reached.
async fn ensure_game(app: &AppHandle, state: &AppState) -> Result<Game> {
    if let Some(game) = GameStore::new(&data_folder(state)).current() {
        return match cached_or_latest(state).await {
            Ok((release, _)) if updates::is_update(&release, Some(&game)) => {
                log_line!("the game {} is outdated: installing {} before reading the disc", game.version, release.manifest.version);
                download_game(app, state, &release).await
            }
            _ => Ok(game),
        };
    }
    let (release, _) = cached_or_latest(state)
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

/// Refuses to start what needs `needed` bytes when the data folder's disk has less.
fn require_space(state: &AppState, needed: u64) -> Result<()> {
    let data = data_folder(state);
    let free = system_check::free_bytes(&data);
    if system_check::enough(free, needed) {
        return Ok(());
    }
    log_line!("not enough space in {}: {free:?} free, {needed} needed", data.display());
    Err(Failure::new(code::NO_SPACE, data.display().to_string()))
}

#[tauri::command]
pub async fn install_disc(app: AppHandle, state: State<'_, AppState>, path: String) -> Result<Status> {
    let _busy = state.begin()?;
    log_line!("the player picked {path}");
    require_space(&state, system_check::INSTALL_BYTES)?;
    install_from(&app, &state, Path::new(&path)).await?;
    Ok(status_of(&app, &state))
}

/// Reads the disc again: from the given file, or from the one the content was read from.
#[tauri::command]
pub async fn reextract(app: AppHandle, state: State<'_, AppState>, path: Option<String>) -> Result<Status> {
    let _busy = state.begin()?;
    require_space(&state, system_check::INSTALL_BYTES)?;
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
    if state.game_running.load(Ordering::SeqCst) {
        return Err(Failure::new(code::GAME_RUNNING, ""));
    }
    let _busy = state.begin()?;
    let (release, cached) = cached_or_latest(&state).await?;
    // Never back to an older game: after a switch to Stable, the newer test version stays.
    let current = GameStore::new(&data_folder(&state)).current();
    if !updates::is_update(&release, current.as_ref()) {
        return Err(Failure::new(code::UP_TO_DATE, release.manifest.version));
    }
    let installed = match download_game(&app, &state, &release).await {
        Err(failure) if cached && releases::worth_reading_again(&failure) => {
            let fresh = read_releases_again(&state, &failure).await?;
            if !updates::is_update(&fresh, current.as_ref()) {
                return Err(Failure::new(code::UP_TO_DATE, fresh.manifest.version));
            }
            download_game(&app, &state, &fresh).await
        }
        other => other,
    };
    let at = auto_update::now();
    match &installed {
        Ok(game) => record(&state, |last| last.updated(at, None, Some(game.version.clone()))),
        Err(failure) if failure.code != code::CANCELLED => record(&state, |_| LastCheck::failed(at, failure)),
        Err(_) => {}
    }
    let game = installed?;
    let content = content::content_folder(&data_folder(&state));
    if content::is_outdated(&content, game.extractor_version) {
        log_line!("the new game reads discs differently: reading the disc again");
        let source = remembered_disc(&state)?;
        install_from(&app, &state, &source).await?;
    }
    Ok(status_of(&app, &state))
}

/// Goes back to the previous game; like an update, reads the disc again when that game reads
/// discs differently.
#[tauri::command]
pub async fn rollback_game(app: AppHandle, state: State<'_, AppState>) -> Result<Status> {
    let _busy = state.begin()?;
    let data = data_folder(&state);
    let game = GameStore::new(&data).rollback()?;
    log_line!("back to the game {}", game.version);
    if content::is_outdated(&content::content_folder(&data), game.extractor_version) {
        log_line!("the previous game reads discs differently: reading the disc again");
        let source = remembered_disc(&state)?;
        install_from(&app, &state, &source).await?;
    }
    Ok(status_of(&app, &state))
}

#[tauri::command]
pub fn cancel(state: State<'_, AppState>) {
    state.cancel.store(true, Ordering::SeqCst);
}

/// The GPU check of a game: the one kept for its folder, or a new one (also when `again`). Runs
/// off the page's thread; a second caller waits for the check in progress.
async fn graphics_of(state: &AppState, game: &Game, again: bool) -> GraphicsCheck {
    let mut kept = state.graphics.lock().await;
    if let Some((dir, check)) = kept.as_ref() {
        if !again && *dir == game.dir {
            return check.clone();
        }
    }
    let probed = game.clone();
    let check = tauri::async_runtime::spawn_blocking(move || system_check::check_graphics(&probed))
        .await
        .unwrap_or_else(|_| GraphicsCheck::unknown());
    *kept = Some((game.dir.clone(), check.clone()));
    check
}

#[derive(Serialize)]
pub struct Requirements {
    /// linux, windows or macos: what the page can offer (Windows Update only on Windows).
    os: &'static str,
    space: Space,
    /// None until a game is installed (the check runs the game's own probe).
    graphics: Option<GraphicsCheck>,
}

/// What this computer has for installing and playing: free space in the data folder and the GPU
/// check of the installed game (kept; `again` checks once more, after the player fixed it).
#[tauri::command]
pub async fn check_system(state: State<'_, AppState>, again: Option<bool>) -> Result<Requirements> {
    let data = data_folder(&state);
    let space = system_check::space(&data, system_check::free_bytes(&data));
    let graphics = match GameStore::new(&data).current() {
        Some(game) => Some(graphics_of(&state, &game, again.unwrap_or(false)).await),
        None => None,
    };
    Ok(Requirements { os: std::env::consts::OS, space, graphics })
}

/// Opens the driver page of a GPU maker (nvidia, amd, intel) or, on Windows, Windows Update
/// (windows-update): only the addresses system_check.rs knows.
#[tauri::command]
pub fn open_driver_page(app: AppHandle, page: String) -> Result<()> {
    let url = match page.as_str() {
        "nvidia" => system_check::driver_page(system_check::Vendor::Nvidia),
        "amd" => system_check::driver_page(system_check::Vendor::Amd),
        "intel" => system_check::driver_page(system_check::Vendor::Intel),
        "windows-update" if cfg!(windows) => Some(system_check::WINDOWS_UPDATE),
        _ => None,
    }
    .ok_or_else(|| Failure::new(code::IO, page.clone()))?;
    app.opener().open_url(url, None::<&str>).map_err(|e| Failure::new(code::IO, e.to_string()))
}

/// Starts the game, hides the launcher while it runs and shows it again when it ends, with the
/// end of the game's log when it failed. A computer whose GPUs cannot run the game starts it on
/// the package's software driver; one where nothing can is refused (the page says what to do).
#[tauri::command]
pub async fn play(app: AppHandle, state: State<'_, AppState>) -> Result<()> {
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
    require_space(&state, system_check::PLAY_BYTES)?;
    let graphics = graphics_of(&state, &game, false).await;
    if graphics.graphics == Graphics::Blocked {
        return Err(Failure::new(code::NO_GPU, graphics.detail));
    }
    if state.game_running.swap(true, Ordering::SeqCst) {
        return Ok(());
    }
    let mut child = match launch::start(&game, &data, own, &graphics.env) {
        Ok(child) => child,
        Err(failure) => {
            state.game_running.store(false, Ordering::SeqCst);
            return Err(failure);
        }
    };
    if graphics.graphics == Graphics::Fallback {
        log_line!("no GPU can run the game: starting it on the software driver");
    }
    log_line!("the game {} started", game.version);
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

/// The automatic updates: what ("launcher" or "game") and on or off.
#[tauri::command]
pub fn set_auto_update(app: AppHandle, state: State<'_, AppState>, what: String, enabled: bool) -> Result<Status> {
    {
        let mut settings = state.settings.lock().unwrap();
        match what.as_str() {
            "launcher" => settings.auto_update_launcher = enabled,
            "game" => settings.auto_update_game = enabled,
            _ => return Err(Failure::new(code::IO, what)),
        }
        settings.save()?;
    }
    Ok(status_of(&app, &state))
}

/// The releases the updates come from: "stable" or "testing". The release kept from the last
/// check belongs to the other channel, so it is dropped; the page looks for updates again.
#[tauri::command]
pub fn set_update_channel(app: AppHandle, state: State<'_, AppState>, channel: String) -> Result<Status> {
    let chosen = UpdateChannel::parse(&channel).ok_or_else(|| Failure::new(code::IO, channel))?;
    {
        let mut settings = state.settings.lock().unwrap();
        settings.update_channel = chosen;
        settings.save()?;
    }
    *state.release.lock().unwrap() = None;
    log_line!("update channel: {chosen}");
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
pub fn open_logs(app: AppHandle, state: State<'_, AppState>) -> Result<()> {
    let folder = crate::log::logs_folder(&data_folder(&state));
    std::fs::create_dir_all(&folder)?;
    app.opener()
        .open_path(folder.display().to_string(), None::<&str>)
        .map_err(|e| Failure::new(code::IO, e.to_string()))
}

#[derive(Serialize)]
pub struct LauncherInstalled {
    version: String,
}

/// Downloads the release's launcher (the Tauri updater: latest.json, signed with the same key),
/// checks its signature and installs it; it runs from the next start (restart_launcher, or the
/// next time the player opens it). Never while the game runs. On Windows the installer runs
/// when the launcher restarts or closes, since it closes the launcher.
#[tauri::command]
pub async fn install_launcher_update(app: AppHandle, state: State<'_, AppState>) -> Result<LauncherInstalled> {
    if state.game_running.load(Ordering::SeqCst) {
        return Err(Failure::new(code::GAME_RUNNING, ""));
    }
    let _busy = state.begin()?;
    let installed = download_launcher(&app, &state).await;
    let at = auto_update::now();
    match &installed {
        Ok(version) => {
            *state.launcher_ready.lock().unwrap() = Some(version.clone());
            record(&state, |last| last.updated(at, Some(version.clone()), None));
        }
        Err(failure) => record(&state, |_| LastCheck::failed(at, failure)),
    }
    installed.map(|version| LauncherInstalled { version })
}

async fn download_launcher(app: &AppHandle, state: &AppState) -> Result<String> {
    let key = public_key_text(app)
        .filter(|k| verify::public_key(k).is_some())
        .ok_or_else(|| Failure::new(code::NO_SIGNING_KEY, ""))?;
    let (release, cached) = cached_or_latest(state).await?;
    let update = match find_launcher(app, state, &release, &key).await {
        Err(failure) if cached && releases::worth_reading_again(&failure) => {
            let fresh = read_releases_again(state, &failure).await?;
            find_launcher(app, state, &fresh, &key).await?
        }
        other => other?,
    };
    log_line!("downloading the launcher {}", update.version);
    let handle = app.clone();
    let mut done = 0u64;
    // The download checks the signature against the build's key before returning the bytes.
    let bytes = update
        .download(
            move |chunk, total| {
                done += chunk as u64;
                emit(&handle, "launcher", None, None, done, total.unwrap_or(0));
            },
            || {},
        )
        .await
        .map_err(|e| match e {
            tauri_plugin_updater::Error::Minisign(_)
            | tauri_plugin_updater::Error::SignatureUtf8(_)
            | tauri_plugin_updater::Error::Base64(_)
            | tauri_plugin_updater::Error::SignedVersionMismatch { .. }
            | tauri_plugin_updater::Error::MissingSignedVersion => Failure::new(code::BAD_SIGNATURE, e.to_string()),
            other => updater_failure(other),
        })?;
    let version = update.version.clone();
    if cfg!(windows) {
        *state.launcher_pending.lock().unwrap() = Some((update, bytes));
    } else {
        update.install(&bytes).map_err(updater_failure)?;
    }
    log_line!("the launcher {version} is installed; it runs from the next start");
    Ok(version)
}

fn updater_failure(e: tauri_plugin_updater::Error) -> Failure {
    Failure::new(code::NETWORK, crate::log::redact(&e.to_string()))
}

/// The release's latest.json through the Tauri updater: the launcher it names, when newer.
async fn find_launcher(app: &AppHandle, state: &AppState, release: &Release, key: &str) -> Result<tauri_plugin_updater::Update> {
    let asset = release.launcher_manifest().ok_or_else(|| Failure::new(code::NO_RELEASE, "latest.json"))?;
    let url = asset.url.parse().map_err(|_| Failure::new(code::BAD_MANIFEST, asset.url.clone()))?;
    let mut builder = app.updater_builder().pubkey(key).endpoints(vec![url]).map_err(updater_failure)?;
    builder = builder.header("Accept", "application/octet-stream").map_err(updater_failure)?;
    for (name, value) in client(state)?.auth_headers() {
        builder = builder.header(name, value).map_err(updater_failure)?;
    }
    let update = builder.build().map_err(updater_failure)?.check().await.map_err(updater_failure)?;
    let Some(update) = update else {
        return Err(Failure::new(code::NO_RELEASE, "latest.json"));
    };
    // The updater sends the key with the download too: only to the release server (the gateway
    // points every file of latest.json at itself).
    if !client(state)?.serves(update.download_url.as_str()) {
        return Err(Failure::new(code::BAD_MANIFEST, format!("{} is not on the release server", update.download_url)));
    }
    Ok(update)
}

/// Starts the new launcher installed by install_launcher_update. Refused while the game runs.
#[tauri::command]
pub fn restart_launcher(app: AppHandle, state: State<'_, AppState>) -> Result<()> {
    if state.game_running.load(Ordering::SeqCst) {
        return Err(Failure::new(code::GAME_RUNNING, ""));
    }
    if state.busy.load(Ordering::SeqCst) {
        return Err(Failure::new(code::BUSY, ""));
    }
    if let Some(marker) = &state.run_marker {
        marker.clear();
    }
    crate::install_pending_launcher(&state);
    app.restart();
}
