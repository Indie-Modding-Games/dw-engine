//! The names the tools show for the story's triggers and variables (`story/names` in the game pack,
//! content/schemas/story_names.schema.json), read with the engine's checks
//! (packages/game/story/src/story_names.cpp). The story runs on numbers; a name is only a label.

use std::collections::{BTreeMap, BTreeSet};

use dwe_pack::{AssetId, Vfs};
use serde_yaml_ng::Value;

use crate::reader::{Checked, expect_keys, key_text};
use crate::{Error, Result};

/// The id of the names' document.
pub const NAMES_ID: &str = "story/names";
/// How many triggers and variables the story's state has.
pub const TRIGGER_COUNT: u32 = 800;
pub const VARIABLE_COUNT: u32 = 256;

/// The labels of the story's triggers and variables, by number.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct StoryNames {
    pub triggers: BTreeMap<u32, String>,
    pub variables: BTreeMap<u32, String>,
}

impl StoryNames {
    pub fn trigger(&self, trigger: u32) -> Option<&str> {
        self.triggers.get(&trigger).map(String::as_str)
    }

    pub fn variable(&self, variable: u32) -> Option<&str> {
        self.variables.get(&variable).map(String::as_str)
    }

    /// Parses the text of `story/names`: numbers within the state, names that are snake_case ids
    /// and no name twice within a kind (parseStoryNames).
    pub fn parse(yaml: &str) -> Result<Self> {
        let invalid = |message: String| Error::Invalid(format!("story names: {message}"));
        let tree: Value =
            serde_yaml_ng::from_str(yaml).map_err(|error| invalid(error.to_string()))?;
        read_names(&tree).map_err(invalid)
    }

    /// Reads `story/names` from the packs (the file that wins); without one, no names
    /// (loadStoryNames).
    pub fn load(vfs: &Vfs) -> Result<Self> {
        let id = AssetId::parse(NAMES_ID)?;
        if vfs.resolve(&id).is_none() {
            return Ok(Self::default());
        }
        Self::parse(&vfs.read_text(&id)?)
    }
}

fn read_names(tree: &Value) -> Checked<StoryNames> {
    let root = expect_keys(tree, "the story names", &[], &["triggers", "variables"])?;
    Ok(StoryNames {
        triggers: match root.get("triggers") {
            Some(node) => read_kind(node, "triggers", TRIGGER_COUNT)?,
            None => BTreeMap::new(),
        },
        variables: match root.get("variables") {
            Some(node) => read_kind(node, "variables", VARIABLE_COUNT)?,
            None => BTreeMap::new(),
        },
    })
}

fn read_kind(node: &Value, kind: &str, count: u32) -> Checked<BTreeMap<u32, String>> {
    let Value::Mapping(entries) = node else {
        return Err(format!("{kind} must be a mapping of numbers to names"));
    };
    let mut names = BTreeMap::new();
    let mut seen = BTreeSet::new();
    for (key, value) in entries {
        let key = key_text(key);
        let number = key
            .parse::<u32>()
            .ok()
            .filter(|&number| number < count && !key.starts_with('+'))
            .ok_or_else(|| format!("{kind}: '{key}' is not a number from 0 to {}", count - 1))?;
        let name = match value {
            Value::String(name) => name.clone(),
            Value::Number(number) => number.to_string(),
            Value::Bool(value) => value.to_string(),
            _ => return Err("expected a single value".to_owned()),
        };
        let is_id = !name.is_empty()
            && name
                .bytes()
                .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_');
        if !is_id {
            return Err(format!(
                "'{name}' is not a name id: use lower-case letters, digits and '_'"
            ));
        }
        if !seen.insert(name.clone()) {
            return Err(format!("{kind}: the name '{name}' is given twice"));
        }
        names.insert(number, name);
    }
    Ok(names)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_the_labels_and_refuses_what_the_engine_refuses() {
        let names = StoryNames::parse(
            "triggers:\n  12: door_open\n  700: last_one\nvariables:\n  3: counter\n",
        )
        .unwrap();
        assert_eq!(names.trigger(12), Some("door_open"));
        assert_eq!(names.trigger(13), None);
        assert_eq!(names.variable(3), Some("counter"));
        assert_eq!(StoryNames::parse("{}").unwrap(), StoryNames::default());
        for (yaml, expected) in [
            ("triggers:\n  800: late\n", "from 0 to 799"),
            ("variables:\n  256: late\n", "from 0 to 255"),
            ("triggers:\n  -1: early\n", "not a number"),
            ("triggers:\n  1: Door\n", "not a name id"),
            ("triggers:\n  1: a\n  2: a\n", "twice"),
            ("triggers: [1]\n", "mapping of numbers"),
            ("flags: {}\n", "unknown key 'flags'"),
        ] {
            let error = StoryNames::parse(yaml).unwrap_err().to_string();
            assert!(error.starts_with("story names: "), "{error}");
            assert!(error.contains(expected), "{error} lacks {expected}");
        }
    }
}
