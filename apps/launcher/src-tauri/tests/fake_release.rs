//! The update path against a local fake release server that answers like the GitHub API for a
//! private repository: listing the releases, reading the manifest, downloading the package and
//! checking it, with and without the token, with a tampered file and a foreign signature.

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
use dwe_launcher::releases::Client;
use dwe_launcher::store::GameStore;
use dwe_launcher::{updates, verify};

const TOKEN: &str = "test-token-123";
const REPO: &str = "team/game";

type Routes = Arc<Mutex<HashMap<String, Vec<u8>>>>;

/// Serves the routes to requests that carry the token, and 404 (as GitHub does for a private
/// repository) to the others. Returns the base URL.
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
                    if line.to_ascii_lowercase().starts_with("authorization:") && line.contains(&format!("Bearer {TOKEN}")) {
                        authorized = true;
                    }
                }
                let body = routes.lock().unwrap().get(&path).cloned().filter(|_| authorized);
                let mut stream = stream;
                match body {
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

    // A private repository: without the token the releases are not found.
    let anonymous = Client::new(&base, REPO, None).unwrap();
    assert_eq!(anonymous.latest().await.unwrap_err().code, code::AUTH);

    let client = Client::new(&base, REPO, Some(TOKEN.into())).unwrap();
    let release = client.latest().await.unwrap();
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
    let release = client.latest().await.unwrap();
    assert!(updates::is_update(&release, Some(&game)));
    let failure = updates::install_game(&client, &release, &data, Some(&key), &never, |_, _, _| {}).await.unwrap_err();
    assert_eq!(failure.code, code::BAD_CHECKSUM);
    assert_eq!(GameStore::new(&data).current().unwrap().version, "0.1.0-beta.1");

    // Signed by someone else: refused too.
    publish(&routes, &base, "0.1.0-beta.2", &second, &second, &Signer::new());
    let release = client.latest().await.unwrap();
    let failure = updates::install_game(&client, &release, &data, Some(&key), &never, |_, _, _| {}).await.unwrap_err();
    assert_eq!(failure.code, code::BAD_SIGNATURE);

    // The real update: installed, the old version kept for a rollback.
    publish(&routes, &base, "0.1.0-beta.2", &second, &second, &signer);
    let release = client.latest().await.unwrap();
    let game = updates::install_game(&client, &release, &data, Some(&key), &never, |_, _, _| {}).await.unwrap();
    assert_eq!(fs::read_to_string(game.game_program()).unwrap(), "second");
    let store = GameStore::new(&data);
    assert!(store.has_previous());
    assert_eq!(store.rollback().unwrap().version, "0.1.0-beta.1");
}

#[tokio::test]
async fn the_token_never_leaves_the_api_server() {
    let routes: Routes = Arc::default();
    let base = serve(routes.clone());
    let api = format!("/repos/{REPO}/releases");
    let releases = serde_json::json!([{ "tag_name": "v1", "assets": [
        { "id": 1, "name": "game-manifest.json", "size": 1, "url": "http://localhost:9/elsewhere" } ] }]);
    routes.lock().unwrap().insert(format!("{api}?per_page=20"), releases.to_string().into_bytes());
    let client = Client::new(&base, REPO, Some(TOKEN.into())).unwrap();
    assert_eq!(client.latest().await.unwrap_err().code, code::BAD_MANIFEST);
}
