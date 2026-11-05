//! Community add-ons (docs/guides/launcher-addons.md): folders with an addon.json and a page,
//! installed in <data>/addons/<id>/, shown by the launcher's page in sandboxed iframes and
//! talking to it only through the versioned bridge (bridge.rs). What an add-on may do is what
//! its manifest declares and the player accepted; nothing here can reach the token, the disc,
//! the updates, the game or files outside the add-on's own folder.

pub mod bridge;
pub mod manifest;
pub mod net;
pub mod package;
pub mod protocol;
pub mod storage;

use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, HashMap};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Mutex;

use crate::failure::{code, Failure, Result};
use crate::store::write_atomically;
pub use manifest::{Manifest, Permissions};

const STATE_FILE: &str = "addons.json";
const STAGING: &str = ".staging";

/// What the launcher remembers of each installed add-on: on or off, and what the player accepted.
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct Installed {
    pub enabled: bool,
    pub granted: Permissions,
}

/// An installed add-on as the page lists it.
#[derive(Debug, Clone, Serialize)]
pub struct Listed {
    /// The folder's name (the id, when the manifest is good).
    pub id: String,
    pub manifest: Option<Manifest>,
    pub enabled: bool,
    /// Permissions the manifest asks for that the player has not accepted (it was changed by
    /// hand): it stays off until they are accepted.
    pub ungranted: Vec<String>,
    pub problem: Option<Failure>,
    /// Loaded in this session: on, valid, accepted and not in safe mode.
    pub active: bool,
    pub base_url: String,
}

/// An add-on unpacked and checked, waiting for the player to accept its permissions.
#[derive(Debug, Clone, Serialize)]
pub struct Staged {
    pub ticket: String,
    pub manifest: Manifest,
    /// The installed version this replaces, if any.
    pub replaces: Option<String>,
    /// Permissions not accepted before (all of them for a new add-on).
    pub new_permissions: Vec<String>,
    #[serde(skip)]
    pub folder: PathBuf,
    #[serde(skip)]
    pub staging: PathBuf,
}

/// The add-ons' state for this run of the launcher.
#[derive(Default)]
pub struct Runtime {
    pub safe_mode: AtomicBool,
    /// The last run did not end cleanly while add-ons were on.
    pub crashed_last_time: AtomicBool,
    pub staged: Mutex<HashMap<String, Staged>>,
    next_ticket: AtomicU64,
}

impl Runtime {
    pub fn new(safe_mode: bool, crashed_last_time: bool) -> Self {
        Self { safe_mode: AtomicBool::new(safe_mode), crashed_last_time: AtomicBool::new(crashed_last_time), ..Self::default() }
    }

    pub fn safe_mode(&self) -> bool {
        self.safe_mode.load(Ordering::SeqCst)
    }

    pub fn ticket(&self) -> String {
        let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
        format!("{nanos:x}-{}", self.next_ticket.fetch_add(1, Ordering::SeqCst))
    }
}

pub struct AddonStore {
    root: PathBuf,
    data: PathBuf,
}

impl AddonStore {
    pub fn new(data: &Path) -> Self {
        Self { root: data.join("addons"), data: data.to_path_buf() }
    }

    pub fn root(&self) -> &Path {
        &self.root
    }

    pub fn folder(&self, id: &str) -> PathBuf {
        self.root.join(id)
    }

    pub fn storage(&self, id: &str) -> storage::Storage {
        storage::Storage::new(&self.data, id)
    }

    pub fn state(&self) -> BTreeMap<String, Installed> {
        fs::read_to_string(self.root.join(STATE_FILE))
            .ok()
            .and_then(|text| serde_json::from_str(&text).ok())
            .unwrap_or_default()
    }

    fn write_state(&self, state: &BTreeMap<String, Installed>) -> Result<()> {
        write_atomically(&self.root.join(STATE_FILE), &serde_json::to_vec_pretty(state).expect("state serializes"))
    }

    /// Every installed add-on, valid or not, by id.
    pub fn list(&self, safe_mode: bool) -> Vec<Listed> {
        let state = self.state();
        let Ok(entries) = fs::read_dir(&self.root) else { return Vec::new() };
        let mut listed: Vec<Listed> = entries
            .flatten()
            .filter(|e| e.file_type().is_ok_and(|t| t.is_dir()))
            .filter_map(|e| e.file_name().to_str().map(str::to_string))
            .filter(|name| !name.starts_with('.'))
            .map(|id| {
                let installed = state.get(&id).cloned().unwrap_or_default();
                let read = Manifest::read(&self.folder(&id)).and_then(|m| {
                    if m.id == id {
                        Ok(m)
                    } else {
                        Err(Failure::new(code::ADDON_INVALID, "id:folder-mismatch"))
                    }
                });
                let (manifest, problem) = match read {
                    Ok(m) => (Some(m), None),
                    Err(f) => (None, Some(f)),
                };
                let ungranted = manifest.as_ref().map(|m| installed.granted.added_in(&m.permissions)).unwrap_or_default();
                let active = installed.enabled && manifest.is_some() && ungranted.is_empty() && !safe_mode;
                Listed { base_url: protocol::base_url(&id), id, manifest, enabled: installed.enabled, ungranted, problem, active }
            })
            .collect();
        listed.sort_by(|a, b| a.id.cmp(&b.id));
        listed
    }

    /// The manifest of an add-on that may run now (on, valid, accepted).
    pub fn active(&self, id: &str, safe_mode: bool) -> Option<Manifest> {
        if safe_mode || !manifest::is_valid_id(id) {
            return None;
        }
        let installed = self.state().get(id).cloned()?;
        let manifest = Manifest::read(&self.folder(id)).ok().filter(|m| m.id == id)?;
        (installed.enabled && installed.granted.covers(&manifest.permissions)).then_some(manifest)
    }

    /// Unpacks and checks what the player picked into the staging folder.
    pub fn stage(&self, source: &Path, ticket: &str) -> Result<Staged> {
        let staging = self.root.join(STAGING).join(ticket);
        let _ = fs::remove_dir_all(&staging);
        let (folder, manifest) = package::stage(source, &staging)?;
        let state = self.state();
        let previous = Manifest::read(&self.folder(&manifest.id)).ok();
        let granted = state.get(&manifest.id).map(|i| i.granted.clone()).unwrap_or_default();
        let new_permissions = match &previous {
            Some(_) => granted.added_in(&manifest.permissions),
            None => manifest.permissions.keys(),
        };
        Ok(Staged {
            ticket: ticket.to_string(),
            replaces: previous.map(|m| m.version),
            new_permissions,
            manifest,
            folder,
            staging,
        })
    }

    /// Installs a staged add-on (the player accepted its permissions): replaces the folder of an
    /// older version, keeps its on/off state and its storage.
    pub fn install(&self, staged: &Staged) -> Result<Listed> {
        let id = &staged.manifest.id;
        let target = self.folder(id);
        let old = self.root.join(STAGING).join(format!("{}-old", staged.ticket));
        if target.exists() {
            fs::rename(&target, &old)?;
        }
        if let Err(error) = fs::rename(&staged.folder, &target) {
            if old.exists() {
                let _ = fs::rename(&old, &target);
            }
            return Err(error.into());
        }
        let _ = fs::remove_dir_all(&old);
        let _ = fs::remove_dir_all(&staged.staging);
        let mut state = self.state();
        let entry = state.entry(id.clone()).or_insert(Installed { enabled: true, granted: Permissions::default() });
        entry.granted = staged.manifest.permissions.clone();
        self.write_state(&state)?;
        self.list(false).into_iter().find(|l| l.id == *id).ok_or_else(|| Failure::new(code::ADDON_NOT_FOUND, id.clone()))
    }

    pub fn discard(&self, staged: &Staged) {
        let _ = fs::remove_dir_all(&staged.staging);
    }

    /// Turns an add-on on or off. Turning it on accepts what its manifest asks for now (the page
    /// shows those permissions first when they changed).
    pub fn set_enabled(&self, id: &str, enabled: bool) -> Result<()> {
        let manifest = Manifest::read(&self.folder(id)).map_err(|_| Failure::new(code::ADDON_NOT_FOUND, id))?;
        let mut state = self.state();
        let entry = state.entry(id.to_string()).or_default();
        entry.enabled = enabled;
        if enabled {
            entry.granted = manifest.permissions;
        }
        self.write_state(&state)
    }

    /// Removes an add-on, its state and its storage.
    pub fn uninstall(&self, id: &str) -> Result<()> {
        if !manifest::is_valid_id(id) && !self.list(false).iter().any(|l| l.id == id) {
            return Err(Failure::new(code::ADDON_NOT_FOUND, id));
        }
        let folder = self.folder(id);
        if !folder.starts_with(&self.root) || id.contains('/') || id.contains("..") || !folder.is_dir() {
            return Err(Failure::new(code::ADDON_NOT_FOUND, id));
        }
        fs::remove_dir_all(&folder)?;
        let mut state = self.state();
        state.remove(id);
        self.write_state(&state)?;
        self.storage(id).clear();
        Ok(())
    }

    /// Leftovers of installs that were never finished.
    pub fn clean_staging(&self) {
        let _ = fs::remove_dir_all(self.root.join(STAGING));
    }

    pub fn any_enabled(&self) -> bool {
        self.state().values().any(|i| i.enabled)
    }
}

/// A file that exists while the launcher runs: found at start, the last run crashed.
pub struct RunMarker(PathBuf);

impl RunMarker {
    /// Creates the marker; returns it and whether one was left by a run that did not end.
    pub fn create(folder: &Path) -> (Self, bool) {
        let path = folder.join("running");
        let crashed = path.exists();
        let _ = fs::create_dir_all(folder);
        let _ = fs::write(&path, std::process::id().to_string());
        (Self(path), crashed)
    }

    pub fn clear(&self) {
        let _ = fs::remove_file(&self.0);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use package::tests::{zip_of, MANIFEST};

    fn staged_zip(store: &AddonStore, dir: &Path, name: &str, manifest: &str, ticket: &str) -> Staged {
        let zip = dir.join(name);
        zip_of(&zip, &[("addon.json", manifest.as_bytes()), ("index.html", b"<p>v</p>")]);
        store.stage(&zip, ticket).unwrap()
    }

    #[test]
    fn install_update_disable_and_uninstall() {
        let dir = tempfile::tempdir().unwrap();
        let data = dir.path().join("data");
        let store = AddonStore::new(&data);
        assert!(store.list(false).is_empty());

        let staged = staged_zip(&store, dir.path(), "v1.zip", MANIFEST, "t1");
        assert_eq!(staged.replaces, None);
        assert_eq!(staged.new_permissions, ["storage"]);
        let listed = store.install(&staged).unwrap();
        assert!(listed.active && listed.enabled);
        assert!(store.active("org.example.notes", false).is_some());
        assert!(store.active("org.example.notes", true).is_none(), "safe mode loads nothing");
        assert!(!data.join("addons/.staging/t1").exists());
        store.storage("org.example.notes").set("k", serde_json::json!(1)).unwrap();

        // An update that asks for more: the new permission is named, the storage is kept.
        let v2 = MANIFEST.replace("1.0.0", "1.1.0").replace(r#""storage":true"#, r#""storage":true,"hosts":["example.org"]"#);
        let staged = staged_zip(&store, dir.path(), "v2.zip", &v2, "t2");
        assert_eq!(staged.replaces.as_deref(), Some("1.0.0"));
        assert_eq!(staged.new_permissions, ["net:example.org"]);
        store.install(&staged).unwrap();
        assert_eq!(store.active("org.example.notes", false).unwrap().version, "1.1.0");
        assert_eq!(store.storage("org.example.notes").get("k").unwrap(), 1);

        // A declined update changes nothing.
        let v3 = MANIFEST.replace("1.0.0", "2.0.0");
        let staged = staged_zip(&store, dir.path(), "v3.zip", &v3, "t3");
        store.discard(&staged);
        assert_eq!(store.active("org.example.notes", false).unwrap().version, "1.1.0");
        assert!(!data.join("addons/.staging/t3").exists());

        store.set_enabled("org.example.notes", false).unwrap();
        assert!(store.active("org.example.notes", false).is_none());
        assert!(!store.list(false)[0].active);

        // A manifest edited by hand to ask for more is not loaded until accepted again.
        store.set_enabled("org.example.notes", true).unwrap();
        let file = store.folder("org.example.notes").join("addon.json");
        let edited = fs::read_to_string(&file).unwrap().replace(r#""storage":true"#, r#""storage":true,"notify":true"#);
        fs::write(&file, edited).unwrap();
        assert_eq!(store.list(false)[0].ungranted, ["notify"]);
        assert!(store.active("org.example.notes", false).is_none());
        store.set_enabled("org.example.notes", true).unwrap();
        assert!(store.active("org.example.notes", false).is_some());

        store.uninstall("org.example.notes").unwrap();
        assert!(store.list(false).is_empty());
        assert!(!store.storage("org.example.notes").file().exists());
        assert_eq!(store.uninstall("org.example.notes").unwrap_err().code, code::ADDON_NOT_FOUND);
        assert_eq!(store.uninstall("..").unwrap_err().code, code::ADDON_NOT_FOUND);
        assert!(data.exists());
    }

    #[test]
    fn a_folder_whose_manifest_is_broken_is_listed_with_its_problem() {
        let dir = tempfile::tempdir().unwrap();
        let store = AddonStore::new(dir.path());
        let folder = store.folder("org.example.broken");
        fs::create_dir_all(&folder).unwrap();
        fs::write(folder.join("addon.json"), "{").unwrap();
        let listed = store.list(false);
        assert_eq!(listed.len(), 1);
        assert!(!listed[0].active);
        assert_eq!(listed[0].problem.as_ref().unwrap().detail, "addon.json:not-json");

        let renamed = store.folder("org.example.renamed");
        fs::create_dir_all(&renamed).unwrap();
        fs::write(renamed.join("addon.json"), MANIFEST).unwrap();
        fs::write(renamed.join("index.html"), "x").unwrap();
        let listed = store.list(false);
        assert_eq!(listed[1].problem.as_ref().unwrap().detail, "id:folder-mismatch");
    }

    #[test]
    fn the_shipped_add_ons_are_valid_and_carry_the_same_client() {
        let addons = Path::new(env!("CARGO_MANIFEST_DIR")).join("../addons");
        let sdk = fs::read_to_string(addons.join("sdk/dwe-addons.js")).unwrap();
        for name in ["example-notes", "test-fetch"] {
            let folder = addons.join(name);
            let manifest = Manifest::read(&folder).unwrap_or_else(|f| panic!("{name}: {f}"));
            assert!(manifest.id.ends_with(name), "{name}");
            assert_eq!(fs::read_to_string(folder.join("dwe-addons.js")).unwrap(), sdk, "{name} has an old copy of dwe-addons.js");
            let dir = tempfile::tempdir().unwrap();
            assert!(AddonStore::new(dir.path()).stage(&folder, "t").is_ok(), "{name} installs");
        }
    }

    #[test]
    fn the_run_marker_tells_a_crash() {
        let dir = tempfile::tempdir().unwrap();
        let (marker, crashed) = RunMarker::create(dir.path());
        assert!(!crashed);
        let (_again, crashed) = RunMarker::create(dir.path());
        assert!(crashed, "the first run never cleared its marker");
        marker.clear();
        let (_, crashed) = RunMarker::create(dir.path());
        assert!(!crashed);
    }
}
