//! The language the descriptions and labels of the editor come out in. It is chosen once by the
//! program that shows them (the map editor); English is the default.

use std::sync::atomic::{AtomicU8, Ordering};

/// A language of the editor's texts.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Language {
    #[default]
    English,
    Spanish,
}

impl Language {
    /// The language of a code such as `en`, `es` or a locale like `es_ES.UTF-8`.
    pub fn parse(code: &str) -> Option<Self> {
        let letters = code.split(['_', '.', '-', '@']).next()?;
        match letters.to_ascii_lowercase().as_str() {
            "en" => Some(Self::English),
            "es" => Some(Self::Spanish),
            _ => None,
        }
    }

    /// The system's language, from the locale variables; English if it is neither.
    pub fn from_system() -> Self {
        ["LC_ALL", "LC_MESSAGES", "LANG"]
            .iter()
            .filter_map(|name| std::env::var(name).ok())
            .find(|value| !value.is_empty())
            .and_then(|value| Self::parse(&value))
            .unwrap_or_default()
    }
}

static CURRENT: AtomicU8 = AtomicU8::new(Language::English as u8);

/// Chooses the language of the texts from now on.
pub fn set_language(language: Language) {
    CURRENT.store(language as u8, Ordering::Relaxed);
}

/// The language the texts come out in.
pub fn language() -> Language {
    if CURRENT.load(Ordering::Relaxed) == Language::Spanish as u8 {
        Language::Spanish
    } else {
        Language::English
    }
}

/// One of two texts, by language.
pub fn pick(english: &'static str, spanish: &'static str) -> &'static str {
    match language() {
        Language::English => english,
        Language::Spanish => spanish,
    }
}

/// One of two expressions, by language; only the chosen one runs.
macro_rules! say {
    ($english:expr, $spanish:expr) => {
        match $crate::language::language() {
            $crate::language::Language::English => $english,
            $crate::language::Language::Spanish => $spanish,
        }
    };
}
pub(crate) use say;
