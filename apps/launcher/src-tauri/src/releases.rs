//! The game's releases through the closed beta's gateway (services/beta-gateway), which answers
//! the GitHub REST API's release requests for the private repository to whoever sends the beta
//! key (or any server that answers the same requests, such as the tests' fake one): the newest
//! release that carries a game manifest, its notes, and its files. Files are fetched through
//! their URL on the gateway, which redirects a package to a short-lived download link on another
//! host; the key is only ever sent to the gateway (reqwest drops the Authorization header when a
//! request redirects to another host).

use futures_util::StreamExt;
use reqwest::header::{HeaderMap, HeaderValue, ACCEPT, AUTHORIZATION, USER_AGENT};
use reqwest::StatusCode;
use serde::{Deserialize, Serialize};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use tokio::io::AsyncWriteExt;

use crate::failure::{code, Failure, Result};
use crate::manifest::{self, GameManifest, LAUNCHER_MANIFEST_ASSET, MANIFEST_ASSET};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Asset {
    pub id: u64,
    pub name: String,
    pub size: u64,
    /// The asset's URL on the release server (answers with the file when asked for
    /// application/octet-stream).
    pub url: String,
}

#[derive(Debug, Clone, Deserialize)]
struct ApiRelease {
    tag_name: String,
    #[serde(default)]
    name: Option<String>,
    #[serde(default)]
    body: Option<String>,
    #[serde(default)]
    draft: bool,
    #[serde(default)]
    prerelease: bool,
    #[serde(default)]
    published_at: Option<String>,
    #[serde(default)]
    assets: Vec<Asset>,
}

#[derive(Debug, Clone, Serialize)]
pub struct Release {
    pub tag: String,
    pub name: String,
    pub notes: String,
    pub prerelease: bool,
    pub published_at: Option<String>,
    pub manifest: GameManifest,
    pub assets: Vec<Asset>,
}

impl Release {
    pub fn asset(&self, name: &str) -> Option<&Asset> {
        self.assets.iter().find(|a| a.name == name)
    }
    pub fn launcher_manifest(&self) -> Option<&Asset> {
        self.asset(LAUNCHER_MANIFEST_ASSET)
    }
}

pub struct Client {
    http: reqwest::Client,
    api: String,
    repository: String,
    /// The beta key, sent as a bearer to the release server only.
    key: Option<String>,
}

fn status_failure(status: StatusCode, what: &str) -> Failure {
    match status {
        StatusCode::UNAUTHORIZED | StatusCode::FORBIDDEN | StatusCode::NOT_FOUND => {
            Failure::new(code::AUTH, format!("{what}: HTTP {}", status.as_u16()))
        }
        _ => Failure::new(code::NETWORK, format!("{what}: HTTP {}", status.as_u16())),
    }
}

fn network(error: reqwest::Error) -> Failure {
    Failure::new(code::NETWORK, crate::log::redact(&error.without_url().to_string()))
}

/// Only https, or plain http to this computer (the tests' fake server): the key and the
/// packages never travel in the clear.
pub fn is_trusted_transport(api: &str) -> bool {
    let Ok(url) = reqwest::Url::parse(api) else { return false };
    match url.scheme() {
        "https" => true,
        "http" => matches!(url.host_str(), Some("127.0.0.1" | "localhost" | "[::1]")),
        _ => false,
    }
}

impl Client {
    pub fn new(api: &str, repository: &str, key: Option<String>) -> Result<Self> {
        if !is_trusted_transport(api) {
            return Err(Failure::new(code::NETWORK, format!("{api} is not https")));
        }
        let mut headers = HeaderMap::new();
        headers.insert(USER_AGENT, HeaderValue::from_static(concat!("dwe-launcher/", env!("CARGO_PKG_VERSION"))));
        headers.insert("X-GitHub-Api-Version", HeaderValue::from_static("2022-11-28"));
        let http = reqwest::Client::builder()
            .default_headers(headers)
            .connect_timeout(std::time::Duration::from_secs(15))
            .build()
            .map_err(network)?;
        Ok(Self { http, api: api.trim_end_matches('/').to_string(), repository: repository.to_string(), key: key.filter(|k| !k.is_empty()) })
    }

    /// The headers a request needs: the key (as a bearer) when there is one.
    pub fn auth_headers(&self) -> Vec<(String, String)> {
        self.key.iter().map(|k| (AUTHORIZATION.to_string(), format!("Bearer {k}"))).collect()
    }

    /// Whether the URL is on the release server: the only place the key may be sent to.
    pub fn serves(&self, url: &str) -> bool {
        url.starts_with(&format!("{}/", self.api))
    }

    fn get(&self, url: &str, accept: &'static str) -> reqwest::RequestBuilder {
        let mut request = self.http.get(url).header(ACCEPT, accept);
        if let Some(key) = &self.key {
            request = request.bearer_auth(key);
        }
        request
    }

    /// The newest release (prereleases included: the beta is one) whose game manifest has a
    /// package for this computer, with that manifest read.
    pub async fn latest(&self) -> Result<Release> {
        self.latest_for(&manifest::platform_key()).await
    }

    /// The newest release whose game manifest has a package for the computer `platform`
    /// (manifest::platform_key). A release made for other computers only is skipped, so an
    /// update for one system never hides the last one for another.
    pub async fn latest_for(&self, platform: &str) -> Result<Release> {
        let url = format!("{}/repos/{}/releases?per_page=20", self.api, self.repository);
        let response = self.get(&url, "application/vnd.github+json").send().await.map_err(network)?;
        if !response.status().is_success() {
            return Err(status_failure(response.status(), "releases"));
        }
        let releases: Vec<ApiRelease> = response.json().await.map_err(network)?;
        for release in releases.into_iter().filter(|r| !r.draft) {
            let Some(manifest_asset) = release.assets.iter().find(|a| a.name == MANIFEST_ASSET) else {
                continue;
            };
            let manifest = manifest::parse(&self.fetch_text(manifest_asset).await?)?;
            if manifest.package(platform).is_none() {
                continue;
            }
            return Ok(Release {
                name: release.name.filter(|n| !n.is_empty()).unwrap_or_else(|| release.tag_name.clone()),
                tag: release.tag_name,
                notes: release.body.unwrap_or_default(),
                prerelease: release.prerelease,
                published_at: release.published_at,
                manifest,
                assets: release.assets,
            });
        }
        Err(Failure::new(code::NO_RELEASE, platform.to_string()))
    }

    /// The key goes only to the release server: a file whose URL points elsewhere is refused.
    fn check_asset_url(&self, asset: &Asset) -> Result<()> {
        if self.serves(&asset.url) {
            Ok(())
        } else {
            Err(Failure::new(code::BAD_MANIFEST, format!("{} is not on {}", asset.name, self.api)))
        }
    }

    pub async fn fetch_text(&self, asset: &Asset) -> Result<String> {
        self.check_asset_url(asset)?;
        let response = self.get(&asset.url, "application/octet-stream").send().await.map_err(network)?;
        if !response.status().is_success() {
            return Err(status_failure(response.status(), &asset.name));
        }
        response.text().await.map_err(network)
    }

    /// Downloads a file to dest (through dest.part, renamed at the end), reporting the bytes done
    /// and the total.
    pub async fn download(
        &self,
        asset: &Asset,
        dest: &Path,
        cancel: &AtomicBool,
        mut progress: impl FnMut(u64, u64),
    ) -> Result<()> {
        self.check_asset_url(asset)?;
        let response = self.get(&asset.url, "application/octet-stream").send().await.map_err(network)?;
        if !response.status().is_success() {
            return Err(status_failure(response.status(), &asset.name));
        }
        let total = response.content_length().unwrap_or(asset.size);
        let partial = dest.with_extension("part");
        if let Some(parent) = dest.parent() {
            tokio::fs::create_dir_all(parent).await?;
        }
        let mut file = tokio::fs::File::create(&partial).await?;
        let mut done = 0u64;
        let mut stream = response.bytes_stream();
        while let Some(chunk) = stream.next().await {
            if cancel.load(Ordering::SeqCst) {
                drop(file);
                let _ = tokio::fs::remove_file(&partial).await;
                return Err(Failure::new(code::CANCELLED, ""));
            }
            let chunk = chunk.map_err(network)?;
            file.write_all(&chunk).await?;
            done += chunk.len() as u64;
            progress(done, total);
        }
        file.flush().await?;
        file.sync_all().await?;
        drop(file);
        tokio::fs::rename(&partial, dest).await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_https_or_this_computer() {
        assert!(is_trusted_transport("https://api.github.com"));
        assert!(is_trusted_transport("http://127.0.0.1:8765"));
        assert!(is_trusted_transport("http://localhost:1"));
        assert!(!is_trusted_transport("http://example.com"));
        assert!(!is_trusted_transport("ftp://127.0.0.1"));
        assert!(Client::new("http://example.com", "o/r", None).is_err());
    }
}
