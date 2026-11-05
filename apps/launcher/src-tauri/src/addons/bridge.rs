//! The add-on bridge's backend half (API v1). The launcher's page receives an add-on's
//! postMessage, works out which add-on sent it from the iframe it came from (never from what the
//! message claims), handles the interface calls itself (ui.*, notify) and forwards only the calls
//! below to the `addon_call` command. Everything else is refused here too: an add-on can never
//! name a launcher command, a file or the token.

use serde::Serialize;
use serde_json::{json, Value};
use std::path::Path;

use super::manifest::Manifest;
use super::net;
use super::AddonStore;
use crate::failure::{code, Failure, Result};

/// The calls the backend answers for an add-on.
#[derive(Debug, PartialEq)]
pub enum Call {
    Context,
    StorageGet(String),
    StorageSet(String, Value),
    StorageRemove(String),
    StorageKeys,
    Fetch(net::Request),
}

/// The calls the page answers itself (and checks against the add-on's slots and permissions).
pub const PAGE_CALLS: [&str; 7] =
    ["ui.sidebar", "ui.tab", "ui.news", "ui.settings", "ui.resize", "notify", "events.subscribe"];

fn denied(method: &str) -> Failure {
    Failure::new(code::ADDON_DENIED, format!("unknown call {}", method.chars().take(60).collect::<String>()))
}

fn key(params: &Value) -> Result<String> {
    params.get("key").and_then(Value::as_str).map(str::to_string).ok_or_else(|| Failure::new(code::ADDON_DENIED, "storage key"))
}

/// The call a method name and its parameters stand for; anything not in API v1 is refused.
pub fn parse(method: &str, params: &Value) -> Result<Call> {
    Ok(match method {
        "context" => Call::Context,
        "storage.get" => Call::StorageGet(key(params)?),
        "storage.set" => Call::StorageSet(key(params)?, params.get("value").cloned().unwrap_or(Value::Null)),
        "storage.remove" => Call::StorageRemove(key(params)?),
        "storage.keys" => Call::StorageKeys,
        "net.fetch" => Call::Fetch(
            serde_json::from_value(params.clone()).map_err(|_| Failure::new(code::ADDON_DENIED, "net.fetch parameters"))?,
        ),
        other => return Err(denied(other)),
    })
}

/// What an add-on may know about the launcher: versions, language and whether the game is
/// installed. Never a path, the token, the release server or the disc.
#[derive(Debug, Serialize, PartialEq)]
pub struct Context {
    pub api: u64,
    pub addon: AddonInfo,
    pub launcher_version: &'static str,
    pub game_version: Option<String>,
    pub language: String,
    pub game_installed: bool,
    pub content_installed: bool,
    pub platform: String,
}

#[derive(Debug, Serialize, PartialEq)]
pub struct AddonInfo {
    pub id: String,
    pub version: String,
    pub permissions: Vec<String>,
}

/// What the backend knows that a context needs (filled by the command from the launcher's state).
pub struct Facts {
    pub language: String,
    pub game_version: Option<String>,
    pub content_installed: bool,
    pub platform: String,
}

pub fn context(manifest: &Manifest, facts: &Facts) -> Context {
    Context {
        api: manifest.api,
        addon: AddonInfo { id: manifest.id.clone(), version: manifest.version.clone(), permissions: manifest.permissions.keys() },
        launcher_version: env!("CARGO_PKG_VERSION"),
        game_installed: facts.game_version.is_some(),
        game_version: facts.game_version.clone(),
        language: facts.language.clone(),
        content_installed: facts.content_installed,
        platform: facts.platform.clone(),
    }
}

fn needs(granted: bool, what: &str) -> Result<()> {
    if granted {
        Ok(())
    } else {
        Err(Failure::new(code::ADDON_DENIED, format!("permission {what}")))
    }
}

/// Runs a call for the active add-on `id` (None when it is off, missing or in safe mode).
pub async fn run(data: &Path, id: &str, active: Option<Manifest>, call: Call, facts: Facts) -> Result<Value> {
    let manifest = active.ok_or_else(|| Failure::new(code::ADDON_NOT_FOUND, id))?;
    let store = AddonStore::new(data);
    let storage = store.storage(&manifest.id);
    let perms = &manifest.permissions;
    Ok(match call {
        Call::Context => serde_json::to_value(context(&manifest, &facts)).expect("context serializes"),
        Call::StorageGet(key) => {
            needs(perms.storage, "storage")?;
            storage.get(&key)?
        }
        Call::StorageSet(key, value) => {
            needs(perms.storage, "storage")?;
            storage.set(&key, value)?;
            Value::Null
        }
        Call::StorageRemove(key) => {
            needs(perms.storage, "storage")?;
            storage.set(&key, Value::Null)?;
            Value::Null
        }
        Call::StorageKeys => {
            needs(perms.storage, "storage")?;
            json!(storage.keys())
        }
        Call::Fetch(request) => {
            needs(!perms.hosts.is_empty(), "hosts")?;
            serde_json::to_value(net::fetch(request, &perms.hosts).await?).expect("response serializes")
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_api_v1_calls_are_parsed() {
        assert_eq!(parse("context", &Value::Null).unwrap(), Call::Context);
        assert_eq!(parse("storage.get", &json!({ "key": "a" })).unwrap(), Call::StorageGet("a".into()));
        assert!(matches!(parse("net.fetch", &json!({ "url": "https://example.org" })).unwrap(), Call::Fetch(_)));
        assert_eq!(parse("storage.get", &json!({})).unwrap_err().code, code::ADDON_DENIED);
        assert_eq!(parse("net.fetch", &json!({ "url": 3 })).unwrap_err().code, code::ADDON_DENIED);
        for method in PAGE_CALLS {
            assert_eq!(parse(method, &Value::Null).unwrap_err().code, code::ADDON_DENIED, "{method} is the page's");
        }
    }
}
