//! The update path against a local fake release server that answers like the beta gateway (the
//! GitHub API's release requests, behind the beta key): listing the releases, reading the
//! manifest, downloading the package and checking it, with and without the key, with a tampered
//! file and a foreign signature; a download that redirects to another host without the key; and
//! the whole path through the real gateway (services/beta-gateway under node) in front of
//! tools/fake_release_server.py as GitHub.

use base64::Engine;
use std::collections::HashMap;
use std::fs::{self, File};
use std::io::{BufRead, BufReader, Cursor, Write};
use std::net::TcpListener;
use std::path::{Path, PathBuf};
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};

use dwe_launcher::failure::code;
use dwe_launcher::manifest::platform_key;
use dwe_launcher::releases::{Client, UpdateChannel};
use dwe_launcher::store::GameStore;
use dwe_launcher::{updates, verify};

const KEY: &str = "placeholder-beta-key-for-tests-0123456789";
const REPO: &str = "team/game";
const TESTING: UpdateChannel = UpdateChannel::Testing;
const STABLE: UpdateChannel = UpdateChannel::Stable;

type Routes = Arc<Mutex<HashMap<String, Vec<u8>>>>;

/// A route whose body starts with this answers a 302 to the rest of the body.
const REDIRECT: &[u8] = b"redirect:";

/// Serves the routes to requests that carry the key, and 404 to the others. Returns the base URL.
fn serve(routes: Routes) -> String {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let base = format!("http://{}", listener.local_addr().unwrap());
    std::thread::spawn(move || {
        for stream in listener.incoming().flatten() {
            let routes = routes.clone();
            std::thread::spawn(move || {
                let mut reader = BufReader::new(stream.try_clone().unwrap());
                let mut request = String::new();
                reader.read_line(&mut request).unwrap();
                let path = request.split(' ').nth(1).unwrap_or("/").to_string();
                let mut authorized = false;
                loop {
                    let mut line = String::new();
                    if reader.read_line(&mut line).unwrap() == 0 || line == "\r\n" {
                        break;
                    }
                    if line.to_ascii_lowercase().starts_with("authorization:") && line.contains(&format!("Bearer {KEY}")) {
                        authorized = true;
                    }
                }
                let body = routes.lock().unwrap().get(&path).cloned().filter(|_| authorized);
                let mut stream = stream;
                match body {
                    Some(body) if body.starts_with(REDIRECT) => {
                        let location = String::from_utf8_lossy(&body[REDIRECT.len()..]).to_string();
                        write!(stream, "HTTP/1.1 302 Found\r\nLocation: {location}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n").unwrap();
                    }
                    Some(body) => {
                        write!(stream, "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", body.len()).unwrap();
                        stream.write_all(&body).unwrap();
                    }
                    None => write!(stream, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n").unwrap(),
                }
            });
        }
    });
    base
}

fn package(dir: &Path, name: &str, marker: &str) -> PathBuf {
    let archive = dir.join(format!("{name}.tar.xz"));
    let mut builder = tar::Builder::new(xz2::write::XzEncoder::new(File::create(&archive).unwrap(), 1));
    let exe = std::env::consts::EXE_SUFFIX;
    for (path, body) in [(format!("bin/dwe-game{exe}"), marker), (format!("bin/dwx{exe}"), "dwx"), ("EXTRACTOR_VERSION".into(), "13\n")] {
        let mut header = tar::Header::new_gnu();
        header.set_size(body.len() as u64);
        header.set_mode(0o755);
        header.set_cksum();
        builder.append_data(&mut header, format!("{name}/{path}"), body.as_bytes()).unwrap();
    }
    builder.into_inner().unwrap().finish().unwrap();
    archive
}

struct Signer(minisign::KeyPair);

impl Signer {
    fn new() -> Self {
        Signer(minisign::KeyPair::generate_unencrypted_keypair().unwrap())
    }
    fn public(&self) -> String {
        base64::engine::general_purpose::STANDARD.encode(self.0.pk.to_box().unwrap().to_string())
    }
    fn sign(&self, data: &[u8]) -> String {
        let signature = minisign::sign(Some(&self.0.pk), &self.0.sk, Cursor::new(data), None, None).unwrap();
        base64::engine::general_purpose::STANDARD.encode(signature.to_string())
    }
}

fn sha256(data: &[u8]) -> String {
    use sha2::Digest;
    sha2::Sha256::digest(data).iter().map(|b| format!("{b:02x}")).collect()
}

/// Publishes a release with the package bytes as served (served may differ from signed, to fake
/// a tampered download).
fn publish(routes: &Routes, base: &str, version: &str, signed: &[u8], served: &[u8], signer: &Signer) {
    let asset = format!("dwe-game-{version}-{}.tar.xz", platform_key());
    let manifest = serde_json::json!({
        "schema": 1, "version": version, "commit": "abc", "extractor_version": 13,
        "packages": { platform_key(): { "asset": asset, "size": served.len(), "sha256": sha256(signed), "signature": signer.sign(signed) } }
    });
    let api = format!("/repos/{REPO}/releases");
    let releases = serde_json::json!([
        { "tag_name": "draft", "draft": true, "assets": [] },
        { "tag_name": format!("v{version}"), "name": format!("Beta {version}"), "body": "## Changes\n- Faster", "prerelease": true,
          "assets": [
            { "id": 1, "name": "game-manifest.json", "size": 1, "url": format!("{base}{api}/assets/1") },
            { "id": 2, "name": asset, "size": served.len(), "url": format!("{base}{api}/assets/2") } ] }
    ]);
    let mut routes = routes.lock().unwrap();
    routes.insert(format!("{api}?per_page=20"), releases.to_string().into_bytes());
    routes.insert(format!("{api}/assets/1"), manifest.to_string().into_bytes());
    routes.insert(format!("{api}/assets/2"), served.to_vec());
}

#[tokio::test]
async fn updates_from_a_private_release_server() {
    let dir = tempfile::tempdir().unwrap();
    let data = dir.path().join("data");
    let routes: Routes = Arc::default();
    let base = serve(routes.clone());
    let signer = Signer::new();
    let key = verify::public_key(&signer.public()).unwrap();
    let never = AtomicBool::new(false);

    let first = fs::read(package(dir.path(), "dwe-a", "first")).unwrap();
    publish(&routes, &base, "0.1.0-beta.1", &first, &first, &signer);

    // Without the key the releases are not shown.
    let anonymous = Client::new(&base, REPO, None).unwrap();
    assert_eq!(anonymous.latest(TESTING).await.unwrap_err().code, code::AUTH);

    let client = Client::new(&base, REPO, Some(KEY.into())).unwrap();
    let release = client.latest(TESTING).await.unwrap();
    assert_eq!(release.tag, "v0.1.0-beta.1");
    assert_eq!(release.notes, "## Changes\n- Faster");
    assert!(updates::is_update(&release, None));

    // No key in the build: nothing is installed.
    let refused = updates::install_game(&client, &release, &data, None, &never, |_, _, _| {}).await;
    assert_eq!(refused.unwrap_err().code, code::NO_SIGNING_KEY);

    let mut stages = Vec::new();
    let game = updates::install_game(&client, &release, &data, Some(&key), &never, |stage, _, _| stages.push(stage)).await.unwrap();
    assert_eq!(fs::read_to_string(game.game_program()).unwrap(), "first");
    assert_eq!(game.extractor_version, Some(13));
    assert!(stages.contains(&updates::Stage::Download) && stages.contains(&updates::Stage::Install));
    assert!(!updates::is_update(&release, Some(&game)));
    assert!(!data.join("downloads").read_dir().unwrap().any(|_| true));

    // A tampered download is refused and the game stays as it was.
    let second = fs::read(package(dir.path(), "dwe-b", "second")).unwrap();
    let mut tampered = second.clone();
    *tampered.last_mut().unwrap() ^= 1;
    publish(&routes, &base, "0.1.0-beta.2", &second, &tampered, &signer);
    let release = client.latest(TESTING).await.unwrap();
    assert!(updates::is_update(&release, Some(&game)));
    let failure = updates::install_game(&client, &release, &data, Some(&key), &never, |_, _, _| {}).await.unwrap_err();
    assert_eq!(failure.code, code::BAD_CHECKSUM);
    assert_eq!(GameStore::new(&data).current().unwrap().version, "0.1.0-beta.1");

    // Signed by someone else: refused too.
    publish(&routes, &base, "0.1.0-beta.2", &second, &second, &Signer::new());
    let release = client.latest(TESTING).await.unwrap();
    let failure = updates::install_game(&client, &release, &data, Some(&key), &never, |_, _, _| {}).await.unwrap_err();
    assert_eq!(failure.code, code::BAD_SIGNATURE);

    // The real update: installed, the old version kept for a rollback.
    publish(&routes, &base, "0.1.0-beta.2", &second, &second, &signer);
    let release = client.latest(TESTING).await.unwrap();
    let game = updates::install_game(&client, &release, &data, Some(&key), &never, |_, _, _| {}).await.unwrap();
    assert_eq!(fs::read_to_string(game.game_program()).unwrap(), "second");
    let store = GameStore::new(&data);
    assert!(store.has_previous());
    assert_eq!(store.rollback().unwrap().version, "0.1.0-beta.1");
}

#[tokio::test]
async fn the_key_never_leaves_the_release_server() {
    let routes: Routes = Arc::default();
    let base = serve(routes.clone());
    let api = format!("/repos/{REPO}/releases");
    let releases = serde_json::json!([{ "tag_name": "v1", "assets": [
        { "id": 1, "name": "game-manifest.json", "size": 1, "url": "http://localhost:9/elsewhere" } ] }]);
    routes.lock().unwrap().insert(format!("{api}?per_page=20"), releases.to_string().into_bytes());
    let client = Client::new(&base, REPO, Some(KEY.into())).unwrap();
    assert_eq!(client.latest(TESTING).await.unwrap_err().code, code::BAD_MANIFEST);
}

/// A storage host like the one GitHub's downloads redirect to: serves its files to anyone, answers
/// 400 to a request that carries an Authorization header (as the real one does), and records
/// every request's headers. Returns its base URL, on "localhost" (another host than 127.0.0.1).
fn serve_storage(files: Routes, seen: Arc<Mutex<Vec<String>>>) -> String {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let base = format!("http://localhost:{}", listener.local_addr().unwrap().port());
    std::thread::spawn(move || {
        for stream in listener.incoming().flatten() {
            let (files, seen) = (files.clone(), seen.clone());
            std::thread::spawn(move || {
                let mut reader = BufReader::new(stream.try_clone().unwrap());
                let mut request = String::new();
                reader.read_line(&mut request).unwrap();
                let path = request.split(' ').nth(1).unwrap_or("/").to_string();
                let mut head = request.clone();
                loop {
                    let mut line = String::new();
                    if reader.read_line(&mut line).unwrap() == 0 || line == "\r\n" {
                        break;
                    }
                    head.push_str(&line);
                }
                seen.lock().unwrap().push(head.clone());
                let mut stream = stream;
                let body = files.lock().unwrap().get(&path).cloned();
                match body {
                    _ if head.to_ascii_lowercase().contains("\nauthorization:") => {
                        write!(stream, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n").unwrap()
                    }
                    Some(body) => {
                        write!(stream, "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", body.len()).unwrap();
                        stream.write_all(&body).unwrap();
                    }
                    None => write!(stream, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n").unwrap(),
                }
            });
        }
    });
    base
}

#[tokio::test]
async fn the_key_is_not_sent_where_a_download_redirects() {
    let dir = tempfile::tempdir().unwrap();
    let data = dir.path().join("data");
    let routes: Routes = Arc::default();
    let base = serve(routes.clone());
    let stored: Routes = Arc::default();
    let seen: Arc<Mutex<Vec<String>>> = Arc::default();
    let storage = serve_storage(stored.clone(), seen.clone());
    let signer = Signer::new();
    let key = verify::public_key(&signer.public()).unwrap();

    // The gateway's answer for the package: a 302 to a short-lived link on the storage host.
    let package_bytes = fs::read(package(dir.path(), "dwe-r", "redirected")).unwrap();
    publish(&routes, &base, "0.3.0", &package_bytes, &package_bytes, &signer);
    stored.lock().unwrap().insert("/signed/package?sig=short".into(), package_bytes.clone());
    let mut redirect = REDIRECT.to_vec();
    redirect.extend_from_slice(format!("{storage}/signed/package?sig=short").as_bytes());
    routes.lock().unwrap().insert(format!("/repos/{REPO}/releases/assets/2"), redirect);

    let client = Client::new(&base, REPO, Some(KEY.into())).unwrap();
    let release = client.latest(TESTING).await.unwrap();
    let never = AtomicBool::new(false);
    let game = updates::install_game(&client, &release, &data, Some(&key), &never, |_, _, _| {}).await.unwrap();
    assert_eq!(fs::read_to_string(game.game_program()).unwrap(), "redirected");
    let requests = seen.lock().unwrap().clone();
    assert_eq!(requests.len(), 1, "the package came from the storage host");
    assert!(!requests[0].to_ascii_lowercase().contains("authorization"), "{}", requests[0]);
    assert!(!requests[0].contains(KEY));
}

/// A program the test starts, stopped when the test ends (even when it fails).
struct Child(std::process::Child);

impl Drop for Child {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

fn free_port() -> u16 {
    TcpListener::bind("127.0.0.1:0").unwrap().local_addr().unwrap().port()
}

fn wait_for(port: u16) {
    for _ in 0..100 {
        if std::net::TcpStream::connect(("127.0.0.1", port)).is_ok() {
            return;
        }
        std::thread::sleep(std::time::Duration::from_millis(100));
    }
    panic!("nothing listens on {port} after 10 s");
}

fn has(program: &str) -> bool {
    std::process::Command::new(program).arg("--version").output().is_ok_and(|o| o.status.success())
}

/// The real path: the launcher's client → the gateway (worker.js under node) → a fake GitHub
/// (tools/fake_release_server.py, which redirects every file to its storage host and refuses
/// a storage request that carries a key). Skipped where node or python3 is missing; the
/// launcher image has both.
#[tokio::test]
async fn through_the_beta_gateway() {
    if !has("node") || !has("python3") {
        eprintln!("skipped: node and python3 are needed to run the gateway and the fake GitHub");
        return;
    }
    let launcher = Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap();
    let fake_github = launcher.join("tools/fake_release_server.py");
    let gateway_program = launcher.join("../../services/beta-gateway/dev/serve.js");
    let dir = tempfile::tempdir().unwrap();
    let data = dir.path().join("data");
    let (github_port, gateway_port) = (free_port(), free_port());
    let github = format!("http://127.0.0.1:{github_port}");
    let gateway = format!("http://127.0.0.1:{gateway_port}");
    const GITHUB_TOKEN: &str = "placeholder-github-token";

    // One release: the game package, its manifest, a launcher bundle and its latest.json, which
    // names the bundle by its GitHub API URL (as the release workflow writes it).
    let signer = Signer::new();
    let key = verify::public_key(&signer.public()).unwrap();
    let folder = dir.path().join("releases/v0.4.0-beta.1");
    fs::create_dir_all(&folder).unwrap();
    let game_package = fs::read(package(dir.path(), "dwe-g", "through the gateway")).unwrap();
    let asset = format!("dwe-game-0.4.0-beta.1-{}.tar.xz", platform_key());
    fs::write(folder.join(&asset), &game_package).unwrap();
    let manifest = serde_json::json!({
        "schema": 1, "version": "0.4.0-beta.1", "commit": "abc", "extractor_version": 13,
        "packages": { platform_key(): { "asset": asset, "size": game_package.len(), "sha256": sha256(&game_package), "signature": signer.sign(&game_package) } }
    });
    fs::write(folder.join("game-manifest.json"), manifest.to_string()).unwrap();
    fs::write(folder.join("dwe-launcher.AppImage"), b"new launcher").unwrap();
    fs::write(folder.join("notes.md"), "## Through the gateway").unwrap();
    // The fake server numbers files 1000 + their place by name: dwe-game, dwe-launcher, game-manifest, latest.
    let launcher_api_url = format!("{github}/repos/{REPO}/releases/assets/1001");
    let latest = serde_json::json!({ "version": "9.0.0", "notes": "", "pub_date": "2026-11-04T12:00:00Z",
        "platforms": { "linux-x86_64": { "signature": "c2ln", "url": launcher_api_url } } });
    fs::write(folder.join("latest.json"), latest.to_string()).unwrap();

    let _github = Child(
        std::process::Command::new("python3")
            .arg(&fake_github)
            .arg(dir.path().join("releases"))
            .args(["--port", &github_port.to_string(), "--token", GITHUB_TOKEN, "--repo", REPO])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .spawn()
            .unwrap(),
    );
    let _gateway = Child(
        std::process::Command::new("node")
            .arg(&gateway_program)
            .args(["--port", &gateway_port.to_string()])
            .env("BETA_KEY", KEY)
            .env("GITHUB_TOKEN", GITHUB_TOKEN)
            .env("GITHUB_REPO", REPO)
            .env("GITHUB_API", &github)
            .stdout(std::process::Stdio::null())
            .spawn()
            .unwrap(),
    );
    wait_for(github_port);
    wait_for(gateway_port);

    // No key or a wrong one: the "needs the key" failure.
    for wrong in [None, Some("not-the-beta-key".to_string()), Some(GITHUB_TOKEN.to_string())] {
        let refused = Client::new(&gateway, REPO, wrong).unwrap().latest(TESTING).await.unwrap_err();
        assert_eq!(refused.code, code::AUTH);
    }

    // The key: the release, every file on the gateway, the game installed through a redirect
    // to the storage host (which refuses any request that carries a key).
    let client = Client::new(&gateway, REPO, Some(KEY.into())).unwrap();
    let release = client.latest(TESTING).await.unwrap();
    assert_eq!(release.tag, "v0.4.0-beta.1");
    assert_eq!(release.notes, "## Through the gateway");
    assert!(release.assets.iter().all(|a| a.url.starts_with(&format!("{gateway}/"))), "{:?}", release.assets);
    let never = AtomicBool::new(false);
    let game = updates::install_game(&client, &release, &data, Some(&key), &never, |_, _, _| {}).await.unwrap();
    assert_eq!(fs::read_to_string(game.game_program()).unwrap(), "through the gateway");

    // latest.json: its bundle now on the gateway, and the bundle downloads through it.
    let text = client.fetch_text(release.launcher_manifest().unwrap()).await.unwrap();
    let served: serde_json::Value = serde_json::from_str(&text).unwrap();
    let bundle_url = served["platforms"]["linux-x86_64"]["url"].as_str().unwrap().to_string();
    assert_eq!(bundle_url, format!("{gateway}/repos/{REPO}/releases/assets/1001"));
    assert!(client.serves(&bundle_url));
    let bundle = dwe_launcher::releases::Asset { id: 1001, name: "dwe-launcher.AppImage".into(), size: 12, url: bundle_url };
    let file = dir.path().join("bundle");
    client.download(&bundle, &file, &never, |_, _| {}).await.unwrap();
    assert_eq!(fs::read(&file).unwrap(), b"new launcher");
    assert!(!text.contains(GITHUB_TOKEN) && !text.contains(&github));

    // Only a test version so far: /releases/latest is 404 through the gateway, and Stable
    // follows the test versions.
    assert_eq!(client.latest(STABLE).await.unwrap().tag, "v0.4.0-beta.1");
    // A stable release comes out: Stable takes it (from the listing, or from /releases/latest
    // while the gateway keeps the listing it read), Testing keeps the newer test version.
    let stable = dir.path().join("releases/v0.3.0");
    fs::create_dir_all(&stable).unwrap();
    let stable_package = fs::read(package(dir.path(), "dwe-s", "stable")).unwrap();
    let stable_asset = format!("dwe-game-0.3.0-{}.tar.xz", platform_key());
    fs::write(stable.join(&stable_asset), &stable_package).unwrap();
    let stable_manifest = serde_json::json!({
        "schema": 1, "version": "0.3.0", "commit": "abc", "extractor_version": 13,
        "packages": { platform_key(): { "asset": stable_asset, "size": stable_package.len(), "sha256": sha256(&stable_package), "signature": signer.sign(&stable_package) } }
    });
    fs::write(stable.join("game-manifest.json"), stable_manifest.to_string()).unwrap();
    let release = client.latest(STABLE).await.unwrap();
    assert_eq!(release.tag, "v0.3.0");
    assert!(!release.prerelease);
    assert!(release.assets.iter().all(|a| a.url.starts_with(&format!("{gateway}/"))), "{:?}", release.assets);
    // The installed test version is newer: Stable offers no update (never a downgrade).
    assert!(!updates::is_update(&release, Some(&game)));
    assert_eq!(client.latest(TESTING).await.unwrap().tag, "v0.4.0-beta.1");
}

#[tokio::test]
async fn each_computer_gets_the_newest_release_made_for_it() {
    let routes: Routes = Arc::default();
    let base = serve(routes.clone());
    let api = format!("/repos/{REPO}/releases");
    let manifest = |version: &str, platforms: &[&str]| {
        let packages: serde_json::Map<String, serde_json::Value> = platforms
            .iter()
            .map(|p| {
                let package = serde_json::json!({ "asset": format!("dwe-game-{version}-{p}.tar.xz"), "size": 1,
                    "sha256": "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08", "signature": "c2ln" });
                (p.to_string(), package)
            })
            .collect();
        serde_json::json!({ "schema": 1, "version": version, "extractor_version": 13, "packages": packages })
    };
    // The newest release has a Linux package only; the one before it has every computer's.
    let releases = serde_json::json!([
        { "tag_name": "v2", "prerelease": true, "assets": [
            { "id": 1, "name": "game-manifest.json", "size": 1, "url": format!("{base}{api}/assets/1") } ] },
        { "tag_name": "v1", "prerelease": true, "assets": [
            { "id": 2, "name": "game-manifest.json", "size": 1, "url": format!("{base}{api}/assets/2") } ] }
    ]);
    {
        let mut routes = routes.lock().unwrap();
        routes.insert(format!("{api}?per_page=20"), releases.to_string().into_bytes());
        routes.insert(format!("{api}/assets/1"), manifest("0.1.0-beta.2", &["linux-x86_64"]).to_string().into_bytes());
        let every = ["linux-x86_64", "windows-x86_64", "darwin-aarch64", "darwin-x86_64"];
        routes.insert(format!("{api}/assets/2"), manifest("0.1.0-beta.1", &every).to_string().into_bytes());
    }
    let client = Client::new(&base, REPO, Some(KEY.into())).unwrap();
    assert_eq!(client.latest_for("linux-x86_64", TESTING).await.unwrap().tag, "v2");
    let windows = client.latest_for("windows-x86_64", TESTING).await.unwrap();
    assert_eq!(windows.tag, "v1");
    assert_eq!(windows.manifest.package("windows-x86_64").unwrap().asset, "dwe-game-0.1.0-beta.1-windows-x86_64.tar.xz");
    assert_eq!(client.latest_for("darwin-aarch64", TESTING).await.unwrap().tag, "v1");
    assert_eq!(client.latest_for("windows-aarch64", TESTING).await.unwrap_err().code, code::NO_RELEASE);
}

/// A release listing entry for the channel tests: its game manifest is at assets/<id>.
fn listed(base: &str, tag: &str, prerelease: bool, id: u64) -> serde_json::Value {
    let api = format!("/repos/{REPO}/releases");
    serde_json::json!({ "tag_name": tag, "prerelease": prerelease, "assets": [
        { "id": id, "name": "game-manifest.json", "size": 1, "url": format!("{base}{api}/assets/{id}") } ] })
}

/// A game manifest with a package for each of the computers.
fn manifest_for(version: &str, platforms: &[&str]) -> Vec<u8> {
    let packages: serde_json::Map<String, serde_json::Value> = platforms
        .iter()
        .map(|p| {
            let package = serde_json::json!({ "asset": format!("dwe-game-{version}-{p}.tar.xz"), "size": 1,
                "sha256": "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08", "signature": "c2ln" });
            (p.to_string(), package)
        })
        .collect();
    serde_json::json!({ "schema": 1, "version": version, "extractor_version": 13, "packages": packages }).to_string().into_bytes()
}

#[tokio::test]
async fn each_channel_gets_its_own_releases() {
    let routes: Routes = Arc::default();
    let base = serve(routes.clone());
    let api = format!("/repos/{REPO}/releases");
    let here = platform_key();
    let here = here.as_str();
    let client = Client::new(&base, REPO, Some(KEY.into())).unwrap();
    let set = |listing: serde_json::Value, manifests: &[(u64, Vec<u8>)], latest: Option<serde_json::Value>| {
        let mut routes = routes.lock().unwrap();
        routes.clear();
        routes.insert(format!("{api}?per_page=20"), listing.to_string().into_bytes());
        for (id, manifest) in manifests {
            routes.insert(format!("{api}/assets/{id}"), manifest.clone());
        }
        if let Some(latest) = latest {
            routes.insert(format!("{api}/latest"), latest.to_string().into_bytes());
        }
    };

    // Test versions newer than the stable ones: Testing takes the newest test version, Stable the
    // newest stable one.
    set(
        serde_json::json!([listed(&base, "v0.3.0-beta.2", true, 1), listed(&base, "v0.2.0", false, 2),
            listed(&base, "v0.2.0-beta.1", true, 3), listed(&base, "v0.1.0", false, 4)]),
        &[(1, manifest_for("0.3.0-beta.2", &[here])), (2, manifest_for("0.2.0", &[here])),
            (3, manifest_for("0.2.0-beta.1", &[here])), (4, manifest_for("0.1.0", &[here]))],
        None,
    );
    let testing = client.latest(TESTING).await.unwrap();
    assert_eq!(testing.tag, "v0.3.0-beta.2");
    assert!(testing.prerelease);
    let stable = client.latest(STABLE).await.unwrap();
    assert_eq!(stable.tag, "v0.2.0");
    assert!(!stable.prerelease);
    // A stable release with no package for a computer: Stable has nothing for it (no test version).
    set(
        serde_json::json!([listed(&base, "v0.3.0-beta.2", true, 1), listed(&base, "v0.2.0", false, 2)]),
        &[(1, manifest_for("0.3.0-beta.2", &["other-computer", here])), (2, manifest_for("0.2.0", &[here]))],
        None,
    );
    assert_eq!(client.latest_for("other-computer", STABLE).await.unwrap_err().code, code::NO_RELEASE);
    assert_eq!(client.latest_for("other-computer", TESTING).await.unwrap().tag, "v0.3.0-beta.2");

    // No stable release yet (none listed, /releases/latest is 404): Stable follows the test versions.
    let only_tests = serde_json::json!([listed(&base, "v0.1.0-beta.2", true, 1), listed(&base, "v0.1.0-beta.1", true, 2)]);
    let test_manifests = [(1, manifest_for("0.1.0-beta.2", &[here])), (2, manifest_for("0.1.0-beta.1", &[here]))];
    set(only_tests.clone(), &test_manifests, None);
    assert_eq!(client.latest(STABLE).await.unwrap().tag, "v0.1.0-beta.2");
    assert_eq!(client.latest(TESTING).await.unwrap().tag, "v0.1.0-beta.2");

    // A stable release beyond the listing (/releases/latest answers it): Stable takes it...
    let mut with_stable = test_manifests.to_vec();
    with_stable.push((9, manifest_for("0.0.9", &[here])));
    set(only_tests.clone(), &with_stable, Some(listed(&base, "v0.0.9", false, 9)));
    assert_eq!(client.latest(STABLE).await.unwrap().tag, "v0.0.9");
    assert_eq!(client.latest(TESTING).await.unwrap().tag, "v0.1.0-beta.2");
    // ...and when it has no package for this computer, there is no release: a stable release
    // exists, so Stable does not fall back to the test versions.
    with_stable.pop();
    with_stable.push((9, manifest_for("0.0.9", &["other-computer"])));
    set(only_tests, &with_stable, Some(listed(&base, "v0.0.9", false, 9)));
    assert_eq!(client.latest(STABLE).await.unwrap_err().code, code::NO_RELEASE);
}
