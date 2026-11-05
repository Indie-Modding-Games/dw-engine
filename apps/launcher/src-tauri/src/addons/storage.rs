//! Each add-on's own key-value storage: one JSON file per add-on in <data>/addon-storage/, outside
//! the add-on's folder (an update keeps it), capped in size.

use serde_json::{Map, Value};
use std::path::{Path, PathBuf};

use crate::failure::{code, Failure, Result};
use crate::store::write_atomically;

/// The most one add-on may keep (its whole storage, as JSON).
pub const MAX_STORAGE_BYTES: usize = 256 * 1024;
pub const MAX_KEY_CHARS: usize = 128;

pub struct Storage {
    file: PathBuf,
}

impl Storage {
    pub fn new(data: &Path, id: &str) -> Self {
        Self { file: data.join("addon-storage").join(format!("{id}.json")) }
    }

    pub fn file(&self) -> &Path {
        &self.file
    }

    fn read(&self) -> Map<String, Value> {
        std::fs::read_to_string(&self.file)
            .ok()
            .and_then(|text| serde_json::from_str(&text).ok())
            .unwrap_or_default()
    }

    fn check_key(key: &str) -> Result<()> {
        if key.is_empty() || key.chars().count() > MAX_KEY_CHARS {
            return Err(Failure::new(code::ADDON_DENIED, "storage key"));
        }
        Ok(())
    }

    pub fn get(&self, key: &str) -> Result<Value> {
        Self::check_key(key)?;
        Ok(self.read().remove(key).unwrap_or(Value::Null))
    }

    pub fn keys(&self) -> Vec<String> {
        self.read().keys().cloned().collect()
    }

    /// Sets a key (null removes it); refused, and nothing written, when the result would pass
    /// the cap.
    pub fn set(&self, key: &str, value: Value) -> Result<()> {
        Self::check_key(key)?;
        let mut all = self.read();
        if value.is_null() {
            all.remove(key);
        } else {
            all.insert(key.to_string(), value);
        }
        let text = serde_json::to_vec(&all).expect("JSON values serialize");
        if text.len() > MAX_STORAGE_BYTES {
            return Err(Failure::new(code::ADDON_STORAGE_FULL, MAX_STORAGE_BYTES.to_string()));
        }
        write_atomically(&self.file, &text)
    }

    pub fn clear(&self) {
        let _ = std::fs::remove_file(&self.file);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn values_round_trip_and_the_cap_holds() {
        let dir = tempfile::tempdir().unwrap();
        let storage = Storage::new(dir.path(), "org.example.notes");
        assert_eq!(storage.get("note").unwrap(), Value::Null);
        storage.set("note", json!({ "text": "hola", "n": 3 })).unwrap();
        assert_eq!(storage.get("note").unwrap()["text"], "hola");
        assert_eq!(storage.keys(), ["note"]);

        let big = "x".repeat(MAX_STORAGE_BYTES);
        assert_eq!(storage.set("big", json!(big)).unwrap_err().code, code::ADDON_STORAGE_FULL);
        assert_eq!(storage.keys(), ["note"], "a refused write changes nothing");
        storage.set("half", json!("y".repeat(MAX_STORAGE_BYTES / 2))).unwrap();
        assert_eq!(storage.set("other-half", json!("y".repeat(MAX_STORAGE_BYTES / 2))).unwrap_err().code, code::ADDON_STORAGE_FULL);

        storage.set("note", Value::Null).unwrap();
        assert_eq!(storage.keys(), ["half"]);
        assert_eq!(storage.get("").unwrap_err().code, code::ADDON_DENIED);
        assert_eq!(storage.set(&"k".repeat(MAX_KEY_CHARS + 1), json!(1)).unwrap_err().code, code::ADDON_DENIED);

        // Each add-on has its own file.
        let other = Storage::new(dir.path(), "org.example.other");
        assert!(other.keys().is_empty());
        storage.clear();
        assert!(storage.keys().is_empty());
    }
}
