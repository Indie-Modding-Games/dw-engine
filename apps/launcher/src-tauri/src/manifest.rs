//! The game package manifest (game-manifest.json), a file of every release next to the packages it
//! describes: the version, the extractor version of its dwx and, for each computer, the package's
//! file name, size, SHA-256 and signature. scripts/package_release.sh writes it.

use semver::Version;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

use crate::failure::{code, Failure, Result};

/// The release file that holds the manifest.
pub const MANIFEST_ASSET: &str = "game-manifest.json";
/// The release file the launcher's own updater reads.
pub const LAUNCHER_MANIFEST_ASSET: &str = "latest.json";
pub const SCHEMA: u32 = 1;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct GameManifest {
    pub schema: u32,
    /// Semantic version, such as 0.1.0-beta.1530; a higher one is an update.
    pub version: String,
    #[serde(default)]
    pub commit: String,
    /// The ExtractorVersion of the package's dwx: when it changes, the player's content is read
    /// from the disc again.
    pub extractor_version: i32,
    #[serde(default)]
    pub date: Option<String>,
    pub packages: BTreeMap<String, Package>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Package {
    /// The release file's name.
    pub asset: String,
    pub size: u64,
    /// Lowercase hexadecimal SHA-256 of the file.
    pub sha256: String,
    /// The minisign signature of the file, base64 of the .sig file (as the updater's latest.json).
    pub signature: String,
}

/// This computer's package key: <os>-<architecture>, such as linux-x86_64.
pub fn platform_key() -> String {
    platform_key_for(std::env::consts::OS, std::env::consts::ARCH)
}

/// The package key of a computer, with the names of the Tauri updater's latest.json: linux-x86_64,
/// windows-x86_64, darwin-aarch64 (Apple silicon) and darwin-x86_64 (Intel Macs).
pub fn platform_key_for(os: &str, arch: &str) -> String {
    let os = if os == "macos" { "darwin" } else { os };
    format!("{os}-{arch}")
}

pub fn parse(text: &str) -> Result<GameManifest> {
    let manifest: GameManifest =
        serde_json::from_str(text).map_err(|e| Failure::new(code::BAD_MANIFEST, e.to_string()))?;
    if manifest.schema != SCHEMA {
        return Err(Failure::new(code::BAD_MANIFEST, format!("schema {}", manifest.schema)));
    }
    Version::parse(&manifest.version)
        .map_err(|e| Failure::new(code::BAD_MANIFEST, format!("version {}: {e}", manifest.version)))?;
    for (key, package) in &manifest.packages {
        let hex = package.sha256.len() == 64 && package.sha256.bytes().all(|b| b.is_ascii_hexdigit());
        if !hex || package.asset.is_empty() || package.asset.contains('/') {
            return Err(Failure::new(code::BAD_MANIFEST, format!("package {key}")));
        }
    }
    Ok(manifest)
}

impl GameManifest {
    pub fn package(&self, platform: &str) -> Option<&Package> {
        self.packages.get(platform)
    }
}

/// Whether the release's version is newer than the installed one; anything is newer than nothing
/// or than a version that does not parse (a development build).
pub fn is_newer(remote: &str, installed: Option<&str>) -> bool {
    let Ok(remote) = Version::parse(remote) else {
        return false;
    };
    match installed.map(Version::parse) {
        Some(Ok(installed)) => remote > installed,
        _ => true,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE: &str = r#"{
      "schema": 1,
      "version": "0.1.0-beta.12",
      "commit": "abc1234",
      "extractor_version": 12,
      "packages": {
        "linux-x86_64": {
          "asset": "dwe-game-0.1.0-beta.12-linux-x86_64.tar.xz",
          "size": 1024,
          "sha256": "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08",
          "signature": "c2ln"
        }
      }
    }"#;

    #[test]
    fn a_manifest_is_read() {
        let manifest = parse(SAMPLE).unwrap();
        assert_eq!(manifest.extractor_version, 12);
        let package = manifest.package("linux-x86_64").unwrap();
        assert_eq!(package.size, 1024);
        assert!(manifest.package("windows-x86_64").is_none());
    }

    #[test]
    fn bad_manifests_are_refused() {
        assert_eq!(parse("{").unwrap_err().code, code::BAD_MANIFEST);
        assert!(parse(&SAMPLE.replace("\"schema\": 1", "\"schema\": 2")).is_err());
        assert!(parse(&SAMPLE.replace("0.1.0-beta.12", "latest")).is_err());
        assert!(parse(&SAMPLE.replace("9f86d0", "zz86d0")).is_err());
        assert!(parse(&SAMPLE.replace("dwe-game-0.1.0", "../dwe-game-0.1.0")).is_err());
    }

    #[test]
    fn versions_compare_as_semver() {
        assert!(is_newer("0.1.0-beta.13", Some("0.1.0-beta.12")));
        assert!(is_newer("0.1.0-beta.100", Some("0.1.0-beta.99")));
        assert!(is_newer("0.1.0", Some("0.1.0-beta.99")));
        assert!(!is_newer("0.1.0-beta.12", Some("0.1.0-beta.12")));
        assert!(!is_newer("0.1.0-beta.11", Some("0.1.0-beta.12")));
        assert!(is_newer("0.1.0-beta.1", None));
        assert!(is_newer("0.1.0-beta.1", Some("dev")));
        assert!(!is_newer("nonsense", None));
    }

    #[test]
    fn each_computer_picks_its_own_package() {
        let mut manifest = parse(SAMPLE).unwrap();
        let linux = manifest.packages["linux-x86_64"].clone();
        for key in ["windows-x86_64", "darwin-aarch64", "darwin-x86_64"] {
            manifest.packages.insert(key.into(), Package { asset: format!("dwe-game-{key}.tar.xz"), ..linux.clone() });
        }
        let picked = |os: &str, arch: &str| manifest.package(&platform_key_for(os, arch)).map(|p| p.asset.clone());
        assert_eq!(picked("linux", "x86_64").unwrap(), linux.asset);
        assert_eq!(picked("windows", "x86_64").unwrap(), "dwe-game-windows-x86_64.tar.xz");
        assert_eq!(picked("macos", "aarch64").unwrap(), "dwe-game-darwin-aarch64.tar.xz");
        assert_eq!(picked("macos", "x86_64").unwrap(), "dwe-game-darwin-x86_64.tar.xz");
        assert!(picked("linux", "aarch64").is_none());
        assert!(picked("windows", "aarch64").is_none());
    }

    #[test]
    fn the_platform_key_names_os_and_architecture() {
        let key = platform_key();
        assert!(key.contains('-'));
        if cfg!(all(target_os = "linux", target_arch = "x86_64")) {
            assert_eq!(key, "linux-x86_64");
        }
        if cfg!(all(target_os = "windows", target_arch = "x86_64")) {
            assert_eq!(key, "windows-x86_64");
        }
        if cfg!(target_os = "macos") {
            assert!(key.starts_with("darwin-"));
        }
    }
}
