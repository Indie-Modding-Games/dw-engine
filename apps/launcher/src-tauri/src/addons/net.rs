//! net.fetch for add-ons: only to the hosts an add-on declared (and the player accepted), only
//! https (plain http only to this computer), through a client of its own that never carries the
//! GitHub token, cookies or any header the add-on did not set from a short safe list.

use reqwest::redirect::Policy;
use reqwest::Url;
use serde::{Deserialize, Serialize};
use std::time::Duration;

use crate::failure::{code, Failure, Result};

pub const MAX_RESPONSE_BYTES: usize = 1024 * 1024;
pub const MAX_BODY_BYTES: usize = 64 * 1024;
const MAX_REDIRECTS: usize = 3;
/// The request headers an add-on may set.
const ALLOWED_HEADERS: [&str; 3] = ["accept", "content-type", "accept-language"];

#[derive(Debug, Clone, Default, PartialEq, Deserialize)]
#[serde(default)]
pub struct Request {
    pub url: String,
    pub method: Option<String>,
    pub headers: Vec<(String, String)>,
    pub body: Option<String>,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Response {
    pub status: u16,
    pub content_type: Option<String>,
    pub body: String,
}

/// "host" for the scheme's default port, "host:port" otherwise: the form permissions use.
fn host_key(url: &Url) -> Option<String> {
    let host = url.host_str()?.to_ascii_lowercase();
    Some(match url.port() {
        Some(port) => format!("{host}:{port}"),
        None => host,
    })
}

/// The URL, if the add-on may reach it with these declared hosts.
pub fn allowed_url(url: &str, hosts: &[String]) -> Result<Url> {
    let denied = || Failure::new(code::ADDON_HOST, url.chars().take(200).collect::<String>());
    let parsed = Url::parse(url).map_err(|_| denied())?;
    if !parsed.username().is_empty() || parsed.password().is_some() {
        return Err(denied());
    }
    let local = matches!(parsed.host_str(), Some("127.0.0.1" | "localhost" | "[::1]"));
    match parsed.scheme() {
        "https" => {}
        "http" if local => {}
        _ => return Err(denied()),
    }
    let key = host_key(&parsed).ok_or_else(denied)?;
    if !hosts.contains(&key) {
        return Err(denied());
    }
    Ok(parsed)
}

fn client(hosts: Vec<String>) -> Result<reqwest::Client> {
    // Every redirect is checked against the same list.
    let policy = Policy::custom(move |attempt| {
        if attempt.previous().len() >= MAX_REDIRECTS {
            attempt.error("too many redirects")
        } else if allowed_url(attempt.url().as_str(), &hosts).is_ok() {
            attempt.follow()
        } else {
            attempt.error("redirect to an undeclared host")
        }
    });
    reqwest::Client::builder()
        .user_agent(concat!("dwe-launcher-addon/", env!("CARGO_PKG_VERSION")))
        .redirect(policy)
        .connect_timeout(Duration::from_secs(10))
        .timeout(Duration::from_secs(20))
        .build()
        .map_err(|e| Failure::new(code::NETWORK, e.to_string()))
}

/// Checks the request and makes it. Header names outside the safe list are refused, not
/// dropped, so an add-on knows.
pub async fn fetch(request: Request, hosts: &[String]) -> Result<Response> {
    let url = allowed_url(&request.url, hosts)?;
    let method = match request.method.as_deref().unwrap_or("GET").to_ascii_uppercase().as_str() {
        "GET" => reqwest::Method::GET,
        "POST" => reqwest::Method::POST,
        other => return Err(Failure::new(code::ADDON_DENIED, format!("method {other}"))),
    };
    let mut builder = client(hosts.to_vec())?.request(method, url);
    for (name, value) in &request.headers {
        let lower = name.to_ascii_lowercase();
        if !ALLOWED_HEADERS.contains(&lower.as_str()) || value.len() > 256 {
            return Err(Failure::new(code::ADDON_DENIED, format!("header {name}")));
        }
        builder = builder.header(lower, value);
    }
    if let Some(body) = request.body {
        if body.len() > MAX_BODY_BYTES {
            return Err(Failure::new(code::ADDON_DENIED, "body too big"));
        }
        builder = builder.body(body);
    }
    let failure = |e: reqwest::Error| Failure::new(code::NETWORK, e.without_url().to_string());
    let mut response = builder.send().await.map_err(failure)?;
    let status = response.status().as_u16();
    let content_type = response
        .headers()
        .get(reqwest::header::CONTENT_TYPE)
        .and_then(|v| v.to_str().ok())
        .map(str::to_string);
    let mut body = Vec::new();
    while let Some(chunk) = response.chunk().await.map_err(failure)? {
        if body.len() + chunk.len() > MAX_RESPONSE_BYTES {
            return Err(Failure::new(code::ADDON_DENIED, "response too big"));
        }
        body.extend_from_slice(&chunk);
    }
    Ok(Response { status, content_type, body: String::from_utf8_lossy(&body).into_owned() })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_declared_hosts_are_allowed() {
        let hosts = vec!["example.org".to_string(), "127.0.0.1:8765".to_string()];
        assert!(allowed_url("https://example.org/news.json", &hosts).is_ok());
        assert!(allowed_url("https://EXAMPLE.org/x", &hosts).is_ok());
        assert!(allowed_url("http://127.0.0.1:8765/releases", &hosts).is_ok());
        for denied in [
            "http://example.org/x",              // plain http to another computer
            "https://example.org:8443/x",        // another port
            "https://api.example.org/x",         // a subdomain is another host
            "https://example.org.evil.net/x",
            "https://user:pass@example.org/x",
            "https://api.github.com/repos",
            "http://127.0.0.1:9999/",
            "file:///etc/passwd",
            "ipc://localhost/status",
            "tauri://localhost/index.html",
            "not a url",
        ] {
            assert_eq!(allowed_url(denied, &hosts).unwrap_err().code, code::ADDON_HOST, "{denied}");
        }
        assert!(allowed_url("https://example.org/", &[]).is_err());
    }
}
