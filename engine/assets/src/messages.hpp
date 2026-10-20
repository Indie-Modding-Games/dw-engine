#pragma once

#include <string_view>

// The error messages of assets. Templates take their arguments in the order of their placeholders.
namespace dwe::assets::messages {

// Text tables (text_table.cpp).
inline constexpr std::string_view TableContext = "text table {}";
inline constexpr std::string_view GameTableWhat = "the text table";
inline constexpr std::string_view StringsWhat = "a block of strings";
inline constexpr std::string_view NotText = "must be a text";
inline constexpr std::string_view NotMapping = "must map keys to texts";
inline constexpr std::string_view BadKey = "'{}' is not a key: use segments of a-z, 0-9 and _ joined by dots";
inline constexpr std::string_view UnclosedPlaceholder = "a {{ at byte {} has no }}: write {{{{ for a brace";
inline constexpr std::string_view InFile = "{}: {}";
inline constexpr std::string_view NoPackOfFile = "{} belongs to the pack '{}', which is not mounted";
inline constexpr std::string_view LoneClosingBrace = "a }} at byte {} closes nothing: write }}}} for a brace";

// The translation check (checkTranslations).
inline constexpr std::string_view MissingTable = "has no {} translation ({}): its {} texts show in {}";
inline constexpr std::string_view NoReferenceTable = "has no {} table to translate ({})";
inline constexpr std::string_view MissingKey = "misses the key {}, which shows in {}";
inline constexpr std::string_view ExtraKey = "has the key {}, which {} does not have";
inline constexpr std::string_view PlaceholderMismatch =
    "has the key {} with the placeholders {{{}}}, where {} has {{{}}}";
inline constexpr std::string_view KeyInTwoTables = "has the key {}, which {} has too";

// The lock of a translation (checkSourcesLocks).
inline constexpr std::string_view LockContext = "the lock {}";
inline constexpr std::string_view LockWhat = "a translation's lock";
inline constexpr std::string_view LockSourcesWhat = "the sources of a lock";
inline constexpr std::string_view LockKeptWhat = "the strings kept in English";
inline constexpr std::string_view BadHash = "'{}' is not a SHA-1: write its 40 hex digits in lowercase";
inline constexpr std::string_view NotInEnglish = "names {}, which the English text does not have";
inline constexpr std::string_view StaleTranslation =
    "{} was translated from another English text: check it and write the new hash, {}";
inline constexpr std::string_view UnhashedTranslation =
    "{} is translated, but the lock has no hash of its English text";
inline constexpr std::string_view KeptAndTranslated = "{} is kept as it is, but the translation has it";

} // namespace dwe::assets::messages
