//! The Images section's commands (custom_images.rs): the export of the originals with dwx, bulk
//! and one-by-one loads into the player's pack, thumbnails, on and off.

use serde::Serialize;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use tauri::State;

use crate::content;
use crate::custom_images::picked::Picked;
use crate::custom_images::{BulkSummary, Catalogue, CustomImages, Overview, Problem};
use crate::failure::{code, Failure, Result};
use crate::log_line;
use crate::store::{Game, GameStore, MAC_APP};
use crate::system_check;
use crate::AppState;

/// What the export of the originals needs on disk, with room to spare.
const EXPORT_BYTES: u64 = 400 * 1024 * 1024;
/// Where a package keeps the team's packs: next to bin/, or in a macOS bundle's resources.
const TEAM_PACKS: &str = "packs";

fn data_folder(state: &AppState) -> PathBuf {
    state.settings.lock().unwrap().data_folder()
}

fn images(state: &AppState) -> CustomImages {
    CustomImages::new(&data_folder(state))
}

/// The game and its content, which the export reads.
fn installed(state: &AppState) -> Result<(Game, PathBuf)> {
    let data = data_folder(state);
    let game = GameStore::new(&data).current().ok_or_else(|| Failure::new(code::IMAGES_NO_CONTENT, ""))?;
    let content = content::content_folder(&data);
    if !content::has_base_pack(&content) {
        return Err(Failure::new(code::IMAGES_NO_CONTENT, content.display().to_string()));
    }
    Ok((game, content))
}

/// What the export was made from: a new game or a disc read again makes it again.
fn stamp(game: &Game, content: &Path) -> String {
    let record = fs::read_to_string(content.join(content::INSTALL_RECORD)).unwrap_or_default();
    format!("{}\n{record}", game.version)
}

fn team_packs(game: &Game) -> Option<PathBuf> {
    [game.dir.join(TEAM_PACKS), game.dir.join(MAC_APP).join("Contents").join("Resources").join(TEAM_PACKS)]
        .into_iter()
        .find(|folder| folder.is_dir())
}

fn catalogue(state: &AppState) -> Result<(CustomImages, Catalogue)> {
    let (game, content) = installed(state)?;
    let images = images(state);
    if !images.prepared(&stamp(&game, &content)) {
        return Err(Failure::new(code::IMAGES_NOT_READY, ""));
    }
    let catalogue = images.catalogue()?;
    Ok((images, catalogue))
}

async fn blocking<T: Send + 'static>(work: impl FnOnce() -> Result<T> + Send + 'static) -> Result<T> {
    tauri::async_runtime::spawn_blocking(work).await.map_err(|e| Failure::new(code::IO, e.to_string()))?
}

/// The section: whether the originals are exported, on or off, and every image by group.
#[tauri::command]
pub async fn images_overview(state: State<'_, AppState>) -> Result<Overview> {
    let (game, content) = installed(&state)?;
    let images = images(&state);
    let prepared = images.prepared(&stamp(&game, &content));
    blocking(move || {
        let catalogue = if prepared { images.catalogue()? } else { Catalogue::empty() };
        Ok(images.overview(&catalogue, prepared))
    })
    .await
}

/// Exports the originals from the installed content with dwx export-images, in the launcher's
/// language, so the list can show them and the player can export them.
#[tauri::command]
pub async fn images_prepare(state: State<'_, AppState>) -> Result<Overview> {
    let _busy = state.begin()?;
    let (game, content) = installed(&state)?;
    let data = data_folder(&state);
    if !system_check::enough(system_check::free_bytes(&data), EXPORT_BYTES) {
        return Err(Failure::new(code::NO_SPACE, data.display().to_string()));
    }
    let images = images(&state);
    let mut command = Command::new(game.dwx());
    command.arg("export-images").arg(&content);
    if let Some(packs) = team_packs(&game) {
        command.arg("--with").arg(packs);
    }
    let language = state.settings.lock().unwrap().language.clone();
    command.arg("--language").arg(language).arg("-o").arg(images.originals());
    log_line!("exporting the game's images into {}", images.originals().display());
    let cancel = state.cancel.clone();
    let stamp = stamp(&game, &content);
    blocking(move || {
        let (exit, lines) = content::run_lines(command, &cancel, |_| {})?;
        if exit != Some(0) {
            return Err(Failure::new(code::IMAGES_EXPORT_FAILED, content::dwx_error(&lines)));
        }
        images.mark_prepared(&stamp)?;
        let catalogue = images.catalogue()?;
        Ok(images.overview(&catalogue, true))
    })
    .await
}

/// What a folder or .zip would load, without changing anything.
#[tauri::command]
pub async fn images_inspect(state: State<'_, AppState>, path: String) -> Result<BulkSummary> {
    let (images, catalogue) = catalogue(&state)?;
    blocking(move || images.inspect(&catalogue, &mut Picked::open(Path::new(&path))?)).await
}

/// Loads the images of a folder or .zip that match and pass the checks.
#[tauri::command]
pub async fn images_apply(state: State<'_, AppState>, path: String) -> Result<BulkSummary> {
    let (images, catalogue) = catalogue(&state)?;
    log_line!("loading the player's images from {path}");
    blocking(move || images.apply(&catalogue, &mut Picked::open(Path::new(&path))?)).await
}

#[derive(Serialize)]
pub struct Assigned {
    scale: Option<u32>,
    problem: Option<Problem>,
}

/// Sets one image from one PNG.
#[tauri::command]
pub async fn images_assign(state: State<'_, AppState>, target: String, path: String) -> Result<Assigned> {
    let (images, catalogue) = catalogue(&state)?;
    let checked = blocking(move || images.assign(&catalogue, &target, Path::new(&path))).await?;
    Ok(match checked {
        Ok(scale) => Assigned { scale: Some(scale), problem: None },
        Err(problem) => Assigned { scale: None, problem: Some(problem) },
    })
}

/// Puts one image back to the original.
#[tauri::command]
pub fn images_reset(state: State<'_, AppState>, target: String) -> Result<()> {
    let (images, catalogue) = catalogue(&state)?;
    images.reset(&catalogue, &target)
}

/// An image as a data: URL, the original or the player's.
#[tauri::command]
pub async fn images_thumbnail(state: State<'_, AppState>, target: String, custom: bool) -> Result<String> {
    let (images, catalogue) = catalogue(&state)?;
    blocking(move || images.thumbnail(&catalogue, &target, custom)).await
}

#[tauri::command]
pub fn images_set_enabled(state: State<'_, AppState>, enabled: bool) -> Result<()> {
    images(&state).set_enabled(enabled)
}

/// Removes every image the player loaded.
#[tauri::command]
pub fn images_remove_all(state: State<'_, AppState>) -> Result<()> {
    log_line!("removing the player's images");
    images(&state).remove_all()
}

/// Copies the originals, with a guide, into a new folder inside the one the player picked.
#[tauri::command]
pub async fn images_export(state: State<'_, AppState>, folder: String) -> Result<String> {
    let (images, catalogue) = catalogue(&state)?;
    let written = blocking(move || images.export(&catalogue, Path::new(&folder))).await?;
    log_line!("the game's images were exported to {}", written.display());
    Ok(written.display().to_string())
}
