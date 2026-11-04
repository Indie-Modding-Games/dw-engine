//! Installing the game from a release: the package for this computer is downloaded, checked
//! (size, SHA-256, signature) and only then unpacked and made current (store.rs).

use minisign_verify::PublicKey;
use std::path::Path;
use std::sync::atomic::AtomicBool;

use crate::failure::{code, Failure, Result};
use crate::manifest::{self, platform_key};
use crate::releases::{Client, Release};
use crate::store::{Game, GameStore, InstalledVersion};
use crate::verify;
use crate::log_line;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Stage {
    Download,
    Verify,
    Install,
}

/// The package size of the release for this computer, when it has one.
pub fn download_size(release: &Release) -> Option<u64> {
    release.manifest.package(&platform_key()).map(|p| p.size)
}

/// Whether the release is newer than the installed game.
pub fn is_update(release: &Release, installed: Option<&Game>) -> bool {
    manifest::is_newer(&release.manifest.version, installed.map(|g| g.version.as_str()))
}

pub async fn install_game(
    client: &Client,
    release: &Release,
    data: &Path,
    key: Option<&PublicKey>,
    cancel: &AtomicBool,
    mut progress: impl FnMut(Stage, u64, u64),
) -> Result<Game> {
    let key = key.ok_or_else(|| Failure::new(code::NO_SIGNING_KEY, ""))?;
    let platform = platform_key();
    let package = release
        .manifest
        .package(&platform)
        .ok_or_else(|| Failure::new(code::NO_RELEASE, platform.clone()))?;
    let asset = release
        .asset(&package.asset)
        .ok_or_else(|| Failure::new(code::BAD_MANIFEST, format!("{} is not in the release", package.asset)))?;
    let downloads = data.join("downloads");
    let file = downloads.join(&package.asset);
    log_line!("downloading {} ({} bytes)", package.asset, package.size);
    client.download(asset, &file, cancel, |done, total| progress(Stage::Download, done, total)).await?;
    let checked = (|| {
        progress(Stage::Verify, 0, 1);
        let size = std::fs::metadata(&file)?.len();
        if size != package.size {
            return Err(Failure::new(code::BAD_CHECKSUM, format!("{} bytes instead of {}", size, package.size)));
        }
        verify::check_sha256(&file, &package.sha256)?;
        verify::check_signature(&file, &package.signature, key)?;
        progress(Stage::Install, 0, 1);
        GameStore::new(data).install(
            &file,
            InstalledVersion {
                version: release.manifest.version.clone(),
                commit: release.manifest.commit.clone(),
                extractor_version: Some(release.manifest.extractor_version),
            },
        )
    })();
    let _ = std::fs::remove_file(&file);
    match &checked {
        Ok(game) => log_line!("the game {} is installed", game.version),
        Err(failure) => log_line!("the game was not installed: {failure}"),
    }
    checked
}
