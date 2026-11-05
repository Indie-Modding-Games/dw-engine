//! The add-ons' own URL scheme: dweaddon://localhost/<id>/<file> (http://dweaddon.localhost/... on
//! Windows). It serves only files inside an active add-on's folder, with a strict Content Security
//! Policy: scripts, styles and images from that add-on's own folder, no network, no frames, no
//! forms. The launcher's page shows each add-on in an iframe with sandbox="allow-scripts" only.

use std::path::{Path, PathBuf};

use super::manifest::{is_safe_relative, is_valid_id};

pub const SCHEME: &str = "dweaddon";
const MAX_FILE_BYTES: u64 = 16 * 1024 * 1024;

/// Where an add-on's files are served from, as the page and the CSP name them.
pub fn base_url(id: &str) -> String {
    if cfg!(any(windows, target_os = "android")) {
        format!("http://{SCHEME}.localhost/{id}/")
    } else {
        format!("{SCHEME}://localhost/{id}/")
    }
}

/// The policy every response of an add-on carries: only its own folder, nothing else.
pub fn content_security_policy(id: &str) -> String {
    let own = base_url(id);
    format!(
        "default-src 'none'; script-src {own}; style-src {own} 'unsafe-inline'; img-src {own} data: blob:; \
         font-src {own}; media-src {own}; connect-src 'none'; frame-src 'none'; child-src 'none'; \
         worker-src 'none'; object-src 'none'; form-action 'none'; base-uri 'none'"
    )
}

#[derive(Debug, PartialEq)]
pub struct Served {
    pub status: u16,
    pub content_type: &'static str,
    pub body: Vec<u8>,
    /// None for a refused request (nothing to run).
    pub csp: Option<String>,
}

fn refused(status: u16) -> Served {
    Served { status, content_type: "text/plain", body: Vec::new(), csp: None }
}

fn content_type(path: &Path) -> Option<&'static str> {
    let extension = path.extension()?.to_str()?.to_ascii_lowercase();
    Some(match extension.as_str() {
        "html" | "htm" => "text/html; charset=utf-8",
        "js" | "mjs" => "text/javascript; charset=utf-8",
        "css" => "text/css; charset=utf-8",
        "json" => "application/json",
        "svg" => "image/svg+xml",
        "png" => "image/png",
        "jpg" | "jpeg" => "image/jpeg",
        "webp" => "image/webp",
        "gif" => "image/gif",
        "woff2" => "font/woff2",
        "woff" => "font/woff",
        "ttf" => "font/ttf",
        "txt" | "md" => "text/plain; charset=utf-8",
        _ => return None,
    })
}

/// Decodes %XX escapes; None for a malformed one or one that is not UTF-8.
fn percent_decode(text: &str) -> Option<String> {
    let bytes = text.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' {
            let hex = std::str::from_utf8(bytes.get(i + 1..i + 3)?).ok()?;
            out.push(u8::from_str_radix(hex, 16).ok()?);
            i += 3;
        } else {
            out.push(bytes[i]);
            i += 1;
        }
    }
    String::from_utf8(out).ok()
}

/// The add-on id and the file a request path ("/<id>/<file>") names.
pub fn split_path(path: &str) -> Option<(String, String)> {
    let decoded = percent_decode(path)?;
    let rest = decoded.strip_prefix('/')?;
    let (id, file) = rest.split_once('/')?;
    let file = if file.is_empty() { "index.html" } else { file };
    (is_valid_id(id) && is_safe_relative(file)).then(|| (id.to_string(), file.to_string()))
}

/// Answers a request for `path`; `folder_of` gives an active add-on's folder (None when it is
/// not installed, is off or the launcher is in safe mode).
pub fn serve(path: &str, folder_of: impl Fn(&str) -> Option<PathBuf>) -> Served {
    let Some((id, file)) = split_path(path) else { return refused(400) };
    let Some(folder) = folder_of(&id) else { return refused(404) };
    let Some(content_type) = content_type(Path::new(&file)) else { return refused(403) };
    // Links are followed only if they stay inside the folder (the installer refuses them anyway).
    let (Ok(root), Ok(target)) = (folder.canonicalize(), folder.join(&file).canonicalize()) else {
        return refused(404);
    };
    if !target.starts_with(&root) {
        return refused(403);
    }
    match std::fs::metadata(&target) {
        Ok(meta) if meta.is_file() && meta.len() <= MAX_FILE_BYTES => {}
        Ok(_) => return refused(403),
        Err(_) => return refused(404),
    }
    match std::fs::read(&target) {
        Ok(body) => Served { status: 200, content_type, body, csp: Some(content_security_policy(&id)) },
        Err(_) => refused(404),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    #[test]
    fn only_files_of_an_active_add_on_are_served() {
        let dir = tempfile::tempdir().unwrap();
        let notes = dir.path().join("org.example.notes");
        fs::create_dir_all(notes.join("img")).unwrap();
        fs::write(notes.join("index.html"), "<p>notes</p>").unwrap();
        fs::write(notes.join("img/icon.svg"), "<svg/>").unwrap();
        fs::write(notes.join("run.sh"), "echo").unwrap();
        let other = dir.path().join("org.example.other");
        fs::create_dir_all(&other).unwrap();
        fs::write(other.join("secret.json"), "{}").unwrap();
        fs::write(dir.path().join("addons.json"), "{}").unwrap();
        let active = |id: &str| (id == "org.example.notes").then(|| notes.clone());

        let ok = serve("/org.example.notes/index.html", active);
        assert_eq!(ok.status, 200);
        assert_eq!(ok.body, b"<p>notes</p>");
        assert!(ok.csp.unwrap().contains("connect-src 'none'"));
        assert_eq!(serve("/org.example.notes/", active).status, 200);
        assert_eq!(serve("/org.example.notes/img%2Ficon.svg", active).status, 200);
        assert_eq!(serve("/org.example.notes/img/icon.svg", active).content_type, "image/svg+xml");

        assert_eq!(serve("/org.example.notes/../org.example.other/secret.json", active).status, 400);
        assert_eq!(serve("/org.example.notes/%2e%2e/addons.json", active).status, 400);
        assert_eq!(serve("/org.example.notes/..%2Faddons.json", active).status, 400);
        assert_eq!(serve("/org.example.notes//etc/passwd", active).status, 400);
        assert_eq!(serve("/org.example.notes/%zz", active).status, 400);
        assert_eq!(serve("/org.example.other/secret.json", active).status, 404, "an add-on that is off");
        assert_eq!(serve("/org.example.notes/run.sh", active).status, 403, "an unknown file type");
        assert_eq!(serve("/org.example.notes/missing.html", active).status, 404);
        assert_eq!(serve("/../../etc/passwd", active).status, 400);
        assert_eq!(serve("/addons.json", active).status, 400);
    }

    #[cfg(unix)]
    #[test]
    fn links_out_of_the_folder_are_not_followed() {
        let dir = tempfile::tempdir().unwrap();
        let notes = dir.path().join("org.example.notes");
        fs::create_dir_all(&notes).unwrap();
        fs::write(dir.path().join("outside.json"), "{}").unwrap();
        std::os::unix::fs::symlink(dir.path().join("outside.json"), notes.join("link.json")).unwrap();
        let active = |_: &str| Some(notes.clone());
        assert_eq!(serve("/org.example.notes/link.json", active).status, 403);
    }

    #[test]
    fn the_policy_names_only_the_add_on_s_folder() {
        let csp = content_security_policy("org.example.notes");
        assert!(csp.contains(&base_url("org.example.notes")));
        assert!(csp.starts_with("default-src 'none'"));
        assert!(!csp.contains("'unsafe-eval'"));
        for directive in ["connect-src 'none'", "frame-src 'none'", "form-action 'none'", "object-src 'none'"] {
            assert!(csp.contains(directive), "{directive}");
        }
    }
}
