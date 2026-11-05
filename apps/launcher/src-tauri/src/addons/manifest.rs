//! An add-on's addon.json (docs/guides/launcher-addons.md): who it is, which API version it was
//! written for, its page, the slots it uses and the permissions it asks for. Every field is
//! checked here; a problem is a Failure "addon-invalid" whose detail is "<field>:<reason>", which
//! the page turns into a sentence in the player's language.

use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use std::path::{Component, Path};

use crate::failure::{code, Failure, Result};

/// The add-on API versions this launcher speaks (the bridge, dwe-addons.js).
pub const SUPPORTED_APIS: [u64; 1] = [1];
pub const MANIFEST_FILE: &str = "addon.json";
pub const MAX_MANIFEST_BYTES: u64 = 64 * 1024;
pub const SLOTS: [&str; 4] = ["sidebar", "tabs", "news", "settings"];
pub const MAX_HOSTS: usize = 8;

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct Permissions {
    /// Its own small key-value storage.
    pub storage: bool,
    /// Short notices (toasts) shown by the launcher.
    pub notify: bool,
    /// The hosts net.fetch may reach ("example.org" or "127.0.0.1:8765").
    pub hosts: Vec<String>,
}

impl Permissions {
    /// Everything `other` allows that this does not: what a player has to accept again.
    pub fn added_in(&self, other: &Permissions) -> Vec<String> {
        let mut added = Vec::new();
        if other.storage && !self.storage {
            added.push("storage".to_string());
        }
        if other.notify && !self.notify {
            added.push("notify".to_string());
        }
        for host in &other.hosts {
            if !self.hosts.contains(host) {
                added.push(format!("net:{host}"));
            }
        }
        added
    }

    /// Whether everything `wanted` asks for is already allowed here.
    pub fn covers(&self, wanted: &Permissions) -> bool {
        self.added_in(wanted).is_empty()
    }

    /// The permissions as the page lists them: "storage", "notify", "net:<host>".
    pub fn keys(&self) -> Vec<String> {
        Permissions::default().added_in(self)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Manifest {
    pub id: String,
    pub name: String,
    pub version: String,
    pub author: String,
    pub description: String,
    pub api: u64,
    pub entry: String,
    pub slots: Vec<String>,
    pub permissions: Permissions,
}

fn invalid(field: &str, reason: &str) -> Failure {
    Failure::new(code::ADDON_INVALID, format!("{field}:{reason}"))
}

/// A reverse-DNS-like id: two or more dot-separated parts of lowercase letters, digits and
/// hyphens ("org.example.notes"), 3 to 64 characters. It is also the add-on's folder name.
pub fn is_valid_id(id: &str) -> bool {
    let parts: Vec<&str> = id.split('.').collect();
    (3..=64).contains(&id.len())
        && parts.len() >= 2
        && id.starts_with(|c: char| c.is_ascii_lowercase())
        && parts.iter().all(|part| {
            !part.is_empty()
                && !part.starts_with('-')
                && !part.ends_with('-')
                && part.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-')
        })
}

/// A host an add-on may reach: a lowercase DNS name with a dot (or "localhost") or an IPv4
/// address, with an optional port. No scheme, no path, no wildcard.
pub fn is_valid_host(host: &str) -> bool {
    let (name, port) = match host.rsplit_once(':') {
        Some((name, port)) => (name, Some(port)),
        None => (host, None),
    };
    if let Some(port) = port {
        if port.parse::<u16>().map_or(true, |p| p == 0) || port.starts_with('0') {
            return false;
        }
    }
    let labels: Vec<&str> = name.split('.').collect();
    let dns = labels.iter().all(|label| {
        (1..=63).contains(&label.len())
            && !label.starts_with('-')
            && !label.ends_with('-')
            && label.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-')
    });
    let ipv4 = name.parse::<std::net::Ipv4Addr>().is_ok();
    name.len() <= 253 && dns && (labels.len() >= 2 || name == "localhost" || ipv4)
}

/// A path inside the add-on's folder: relative, plain names only.
pub fn is_safe_relative(path: &str) -> bool {
    !path.is_empty()
        && path.len() <= 200
        && !path.contains('\\')
        && !path.contains('\0')
        && Path::new(path).components().all(|c| matches!(c, Component::Normal(_)))
}

fn text(object: &Map<String, Value>, field: &str, max: usize, required: bool) -> Result<String> {
    match object.get(field) {
        None | Some(Value::Null) if !required => Ok(String::new()),
        None | Some(Value::Null) => Err(invalid(field, "missing")),
        Some(Value::String(value)) => {
            let value = value.trim();
            if required && value.is_empty() {
                Err(invalid(field, "missing"))
            } else if value.chars().count() > max {
                Err(invalid(field, "too-long"))
            } else if value.chars().any(char::is_control) {
                Err(invalid(field, "format"))
            } else {
                Ok(value.to_string())
            }
        }
        Some(_) => Err(invalid(field, "format")),
    }
}

fn flag(object: &Map<String, Value>, field: &str) -> Result<bool> {
    match object.get(field) {
        None | Some(Value::Null) => Ok(false),
        Some(Value::Bool(value)) => Ok(*value),
        Some(_) => Err(invalid(&format!("permissions.{field}"), "format")),
    }
}

fn permissions(value: Option<&Value>) -> Result<Permissions> {
    let object = match value {
        None | Some(Value::Null) => return Ok(Permissions::default()),
        Some(Value::Object(object)) => object,
        Some(_) => return Err(invalid("permissions", "format")),
    };
    // A permission this launcher does not know means the add-on expects something it cannot give.
    if let Some(unknown) = object.keys().find(|k| !matches!(k.as_str(), "storage" | "notify" | "hosts")) {
        return Err(invalid(&format!("permissions.{unknown}"), "unknown-permission"));
    }
    let mut hosts = Vec::new();
    match object.get("hosts") {
        None | Some(Value::Null) => {}
        Some(Value::Array(list)) => {
            if list.len() > MAX_HOSTS {
                return Err(invalid("permissions.hosts", "too-many"));
            }
            for host in list {
                let Some(host) = host.as_str().filter(|h| is_valid_host(h)) else {
                    return Err(invalid("permissions.hosts", "bad-host"));
                };
                if hosts.iter().any(|h| h == host) {
                    return Err(invalid("permissions.hosts", "duplicate"));
                }
                hosts.push(host.to_string());
            }
        }
        Some(_) => return Err(invalid("permissions.hosts", "format")),
    }
    Ok(Permissions { storage: flag(object, "storage")?, notify: flag(object, "notify")?, hosts })
}

fn slots(value: Option<&Value>) -> Result<Vec<String>> {
    let Some(Value::Array(list)) = value else {
        return Err(invalid("slots", if value.is_none() { "missing" } else { "format" }));
    };
    let mut slots = Vec::new();
    for slot in list {
        let Some(slot) = slot.as_str() else { return Err(invalid("slots", "format")) };
        if !SLOTS.contains(&slot) {
            return Err(invalid("slots", "unknown-slot"));
        }
        if slots.iter().any(|s| s == slot) {
            return Err(invalid("slots", "duplicate"));
        }
        slots.push(slot.to_string());
    }
    if slots.is_empty() {
        return Err(invalid("slots", "missing"));
    }
    // A sidebar icon opens the add-on's tab: there must be one.
    if slots.iter().any(|s| s == "sidebar") && !slots.iter().any(|s| s == "tabs") {
        return Err(invalid("slots", "sidebar-needs-tab"));
    }
    Ok(slots)
}

impl Manifest {
    /// Reads and checks addon.json's text. The folder (whether the entry page exists) is checked
    /// by `read`.
    pub fn parse(source: &str) -> Result<Manifest> {
        let value: Value = serde_json::from_str(source).map_err(|_| invalid(MANIFEST_FILE, "not-json"))?;
        let Value::Object(object) = value else { return Err(invalid(MANIFEST_FILE, "not-json")) };
        // The API version first: a manifest for another API may have other fields.
        let api = match object.get("api") {
            None => return Err(invalid("api", "missing")),
            Some(value) => value.as_u64().ok_or_else(|| invalid("api", "format"))?,
        };
        if !SUPPORTED_APIS.contains(&api) {
            return Err(Failure::new(code::ADDON_API, api.to_string()));
        }
        let id = text(&object, "id", 64, true)?;
        if !is_valid_id(&id) {
            return Err(invalid("id", "format"));
        }
        let version = text(&object, "version", 32, true)?;
        if semver::Version::parse(&version).is_err() {
            return Err(invalid("version", "format"));
        }
        let entry = text(&object, "entry", 200, true)?;
        let lower = entry.to_ascii_lowercase();
        if !is_safe_relative(&entry) || !(lower.ends_with(".html") || lower.ends_with(".htm")) {
            return Err(invalid("entry", "format"));
        }
        Ok(Manifest {
            name: text(&object, "name", 40, true)?,
            author: text(&object, "author", 60, true)?,
            description: text(&object, "description", 300, false)?,
            slots: slots(object.get("slots"))?,
            permissions: permissions(object.get("permissions"))?,
            id,
            version,
            api,
            entry,
        })
    }

    /// Reads a folder's addon.json and checks that its entry page is there.
    pub fn read(folder: &Path) -> Result<Manifest> {
        let file = folder.join(MANIFEST_FILE);
        let meta = std::fs::symlink_metadata(&file).map_err(|_| invalid(MANIFEST_FILE, "missing"))?;
        if !meta.is_file() {
            return Err(invalid(MANIFEST_FILE, "missing"));
        }
        if meta.len() > MAX_MANIFEST_BYTES {
            return Err(invalid(MANIFEST_FILE, "too-long"));
        }
        let text = std::fs::read_to_string(&file).map_err(|_| invalid(MANIFEST_FILE, "not-json"))?;
        let manifest = Manifest::parse(&text)?;
        let entry = std::fs::symlink_metadata(folder.join(&manifest.entry));
        if !entry.is_ok_and(|m| m.is_file()) {
            return Err(invalid("entry", "entry-missing"));
        }
        Ok(manifest)
    }

    pub fn uses(&self, slot: &str) -> bool {
        self.slots.iter().any(|s| s == slot)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn good() -> Value {
        json!({
            "id": "org.example.notes", "name": "Notas", "version": "1.0.0", "author": "Equipo",
            "description": "Notas del jugador.", "api": 1, "entry": "index.html",
            "slots": ["sidebar", "tabs", "settings"],
            "permissions": { "storage": true, "notify": true, "hosts": ["example.org", "127.0.0.1:8765"] }
        })
    }

    fn with(field: &str, value: Value) -> String {
        let mut manifest = good();
        if value.is_null() {
            manifest.as_object_mut().unwrap().remove(field);
        } else {
            manifest[field] = value;
        }
        manifest.to_string()
    }

    fn detail(text: &str) -> String {
        let failure = Manifest::parse(text).unwrap_err();
        format!("{}|{}", failure.code, failure.detail)
    }

    #[test]
    fn a_good_manifest_is_read() {
        let manifest = Manifest::parse(&good().to_string()).unwrap();
        assert_eq!(manifest.id, "org.example.notes");
        assert!(manifest.uses("tabs") && !manifest.uses("news"));
        assert_eq!(manifest.permissions.keys(), ["storage", "notify", "net:example.org", "net:127.0.0.1:8765"]);
    }

    #[test]
    fn bad_manifests_say_which_field_is_wrong() {
        assert_eq!(detail("not json"), "addon-invalid|addon.json:not-json");
        assert_eq!(detail("[1]"), "addon-invalid|addon.json:not-json");
        assert_eq!(detail(&with("api", Value::Null)), "addon-invalid|api:missing");
        assert_eq!(detail(&with("api", json!("1"))), "addon-invalid|api:format");
        assert_eq!(detail(&with("id", Value::Null)), "addon-invalid|id:missing");
        for id in ["notes", "Org.Example", "org..x", "org.-x", "../evil.x", "org/example.x", "1org.x", "ab"] {
            assert_eq!(detail(&with("id", json!(id))), "addon-invalid|id:format", "{id}");
        }
        assert_eq!(detail(&with("version", json!("1.0"))), "addon-invalid|version:format");
        assert_eq!(detail(&with("name", json!(""))), "addon-invalid|name:missing");
        assert_eq!(detail(&with("name", json!("x".repeat(41)))), "addon-invalid|name:too-long");
        assert_eq!(detail(&with("author", json!(3))), "addon-invalid|author:format");
        for entry in ["../index.html", "/index.html", "index.js", "a\\b.html", "./index.html"] {
            assert_eq!(detail(&with("entry", json!(entry))), "addon-invalid|entry:format", "{entry}");
        }
        assert_eq!(detail(&with("slots", json!([]))), "addon-invalid|slots:missing");
        assert_eq!(detail(&with("slots", json!(["toolbar"]))), "addon-invalid|slots:unknown-slot");
        assert_eq!(detail(&with("slots", json!(["tabs", "tabs"]))), "addon-invalid|slots:duplicate");
        assert_eq!(detail(&with("slots", json!(["sidebar"]))), "addon-invalid|slots:sidebar-needs-tab");
    }

    #[test]
    fn unknown_api_versions_are_refused() {
        assert_eq!(detail(&with("api", json!(2))), "addon-api|2");
        assert_eq!(detail(&with("api", json!(0))), "addon-api|0");
    }

    #[test]
    fn permissions_are_checked() {
        let perms = |p: Value| detail(&with("permissions", p));
        assert_eq!(perms(json!({ "disc": true })), "addon-invalid|permissions.disc:unknown-permission");
        assert_eq!(perms(json!({ "token": true })), "addon-invalid|permissions.token:unknown-permission");
        assert_eq!(perms(json!({ "storage": "yes" })), "addon-invalid|permissions.storage:format");
        for host in ["https://example.org", "*.example.org", "example", "example.org/path", "EXAMPLE.org", "x.org:0", "x.org:99999", ""] {
            assert_eq!(perms(json!({ "hosts": [host] })), "addon-invalid|permissions.hosts:bad-host", "{host}");
        }
        let many: Vec<String> = (0..9).map(|i| format!("h{i}.example.org")).collect();
        assert_eq!(perms(json!({ "hosts": many })), "addon-invalid|permissions.hosts:too-many");
        assert_eq!(perms(json!({ "hosts": ["a.org", "a.org"] })), "addon-invalid|permissions.hosts:duplicate");
        assert!(Manifest::parse(&with("permissions", Value::Null)).unwrap().permissions == Permissions::default());
    }

    #[test]
    fn new_permissions_are_found() {
        let old = Permissions { storage: true, notify: false, hosts: vec!["a.org".into()] };
        let new = Permissions { storage: true, notify: true, hosts: vec!["a.org".into(), "b.org".into()] };
        assert_eq!(old.added_in(&new), ["notify", "net:b.org"]);
        assert!(new.covers(&old));
        assert!(!old.covers(&new));
    }
}
