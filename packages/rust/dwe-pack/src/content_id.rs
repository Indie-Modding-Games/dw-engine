use std::fmt;

use crate::asset_id::is_id_character;
use crate::manifest::check_pack_id;
use crate::{Error, Result};

/// The id of an entry of a content table (content_id.hpp): an original's stable name (`area_a`),
/// or `<pack id>:<name>` for what a pack adds.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ContentId {
    text: String,
    separator: Option<usize>,
}

impl ContentId {
    pub const MAX_NAME_LENGTH: usize = 64;

    pub fn parse(text: &str) -> Result<Self> {
        if text.is_empty() {
            return Err(Error::invalid("a content id cannot be empty"));
        }
        let separator = text.find(':');
        if let Some(separator) = separator {
            check_pack_id(&text[..separator]).map_err(|error| {
                Error::invalid(format!("content id '{text}' has a bad namespace: {error}"))
            })?;
            check_name(text, &text[separator + 1..])?;
        } else {
            check_name(text, text)?;
        }
        Ok(Self {
            text: text.to_owned(),
            separator,
        })
    }

    pub fn as_str(&self) -> &str {
        &self.text
    }

    pub fn is_namespaced(&self) -> bool {
        self.separator.is_some()
    }

    /// The pack that added the entry, or "" for an original's.
    pub fn pack_namespace(&self) -> &str {
        self.separator.map_or("", |at| &self.text[..at])
    }

    pub fn name(&self) -> &str {
        self.separator.map_or(&self.text, |at| &self.text[at + 1..])
    }

    /// The folder path of the entry's assets: one folder per part (`pack/name`).
    pub fn asset_path(&self) -> String {
        match self.separator {
            Some(_) => format!("{}/{}", self.pack_namespace(), self.name()),
            None => self.text.clone(),
        }
    }

    /// The asset path of an id, or the text as it is when it is not a content id
    /// (contentAssetPath).
    pub fn asset_path_of(text: &str) -> String {
        Self::parse(text).map_or_else(|_| text.to_owned(), |id| id.asset_path())
    }
}

impl fmt::Display for ContentId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.text)
    }
}

fn check_name(id: &str, name: &str) -> Result<()> {
    if name.is_empty() || !name.chars().all(is_id_character) {
        return Err(Error::invalid(format!(
            "content id '{id}' may only use a-z, 0-9 and '_' around one ':'"
        )));
    }
    if name.len() > ContentId::MAX_NAME_LENGTH {
        return Err(Error::invalid(format!(
            "content id '{id}' has a name longer than {} characters",
            ContentId::MAX_NAME_LENGTH
        )));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn namespaced_ids_get_a_folder_per_part() {
        let id = ContentId::parse("example_mod:meadow").unwrap();
        assert!(id.is_namespaced());
        assert_eq!(id.pack_namespace(), "example_mod");
        assert_eq!(id.name(), "meadow");
        assert_eq!(id.asset_path(), "example_mod/meadow");
        assert_eq!(ContentId::parse("area_a").unwrap().asset_path(), "area_a");
        for bad in ["", "a:b:c", "A", ":b", "a:", "a b"] {
            assert!(ContentId::parse(bad).is_err(), "{bad}");
        }
    }
}
