//! What went wrong, as the page shows it: a stable code the page turns into a sentence in the
//! player's language, and a detail (a path, a tool's own words) shown under it.

use serde::Serialize;

#[derive(Debug, Clone, Serialize, PartialEq, Eq, thiserror::Error)]
#[error("{code}: {detail}")]
pub struct Failure {
    pub code: &'static str,
    pub detail: String,
}

impl Failure {
    pub fn new(code: &'static str, detail: impl Into<String>) -> Self {
        Self { code, detail: detail.into() }
    }
}

/// The codes the page knows (ui/i18n.js has a sentence for each).
pub mod code {
    /// The picked file, or a file its CUE sheet names, is not there.
    pub const MISSING_FILE: &str = "missing-file";
    /// A disc format dwx does not read (ECM, PBP, RAR, CHD...).
    pub const UNSUPPORTED_FORMAT: &str = "unsupported-format";
    /// An archive that holds no disc image dwx reads.
    pub const NO_IMAGE_IN_ARCHIVE: &str = "no-image-in-archive";
    /// dwx cannot read it as a disc image.
    pub const CANNOT_READ: &str = "cannot-read";
    /// A disc image, but not of the game.
    pub const NOT_THE_GAME: &str = "not-the-game";
    /// The game, but another region's release.
    pub const OTHER_REGION: &str = "other-region";
    /// dwx extract failed, or its packs could not be moved into place.
    pub const EXTRACT_FAILED: &str = "extract-failed";
    /// No game is installed yet and none could be downloaded.
    pub const NO_GAME: &str = "no-game";
    /// The release server could not be reached.
    pub const NETWORK: &str = "network";
    /// The release server refused us: the beta gateway needs the beta key (missing or wrong).
    pub const AUTH: &str = "auth";
    /// No release has a game package for this computer.
    pub const NO_RELEASE: &str = "no-release";
    /// A release file is not what its manifest says.
    pub const BAD_CHECKSUM: &str = "bad-checksum";
    /// A release file is not signed by the team's key.
    pub const BAD_SIGNATURE: &str = "bad-signature";
    /// This build has no public key to check updates with.
    pub const NO_SIGNING_KEY: &str = "no-signing-key";
    /// A manifest that cannot be read.
    pub const BAD_MANIFEST: &str = "bad-manifest";
    /// A file or folder could not be read or written.
    pub const IO: &str = "io";
    /// The system keyring cannot be used.
    pub const KEYRING: &str = "keyring";
    /// Another task is running.
    pub const BUSY: &str = "busy";
    /// Refused while the game runs (updates and restarts never interrupt it).
    pub const GAME_RUNNING: &str = "game-running";
    /// The player stopped the task.
    pub const CANCELLED: &str = "cancelled";
    /// The disc the content was read from is no longer where it was.
    pub const DISC_MOVED: &str = "disc-moved";
    /// The game did not start, or stopped with an error.
    pub const GAME_FAILED: &str = "game-failed";
    /// The release of the player's channel is not newer than the installed game (never a downgrade).
    pub const UP_TO_DATE: &str = "up-to-date";
    /// There is no previous version to go back to.
    pub const NO_PREVIOUS: &str = "no-previous";
    /// An add-on whose addon.json is wrong; the detail is "<field>:<reason>" (addons/manifest.rs).
    pub const ADDON_INVALID: &str = "addon-invalid";
    /// An add-on written for an API version this launcher does not have; the detail is that version.
    pub const ADDON_API: &str = "addon-api";
    /// An add-on package with a path that leaves its folder, a link or a file type it may not have.
    pub const ADDON_UNSAFE: &str = "addon-unsafe";
    /// An add-on package bigger than the launcher accepts.
    pub const ADDON_TOO_BIG: &str = "addon-too-big";
    /// No add-on with that id is installed (or it is off).
    pub const ADDON_NOT_FOUND: &str = "addon-not-found";
    /// An add-on asked for something it may not do: an unknown call or one it has no permission for.
    pub const ADDON_DENIED: &str = "addon-denied";
    /// An add-on's own storage is full.
    pub const ADDON_STORAGE_FULL: &str = "addon-storage-full";
    /// An add-on asked for an address whose host it did not declare.
    pub const ADDON_HOST: &str = "addon-host";
    /// Not enough free space in the data folder; the detail is the folder (system_check.rs).
    pub const NO_SPACE: &str = "no-space";
    /// No GPU of this computer can run the game, not even with the software driver.
    pub const NO_GPU: &str = "no-gpu";
}

impl From<std::io::Error> for Failure {
    fn from(error: std::io::Error) -> Self {
        Failure::new(code::IO, error.to_string())
    }
}

pub type Result<T> = std::result::Result<T, Failure>;
