//! The main screen's links (bottom right): the repository, the community and the web page. They
//! are built in from DWE_LAUNCHER_LINKS, a JSON object such as
//! `{"repo": "https://...", "community": "https://...", "web": "https://..."}`. A kind that is
//! missing, empty or not an https URL with a host is left out, so without the variable (the
//! default) the launcher shows no links at all.
use serde::Serialize;

/// The kinds of link, in the order they are shown.
pub const KINDS: [&str; 3] = ["repo", "community", "web"];

#[derive(Serialize, Clone, Debug, PartialEq, Eq)]
pub struct Link {
    pub kind: &'static str,
    pub url: String,
}

/// The links this build was made with.
pub fn built_in() -> Vec<Link> {
    parse(option_env!("DWE_LAUNCHER_LINKS").unwrap_or(""))
}

pub fn parse(text: &str) -> Vec<Link> {
    let Ok(serde_json::Value::Object(map)) = serde_json::from_str::<serde_json::Value>(text) else {
        return Vec::new();
    };
    KINDS
        .iter()
        .filter_map(|kind| {
            let url = tauri::Url::parse(map.get(*kind)?.as_str()?.trim()).ok()?;
            (url.scheme() == "https" && url.host_str().is_some_and(|h| !h.is_empty()))
                .then(|| Link { kind, url: url.to_string() })
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn no_variable_or_bad_text_means_no_links() {
        assert!(parse("").is_empty());
        assert!(parse("not json").is_empty());
        assert!(parse("[\"https://example.org\"]").is_empty());
        assert!(parse("{}").is_empty());
    }

    #[test]
    fn only_https_links_of_known_kinds_are_kept_in_order() {
        let links = parse(
            r#"{"web": "https://example.org/", "repo": " https://code.example.org/x ", "community": "http://forum.example.org",
                "other": "https://other.example.org", "extra": 3}"#,
        );
        assert_eq!(
            links,
            vec![
                Link { kind: "repo", url: "https://code.example.org/x".into() },
                Link { kind: "web", url: "https://example.org/".into() },
            ]
        );
        assert!(parse(r#"{"repo": "javascript:alert(1)", "web": "https://", "community": ""}"#).is_empty());
    }
}
