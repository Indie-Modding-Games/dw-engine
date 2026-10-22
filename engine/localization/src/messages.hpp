#pragma once

#include <string_view>

// The log and error messages of localization. Templates take their arguments in the order of their placeholders.
namespace dwe::localization::messages {

inline constexpr std::string_view NoLanguage = "the packs have no texts in '{}' (no localization/{}/)";
inline constexpr std::string_view UnreadableTable = "cannot read the texts of {}: {}";
// One line per pack for its tables with no translation at all: the pack, the language, how many tables and texts, the
// language they show in, and the first tables by id.
inline constexpr std::string_view MissingTables =
    "pack {} has no {} translation of {} tables ({} texts), which show in {}: {}";
inline constexpr std::string_view MoreNamed = ", ...";
inline constexpr std::string_view MissingKeys = "{} misses {} keys of {}, which show in {}: {}";
inline constexpr std::string_view Loaded = "localization: texts in {} ({} keys, {} from {})";
inline constexpr std::string_view Warning = "localization: {}";

} // namespace dwe::localization::messages
