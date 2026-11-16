#include <dwe/assets/localized_text.hpp>
#include <dwe/assets/text_table.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace dwe::assets {

namespace {

constexpr std::size_t MinLanguageLetters = 2;
constexpr std::size_t MaxLanguageLetters = 3;
constexpr std::size_t MinRegionLength = 2;
constexpr std::size_t MaxRegionLength = 8;
constexpr char RegionSeparator = '_';

bool isLower(char c) {
  return c >= 'a' && c <= 'z';
}

bool isLowerOrDigit(char c) {
  return isLower(c) || (c >= '0' && c <= '9');
}

} // namespace

LocalizedText LocalizedText::of(std::string text) {
  LocalizedText localized;
  localized.byLanguage.emplace(ReferenceLanguage, std::move(text));
  return localized;
}

std::string_view LocalizedText::in(std::string_view language) const {
  if (const auto found = byLanguage.find(language); found != byLanguage.end()) {
    return found->second;
  }
  return english();
}

std::string_view LocalizedText::english() const {
  const auto found = byLanguage.find(ReferenceLanguage);
  return found == byLanguage.end() ? std::string_view{} : std::string_view{found->second};
}

bool isLanguageCode(std::string_view code) {
  const std::string_view language = code.substr(0, code.find(RegionSeparator));
  if (language.size() < MinLanguageLetters || language.size() > MaxLanguageLetters ||
      !std::ranges::all_of(language, isLower)) {
    return false;
  }
  if (language.size() == code.size()) {
    return true;
  }
  const std::string_view region = code.substr(language.size() + 1);
  return region.size() >= MinRegionLength && region.size() <= MaxRegionLength &&
         std::ranges::all_of(region, isLowerOrDigit);
}

LocalizedText readLocalizedText(YamlReader& reader, const core::YamlNode& node) {
  if (!node.isMapping()) {
    return LocalizedText::of(reader.text(node));
  }
  LocalizedText localized;
  for (const core::YamlEntry& entry : node.entries()) {
    if (!isLanguageCode(entry.key)) {
      reader.fail(entry.value,
                  std::format("'{}' is not a language code, such as en or es", entry.key));
      return {};
    }
    localized.byLanguage.emplace(entry.key, reader.text(entry.value));
  }
  if (reader.ok() && !localized.byLanguage.contains(ReferenceLanguage)) {
    reader.fail(node, std::format("a text in several languages needs '{}', the one the others "
                                  "fall back to",
                                  ReferenceLanguage));
  }
  return reader.ok() ? localized : LocalizedText{};
}

} // namespace dwe::assets
