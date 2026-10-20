#pragma once

#include <string_view>

// The log and error messages of localization. Templates take their arguments in the order of their placeholders.
namespace dwe::localization::messages {

inline constexpr std::string_view NoLanguage = "the packs have no texts in '{}' (no localization/{}/)";
inline constexpr std::string_view UnreadableTable = "cannot read the texts of {}: {}";
inline constexpr std::string_view MissingTable = "{} has no {} translation: its {} texts show in {}";
inline constexpr std::string_view MissingKeys = "{} misses {} keys of {}, which show in {}: {}";
inline constexpr std::string_view Loaded = "localization: texts in {} ({} keys, {} from {})";
inline constexpr std::string_view Warning = "localization: {}";

} // namespace dwe::localization::messages
