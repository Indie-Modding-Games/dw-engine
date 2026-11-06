use std::collections::BTreeMap;
use std::fmt;
use std::path::Path;

use serde::Deserialize;

use crate::{Error, Result};

/// The content layer of a pack, from the bottom of the stack up.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Layer {
    Base = 1,
    Remaster = 2,
    Mods = 3,
    User = 4,
}

impl Layer {
    pub fn name(self) -> &'static str {
        match self {
            Self::Base => "base",
            Self::Remaster => "remaster",
            Self::Mods => "mods",
            Self::User => "user",
        }
    }

    /// Whether only the engine's own packs may declare it: a mod declares mods or user.
    pub fn is_engine_layer(self) -> bool {
        matches!(self, Self::Base | Self::Remaster)
    }
}

/// A semantic version without suffixes, `MAJOR.MINOR.PATCH`.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Version {
    pub major: u32,
    pub minor: u32,
    pub patch: u32,
}

impl Version {
    pub fn parse(text: &str) -> Result<Self> {
        let bad = || Error::invalid(format!("version '{text}' is not MAJOR.MINOR.PATCH"));
        let mut parts = text.splitn(3, '.');
        let mut number = || -> Result<u32> {
            let part = parts.next().ok_or_else(bad)?;
            if part.is_empty() || !part.chars().all(|c| c.is_ascii_digit()) {
                return Err(bad());
            }
            part.parse().map_err(|_| bad())
        };
        Ok(Self {
            major: number()?,
            minor: number()?,
            patch: number()?,
        })
    }
}

impl fmt::Display for Version {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}.{}.{}", self.major, self.minor, self.patch)
    }
}

/// A pack another pack needs, at least at a version.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Dependency {
    pub id: String,
    pub min_version: Version,
}

/// A pack's `pack.yaml` (content/schemas/pack.schema.json). Unknown keys are errors, as in the
/// engine, so a typo never goes unnoticed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PackManifest {
    pub id: String,
    pub version: Version,
    pub layer: Layer,
    pub priority: i64,
    pub name: Option<String>,
    pub description: Option<String>,
    pub dependencies: Vec<Dependency>,
    /// Schema name to the id patterns of the documents that follow it.
    pub schemas: BTreeMap<String, Vec<String>>,
    pub api: Option<String>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RawManifest {
    id: String,
    version: String,
    layer: Layer,
    #[serde(default)]
    priority: i64,
    name: Option<String>,
    description: Option<String>,
    #[serde(default)]
    dependencies: Vec<RawDependency>,
    #[serde(default)]
    schemas: BTreeMap<String, Vec<String>>,
    api: Option<String>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RawDependency {
    id: String,
    min_version: String,
}

impl PackManifest {
    /// Parses the text of a `pack.yaml`; `path` only names the file in the errors.
    pub fn parse(text: &str, path: &Path) -> Result<Self> {
        let raw: RawManifest =
            serde_yaml_ng::from_str(text).map_err(|error| Error::yaml(path, &error))?;
        let in_file = |error: Error| Error::invalid(format!("{}: {error}", path.display()));
        check_pack_id(&raw.id).map_err(in_file)?;
        let mut dependencies = Vec::with_capacity(raw.dependencies.len());
        for dependency in raw.dependencies {
            check_pack_id(&dependency.id).map_err(in_file)?;
            dependencies.push(Dependency {
                id: dependency.id,
                min_version: Version::parse(&dependency.min_version).map_err(in_file)?,
            });
        }
        Ok(Self {
            version: Version::parse(&raw.version).map_err(in_file)?,
            id: raw.id,
            layer: raw.layer,
            priority: raw.priority,
            name: raw.name,
            description: raw.description,
            dependencies,
            schemas: raw.schemas,
            api: raw.api,
        })
    }

    /// Reads `<root>/pack.yaml`.
    pub fn read(root: &Path) -> Result<Self> {
        let path = root.join("pack.yaml");
        let text = std::fs::read_to_string(&path).map_err(|error| Error::io(&path, error))?;
        Self::parse(&text, &path)
    }
}

/// A pack id: 1 to 64 lowercase letters, digits and underscores.
pub(crate) fn check_pack_id(id: &str) -> Result<()> {
    const MAX_LENGTH: usize = 64;
    if id.is_empty() || id.len() > MAX_LENGTH {
        return Err(Error::invalid(format!(
            "pack id '{id}' must have 1 to {MAX_LENGTH} characters"
        )));
    }
    if !id.chars().all(crate::asset_id::is_id_character) {
        return Err(Error::invalid(format!(
            "pack id '{id}' may only use a-z, 0-9 and '_'"
        )));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(text: &str) -> Result<PackManifest> {
        PackManifest::parse(text, Path::new("pack.yaml"))
    }

    #[test]
    fn reads_a_full_manifest() {
        let manifest = parse(
            "id: hd\nversion: 1.2.0\nlayer: remaster\npriority: 3\nname: HD\n\
             dependencies:\n  - id: base\n    min_version: 1.0.0\n\
             schemas:\n  map: [map/*/data]\napi: 1.x\n",
        )
        .unwrap();
        assert_eq!(manifest.id, "hd");
        assert_eq!(
            manifest.version,
            Version {
                major: 1,
                minor: 2,
                patch: 0
            }
        );
        assert_eq!(manifest.layer, Layer::Remaster);
        assert_eq!(manifest.priority, 3);
        assert_eq!(manifest.dependencies[0].id, "base");
        assert_eq!(manifest.schemas["map"], vec!["map/*/data".to_owned()]);
    }

    #[test]
    fn refuses_unknown_keys_bad_versions_and_layers() {
        assert!(parse("id: a\nversion: 1.0.0\nlayer: base\ntypo: 1\n").is_err());
        assert!(parse("id: a\nversion: 1.0.0-beta\nlayer: base\n").is_err());
        assert!(parse("id: a\nversion: 1.0.0\nlayer: top\n").is_err());
        assert!(parse("id: A\nversion: 1.0.0\nlayer: base\n").is_err());
        assert_eq!(
            parse("id: a\nversion: 1.0.0\nlayer: base\n")
                .unwrap()
                .priority,
            0
        );
    }
}
