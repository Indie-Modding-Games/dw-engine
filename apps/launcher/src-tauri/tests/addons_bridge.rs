//! What an add-on can reach through the bridge, end to end on the backend: none of the
//! launcher's commands, never the GitHub token (not even when one is in use for the release
//! server), no cookies, no host it did not declare (not even through a redirect), no path or
//! secret in its context, nothing at all when it is off or in safe mode.

use serde_json::{json, Value};
use std::fs::{self, File};
use std::io::{BufRead, BufReader, Write};
use std::net::TcpListener;
use std::path::Path;
use std::sync::{Arc, Mutex};

use dwe_launcher::addons::bridge::{self, Facts};
use dwe_launcher::addons::AddonStore;
use dwe_launcher::failure::code;
use dwe_launcher::releases::Client;

const TOKEN: &str = "ghp_secretsecretsecretsecret";

type Seen = Arc<Mutex<Vec<String>>>;

/// A server that records every request (line and headers) and answers /hello with a cookie,
/// /redirect with a redirect to `redirect_to`, anything else with 404.
fn serve(seen: Seen, redirect_to: Option<String>) -> String {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let base = format!("http://{}", listener.local_addr().unwrap());
    std::thread::spawn(move || {
        for stream in listener.incoming().flatten() {
            let mut reader = BufReader::new(stream.try_clone().unwrap());
            let mut request = String::new();
            loop {
                let mut line = String::new();
                if reader.read_line(&mut line).unwrap_or(0) == 0 || line == "\r\n" {
                    break;
                }
                request.push_str(&line);
            }
            let path = request.split(' ').nth(1).unwrap_or("/").to_string();
            seen.lock().unwrap().push(request);
            let mut stream = stream;
            let _ = match (path.as_str(), &redirect_to) {
                ("/hello", _) => write!(
                    stream,
                    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nSet-Cookie: session=abc\r\nContent-Length: 2\r\nConnection: close\r\n\r\nhi"
                ),
                ("/redirect", Some(to)) => {
                    write!(stream, "HTTP/1.1 302 Found\r\nLocation: {to}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                }
                _ => write!(stream, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"),
            };
        }
    });
    base
}

fn install(data: &Path, permissions: Value) -> String {
    let source = data.join("source");
    fs::create_dir_all(&source).unwrap();
    let manifest = json!({
        "id": "org.example.probe", "name": "Probe", "version": "1.0.0", "author": "Tests", "api": 1,
        "entry": "index.html", "slots": ["tabs"], "permissions": permissions
    });
    fs::write(source.join("addon.json"), manifest.to_string()).unwrap();
    File::create(source.join("index.html")).unwrap();
    let store = AddonStore::new(data);
    let staged = store.stage(&source, "t").unwrap();
    store.install(&staged).unwrap();
    "org.example.probe".to_string()
}

fn facts() -> Facts {
    Facts { language: "es".into(), game_version: Some("0.1.0".into()), content_installed: true, platform: "linux-x86_64".into() }
}

async fn call(data: &Path, id: &str, method: &str, params: Value) -> Result<Value, dwe_launcher::failure::Failure> {
    let call = bridge::parse(method, &params)?;
    let active = AddonStore::new(data).active(id, false);
    bridge::run(data, id, active, call, facts()).await
}

#[test]
fn no_launcher_command_can_be_named() {
    // Every command of the launcher (lib.rs), the plugins' and a few tricks.
    let privileged = [
        "status", "background", "check_updates", "install_disc", "reextract", "update_game", "rollback_game", "cancel",
        "play", "set_language", "set_check_updates", "set_data_folder", "save_token", "clear_token", "open_logs",
        "install_launcher_update", "restart_launcher", "set_auto_update", "addons_list", "addon_inspect", "addon_install", "addon_set_enabled",
        "addon_uninstall", "addons_safe_mode", "addons_dismiss_crash", "addon_call", "plugin:dialog|open",
        "plugin:opener|open_path", "plugin:opener|open_url", "plugin:updater|check", "plugin:updater|download_and_install",
        "token", "storage", "storage.get.token", "net", "net.fetch ", "Context", "__proto__", "constructor", "",
    ];
    for method in privileged {
        let failure = bridge::parse(method, &json!({ "key": "a", "url": "https://example.org", "path": "/" })).unwrap_err();
        assert_eq!(failure.code, code::ADDON_DENIED, "{method:?} must be refused");
    }
}

#[tokio::test]
async fn the_context_holds_no_path_and_no_secret() {
    let dir = tempfile::tempdir().unwrap();
    let data = dir.path().join("secret-data-folder");
    let id = install(&data, json!({ "storage": true }));
    let context = call(&data, &id, "context", Value::Null).await.unwrap();
    let text = context.to_string();
    assert_eq!(context["addon"]["id"], id);
    assert_eq!(context["language"], "es");
    assert_eq!(context["game_version"], "0.1.0");
    for secret in ["secret-data-folder", "/", "\\\\", "token", "api.github.com", "disc", ".cue"] {
        assert!(!text.contains(secret), "the context must not hold {secret:?}: {text}");
    }
}

#[tokio::test]
async fn fetch_never_carries_the_token_or_cookies_and_stays_on_declared_hosts() {
    let seen: Seen = Arc::default();
    let elsewhere_seen: Seen = Arc::default();
    let elsewhere = serve(elsewhere_seen.clone(), None);
    let base = serve(seen.clone(), Some(format!("{elsewhere}/hello")));
    let host = base.trim_start_matches("http://").to_string();

    // The launcher's own client sends the token to the release server...
    let releases = Client::new(&base, "team/game", Some(TOKEN.into())).unwrap();
    assert!(releases.auth_headers().iter().any(|(_, v)| v.contains(TOKEN)));

    // ...the add-on's fetch to the same server never does.
    let dir = tempfile::tempdir().unwrap();
    let id = install(dir.path(), json!({ "hosts": [host] }));
    for _ in 0..2 {
        let response = call(dir.path(), &id, "net.fetch", json!({ "url": format!("{base}/hello") })).await.unwrap();
        assert_eq!(response["status"], 200);
        assert_eq!(response["body"], "hi");
    }
    let requests = seen.lock().unwrap().clone();
    assert_eq!(requests.len(), 2);
    for request in &requests {
        let lower = request.to_ascii_lowercase();
        assert!(!lower.contains("authorization"), "{request}");
        assert!(!lower.contains("cookie"), "the cookie of the first answer is not sent back: {request}");
        assert!(!request.contains(TOKEN));
    }

    // A header that could carry credentials is refused, not sent.
    let failure = call(
        dir.path(),
        &id,
        "net.fetch",
        json!({ "url": format!("{base}/hello"), "headers": [["Authorization", format!("Bearer {TOKEN}")]] }),
    )
    .await
    .unwrap_err();
    assert_eq!(failure.code, code::ADDON_DENIED);
    assert_eq!(seen.lock().unwrap().len(), 2);

    // Another host, the release API, or a redirect to an undeclared host: refused.
    for url in [format!("{elsewhere}/hello"), "https://api.github.com/repos/team/game/releases".into(), "ipc://localhost/status".into()] {
        let failure = call(dir.path(), &id, "net.fetch", json!({ "url": url })).await.unwrap_err();
        assert_eq!(failure.code, code::ADDON_HOST, "{url}");
    }
    let failure = call(dir.path(), &id, "net.fetch", json!({ "url": format!("{base}/redirect") })).await.unwrap_err();
    assert_eq!(failure.code, code::NETWORK);
    assert!(elsewhere_seen.lock().unwrap().is_empty(), "the redirect was not followed");
}

#[tokio::test]
async fn permissions_are_enforced_and_off_add_ons_get_nothing() {
    let dir = tempfile::tempdir().unwrap();
    let id = install(dir.path(), json!({}));
    for (method, params) in [
        ("storage.set", json!({ "key": "a", "value": 1 })),
        ("storage.get", json!({ "key": "a" })),
        ("storage.keys", Value::Null),
        ("net.fetch", json!({ "url": "https://example.org/" })),
    ] {
        let failure = call(dir.path(), &id, method, params).await.unwrap_err();
        assert_eq!(failure.code, code::ADDON_DENIED, "{method} without its permission");
    }

    let store = AddonStore::new(dir.path());
    store.set_enabled(&id, false).unwrap();
    let failure = call(dir.path(), &id, "context", Value::Null).await.unwrap_err();
    assert_eq!(failure.code, code::ADDON_NOT_FOUND);
    store.set_enabled(&id, true).unwrap();
    assert!(store.active(&id, true).is_none(), "safe mode");
    assert!(call(dir.path(), "org.example.missing", "context", Value::Null).await.is_err());
}

#[tokio::test]
async fn storage_is_per_add_on_and_capped() {
    let dir = tempfile::tempdir().unwrap();
    let id = install(dir.path(), json!({ "storage": true }));
    call(dir.path(), &id, "storage.set", json!({ "key": "note", "value": { "text": "hola" } })).await.unwrap();
    assert_eq!(call(dir.path(), &id, "storage.get", json!({ "key": "note" })).await.unwrap()["text"], "hola");
    assert_eq!(call(dir.path(), &id, "storage.keys", Value::Null).await.unwrap(), json!(["note"]));
    let big = "x".repeat(300 * 1024);
    let failure = call(dir.path(), &id, "storage.set", json!({ "key": "big", "value": big })).await.unwrap_err();
    assert_eq!(failure.code, code::ADDON_STORAGE_FULL);
    // The storage lives outside the add-on's folder, under the add-on's own name only.
    assert!(dir.path().join("addon-storage/org.example.probe.json").is_file());
    let failure = call(dir.path(), &id, "storage.get", json!({ "key": "../../settings" })).await.unwrap();
    assert_eq!(failure, Value::Null, "a key is a key, never a path");
}
