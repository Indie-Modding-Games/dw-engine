#pragma once

#include <string_view>

// The messages of dwx. Templates take their arguments in the order of their placeholders.
namespace dwe::dwx::messages {

// dwx validate: a translation that differs from the reference language (assets::checkTranslations).
inline constexpr std::string_view TranslationProblem = "the translation {} {}";

// dwx extract: the engine cannot mount the pack just written, or open one of its assets.
inline constexpr std::string_view EngineCheckFailed = "engine check failed: {}";

// dwx extract --cache: the dev cache of extracted discs.
inline constexpr std::string_view CacheExtracting =
    "dev cache: extracting {} into {} (extractor version {}); the first time for a disc this takes a few minutes and "
    "about 1.5 GB for the whole game";
inline constexpr std::string_view CacheWritten = "dev cache: {} written (extractor version {})";
inline constexpr std::string_view CacheIncompleteEntry =
    "dev cache: {} is not a complete entry (a link, a file or a folder without its record); extracting it again";
inline constexpr std::string_view CacheCannotSync = "dev cache: cannot flush {} to the disk";
inline constexpr std::string_view CacheReady = "dev cache: {} is ready (extractor version {})";
inline constexpr std::string_view CacheRemovedStale = "dev cache: removed {}, made by an older extractor version";
inline constexpr std::string_view CacheOldPartials =
    "dev cache: temporary folders older than {} h, left by stopped extractions; delete them by hand while no "
    "extraction is running: {}";
inline constexpr std::string_view CacheRecordComment = "Written by dwx extract --cache: the disc and the extractor "
                                                       "version of this entry.";
inline constexpr std::string_view CacheCannotCreate = "cannot create {}: {}";
inline constexpr std::string_view CacheCannotList = "cannot list {}: {}";
inline constexpr std::string_view CacheCannotWrite = "cannot write {}";
inline constexpr std::string_view CacheCannotRename = "cannot move {} to {}: {}";
inline constexpr std::string_view CacheCannotRemove = "cannot remove {}: {}";

} // namespace dwe::dwx::messages
