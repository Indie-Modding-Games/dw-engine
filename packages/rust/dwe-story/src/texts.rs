//! The story's texts: `text/<language>/story_<nnn>.yaml`, flat mappings of `text.<offset>` to the
//! line, merged key by key from the bottom of the stack up (docs/architecture/localizacion.md:
//! `assets::readTextTable`). A command names a line by its full key, `story_001.text.00aa`.

use std::collections::BTreeMap;
use std::path::Path;

use dwe_pack::{AssetId, Vfs};
use serde_yaml_ng::Value;

use crate::{Error, Result};

/// The language the extractor writes and every other falls back to.
pub const ENGLISH: &str = "en";

/// Splits a full key into its table and its key in the table:
/// `story_001.text.00aa` → (`story_001`, `text.00aa`).
pub fn split_key(key: &str) -> Option<(&str, &str)> {
    let (table, rest) = key.split_once('.')?;
    (!table.is_empty() && !rest.is_empty()).then_some((table, rest))
}

/// The id of a text table: `text/en/story_001`.
pub fn table_id(language: &str, table: &str) -> String {
    format!("text/{language}/{table}")
}

/// The texts read so far, by table.
#[derive(Clone, Debug, Default)]
pub struct StoryTexts {
    pub language: String,
    tables: BTreeMap<String, BTreeMap<String, String>>,
}

impl StoryTexts {
    pub fn new(language: &str) -> Self {
        Self {
            language: language.to_owned(),
            tables: BTreeMap::new(),
        }
    }

    /// Reads a table from the packs (merged), once; the language's lines over the English ones.
    pub fn load_table(&mut self, vfs: &Vfs, table: &str) {
        if self.tables.contains_key(table) {
            return;
        }
        let mut lines = read_merged(vfs, ENGLISH, table);
        if self.language != ENGLISH {
            lines.extend(read_merged(vfs, &self.language, table));
        }
        self.tables.insert(table.to_owned(), lines);
    }

    /// The line of a full key, when its table was loaded and has it.
    pub fn text(&self, key: &str) -> Option<&str> {
        let (table, key) = split_key(key)?;
        self.tables.get(table)?.get(key).map(String::as_str)
    }

    /// Sets a line in memory (an edit not saved yet, or one just saved).
    pub fn set(&mut self, key: &str, text: Option<&str>) {
        let Some((table, inner)) = split_key(key) else {
            return;
        };
        let lines = self.tables.entry(table.to_owned()).or_default();
        match text {
            Some(text) => lines.insert(inner.to_owned(), text.to_owned()),
            None => lines.remove(inner),
        };
    }

    /// The keys of a loaded table, full.
    pub fn keys(&self, table: &str) -> Vec<String> {
        self.tables
            .get(table)
            .map(|lines| lines.keys().map(|key| format!("{table}.{key}")).collect())
            .unwrap_or_default()
    }

    /// Drops what was read, to read the packs again.
    pub fn clear(&mut self) {
        self.tables.clear();
    }
}

fn read_merged(vfs: &Vfs, language: &str, table: &str) -> BTreeMap<String, String> {
    let mut lines = BTreeMap::new();
    let Ok(id) = AssetId::parse(&table_id(language, table)) else {
        return lines;
    };
    for pack in vfs.sources(&id) {
        if let Some(file) = pack.file(&id)
            && let Ok(table) = read_table_file(file)
        {
            lines.extend(table);
        }
    }
    lines
}

/// Reads one text table file (a flat mapping).
pub fn read_table_file(file: &Path) -> Result<BTreeMap<String, String>> {
    let text = std::fs::read_to_string(file)
        .map_err(|error| Error::Invalid(format!("{}: {error}", file.display())))?;
    parse_table(&text).map_err(|error| Error::Invalid(format!("{}: {error}", file.display())))
}

pub fn parse_table(text: &str) -> std::result::Result<BTreeMap<String, String>, String> {
    let value: Value = serde_yaml_ng::from_str(text).map_err(|error| error.to_string())?;
    let mut lines = BTreeMap::new();
    match value {
        Value::Null => {}
        Value::Mapping(mapping) => {
            for (key, value) in mapping {
                let (Some(key), Some(value)) = (key.as_str(), value.as_str()) else {
                    return Err("a text table maps keys to texts".to_owned());
                };
                lines.insert(key.to_owned(), value.to_owned());
            }
        }
        _ => return Err("a text table maps keys to texts".to_owned()),
    }
    Ok(lines)
}

/// Writes a text table: one `key: "text"` a line, in key order, as `dwx` writes them.
pub fn write_table(lines: &BTreeMap<String, String>) -> String {
    let mut out = String::new();
    for (key, text) in lines {
        out.push_str(key);
        out.push_str(": ");
        out.push_str(&yaml_quote(text));
        out.push('\n');
    }
    out
}

fn yaml_quote(text: &str) -> String {
    let mut quoted = String::from("\"");
    for c in text.chars() {
        match c {
            '"' => quoted.push_str("\\\""),
            '\\' => quoted.push_str("\\\\"),
            '\n' => quoted.push_str("\\n"),
            '\t' => quoted.push_str("\\t"),
            c if (c as u32) < 0x20 => quoted.push_str(&format!("\\x{:02x}", c as u32)),
            c => quoted.push(c),
        }
    }
    quoted.push('"');
    quoted
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tables_round_trip() {
        let mut lines = BTreeMap::new();
        lines.insert("text.0010".to_owned(), "Hola \"tú\"\nadiós\\".to_owned());
        lines.insert("text.0002".to_owned(), "".to_owned());
        let text = write_table(&lines);
        assert!(text.starts_with("text.0002: \"\"\n"));
        assert_eq!(parse_table(&text).unwrap(), lines);
    }

    #[test]
    fn splits_keys() {
        assert_eq!(
            split_key("story_001.text.00aa"),
            Some(("story_001", "text.00aa"))
        );
        assert_eq!(split_key("nothing"), None);
    }
}
