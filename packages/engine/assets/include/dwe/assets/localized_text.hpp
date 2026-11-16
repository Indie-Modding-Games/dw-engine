#pragma once

#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <functional>
#include <map>
#include <string>
#include <string_view>

// A text for people in a manifest (the name and description of pack.yaml and ruleset.yaml): one
// text for every language, or one per language (docs/architecture/localization.md).
namespace dwe::assets {

struct LocalizedText {
  // The text by language code ("en", "es"). One text for every language is kept as English, the
  // language every missing text falls back to (ReferenceLanguage).
  std::map<std::string, std::string, std::less<>> byLanguage;

  // One text for every language.
  [[nodiscard]] static LocalizedText of(std::string text);

  // The text in a language, else in English; empty when there is none.
  [[nodiscard]] std::string_view in(std::string_view language) const;

  // The English text: what logs and the developer tools show.
  [[nodiscard]] std::string_view english() const;

  [[nodiscard]] bool empty() const { return byLanguage.empty(); }

  friend bool operator==(const LocalizedText&, const LocalizedText&) = default;
};

// Whether a language code may key a localized text: 2 or 3 lowercase letters, optionally followed
// by _ and 2 to 8 lowercase letters or digits ("en", "es", "pt_br").
[[nodiscard]] bool isLanguageCode(std::string_view code);

// Reads a localized text: a single value, or a mapping of language codes to single values that has
// English (en) among them. Problems go to the reader.
LocalizedText readLocalizedText(YamlReader& reader, const core::YamlNode& node);

} // namespace dwe::assets
