use std::fmt;

use crate::{Error, Result};

/// The id of an asset: segments of `a-z`, `0-9` and `_` separated by `/`, at most 200 characters
/// (`map/twna01/background_day`). A file's id is its path inside its pack without the extension.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct AssetId(String);

impl AssetId {
    pub const MAX_LENGTH: usize = 200;

    pub fn parse(text: &str) -> Result<Self> {
        if text.is_empty() {
            return Err(Error::invalid("an asset id cannot be empty"));
        }
        if text.len() > Self::MAX_LENGTH {
            return Err(Error::invalid(format!(
                "asset id '{text}' is longer than {} characters",
                Self::MAX_LENGTH
            )));
        }
        if text.starts_with('/') || text.ends_with('/') || text.contains("//") {
            return Err(Error::invalid(format!(
                "asset id '{text}' has an empty segment"
            )));
        }
        if !text.chars().all(|c| c == '/' || is_id_character(c)) {
            return Err(Error::invalid(format!(
                "asset id '{text}' may only use a-z, 0-9, '_' and '/'"
            )));
        }
        Ok(Self(text.to_owned()))
    }

    /// The id of a file from its path inside the pack, with `/` separators: the name needs exactly
    /// one dot and a lowercase extension of letters and digits.
    pub fn of_file(relative_path: &str) -> Result<Self> {
        let name_start = relative_path.rfind('/').map_or(0, |slash| slash + 1);
        let name = &relative_path[name_start..];
        let dot = match name.find('.') {
            Some(dot) if dot > 0 && !name[dot + 1..].contains('.') => dot,
            _ => {
                return Err(Error::invalid(format!(
                    "file '{relative_path}' must be named <name>.<extension>, with a single dot"
                )));
            }
        };
        let extension = &name[dot + 1..];
        if extension.is_empty()
            || !extension
                .chars()
                .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit())
        {
            return Err(Error::invalid(format!(
                "file '{relative_path}' needs a lowercase extension of letters and digits"
            )));
        }
        Self::parse(&relative_path[..name_start + dot])
            .map_err(|error| Error::invalid(format!("file '{relative_path}': {error}")))
    }

    /// An id from text already known to be valid, or a bound for a range of ids.
    pub(crate) fn unchecked(text: &str) -> Self {
        Self(text.to_owned())
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for AssetId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

pub(crate) fn is_id_character(c: char) -> bool {
    c.is_ascii_lowercase() || c.is_ascii_digit() || c == '_'
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_valid_ids_and_refuses_the_rest() {
        assert_eq!(
            AssetId::parse("map/area_a/data").unwrap().as_str(),
            "map/area_a/data"
        );
        for bad in ["", "/a", "a/", "a//b", "Map/a", "a.b", "a-b"] {
            assert!(AssetId::parse(bad).is_err(), "{bad}");
        }
        assert!(AssetId::parse(&"a".repeat(201)).is_err());
    }

    #[test]
    fn a_file_id_drops_the_single_extension() {
        assert_eq!(
            AssetId::of_file("map/a/background_day.png")
                .unwrap()
                .as_str(),
            "map/a/background_day"
        );
        for bad in [
            "map/a/model.lod1.gltf",
            "map/a/noext",
            "map/a/.hidden",
            "map/a/b.PNG",
        ] {
            assert!(AssetId::of_file(bad).is_err(), "{bad}");
        }
    }
}
