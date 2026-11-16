//! The editor's language: the codes, and that the texts follow the one chosen. It is a test
//! binary of its own because the language is global.

use mapper_core::text::{self, Language};

#[test]
fn reads_language_codes_and_locales() {
    assert_eq!(Language::parse("en"), Some(Language::English));
    assert_eq!(Language::parse("ES"), Some(Language::Spanish));
    assert_eq!(Language::parse("es_ES.UTF-8"), Some(Language::Spanish));
    assert_eq!(Language::parse("en-US"), Some(Language::English));
    assert_eq!(Language::parse("C"), None);
    assert_eq!(Language::parse("fr"), None);
}

#[test]
fn the_texts_come_out_in_the_chosen_language() {
    assert_eq!(text::language(), Language::English);
    assert_eq!(text::menu_file(), "File");
    assert_eq!(text::jump_count(2), "2 jumps");
    assert_eq!(text::shortcut_list()[0], ("Ctrl+S", "Save (patch)"));

    text::set_language(Language::Spanish);
    assert_eq!(text::menu_file(), "Archivo");
    assert_eq!(text::jump_count(2), "2 saltos");
    assert_eq!(text::shortcut_list()[0], ("Ctrl+S", "Guardar (parche)"));

    text::set_language(Language::English);
    assert_eq!(text::menu_file(), "File");
}
