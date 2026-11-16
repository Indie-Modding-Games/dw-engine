//! A text for people in a manifest (the name and description of pack.yaml and ruleset.yaml): one
//! text for every language, or one per language with English among them. It mirrors
//! `packages/engine/assets` (localized_text.cpp).

use std::collections::BTreeMap;

use serde::Deserialize;

/// The language every missing text falls back to.
pub const REFERENCE_LANGUAGE: &str = "en";

/// A text by language code ("en", "es"). One text for every language is kept as English.
#[derive(Clone, Debug, Default, PartialEq, Eq, Deserialize)]
#[serde(try_from = "RawText")]
pub struct LocalizedText {
    by_language: BTreeMap<String, String>,
}

#[derive(Deserialize)]
#[serde(untagged)]
enum RawText {
    One(String),
    ByLanguage(BTreeMap<String, String>),
}

impl TryFrom<RawText> for LocalizedText {
    type Error = String;

    fn try_from(raw: RawText) -> Result<Self, String> {
        match raw {
            RawText::One(text) => Ok(Self::of(text)),
            RawText::ByLanguage(by_language) => {
                if let Some(code) = by_language.keys().find(|code| !is_language_code(code)) {
                    return Err(format!("'{code}' is not a language code, such as en or es"));
                }
                if !by_language.contains_key(REFERENCE_LANGUAGE) {
                    return Err(format!(
                        "a text in several languages needs '{REFERENCE_LANGUAGE}', the one the \
                         others fall back to"
                    ));
                }
                Ok(Self { by_language })
            }
        }
    }
}

impl LocalizedText {
    /// One text for every language.
    pub fn of(text: impl Into<String>) -> Self {
        Self {
            by_language: BTreeMap::from([(REFERENCE_LANGUAGE.to_owned(), text.into())]),
        }
    }

    /// The text in a language, else in English; empty when there is none.
    pub fn in_language(&self, language: &str) -> &str {
        self.by_language
            .get(language)
            .map_or_else(|| self.english(), String::as_str)
    }

    /// The English text.
    pub fn english(&self) -> &str {
        self.by_language
            .get(REFERENCE_LANGUAGE)
            .map_or("", String::as_str)
    }

    /// Every text, by language code.
    pub fn by_language(&self) -> &BTreeMap<String, String> {
        &self.by_language
    }
}

/// Whether a language code may key a localized text: 2 or 3 lowercase letters, optionally
/// followed by _ and 2 to 8 lowercase letters or digits ("en", "es", "pt_br").
pub fn is_language_code(code: &str) -> bool {
    let (language, region) = match code.split_once('_') {
        Some((language, region)) => (language, Some(region)),
        None => (code, None),
    };
    let language_ok =
        (2..=3).contains(&language.len()) && language.bytes().all(|b| b.is_ascii_lowercase());
    let region_ok = region.is_none_or(|region| {
        (2..=8).contains(&region.len())
            && region
                .bytes()
                .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit())
    });
    language_ok && region_ok
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(text: &str) -> Result<LocalizedText, serde_yaml_ng::Error> {
        serde_yaml_ng::from_str(text)
    }

    #[test]
    fn one_text_or_one_per_language_with_english() {
        let one = parse("\"Game data\"").unwrap();
        assert_eq!(one.in_language("es"), "Game data");
        let both = parse("en: Game data\nes: Datos del juego\n").unwrap();
        assert_eq!(both.in_language("es"), "Datos del juego");
        assert_eq!(both.in_language("fr"), "Game data");
        assert_eq!(both.english(), "Game data");
        assert!(parse("es: Datos\n").is_err());
        assert!(parse("en: Data\nSpanish: Datos\n").is_err());
        assert!(parse("[Data]").is_err());
    }

    #[test]
    fn language_codes_are_a_language_and_an_optional_region() {
        for code in ["en", "es", "ast", "pt_br", "zh_hans", "es_419"] {
            assert!(is_language_code(code), "{code}");
        }
        for code in [
            "", "e", "english", "EN", "en_", "en_b", "en-us", "pt_br_x", "1a",
        ] {
            assert!(!is_language_code(code), "{code}");
        }
    }
}
