#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/ruleset_store.hpp>
#include <dwe/sim/scenario.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::sim {

inline constexpr std::string_view Usage =
    "dwe-sim --scenario <name> [--content <folder>] [--mods <folder>]... [--ruleset <id>] [--runs <n>] [--seed <n>] "
    "[--threads <n>] [--days <n>] [--speed <x>] [--set <section>.<path>=<value>]... [--param <key>=<value>]... "
    "[--out <folder>] | --list";

// A value of the ruleset changed from the command line (--set care.hunger.window_minutes=45).
struct ValueOverride {
    std::string section;
    std::string path;
    std::string yaml;
    // As the command line gave it, for the report.
    std::string text;
};

// What the command line asks for.
struct SimOptions {
    // Lists the scenarios instead of running one.
    bool list = false;
    std::string scenario;
    // As in the game: the content folder, the mods folders and the ruleset id.
    std::filesystem::path content = "content";
    std::vector<std::filesystem::path> mods;
    std::string ruleset{ruleset::OriginalRuleset};
    std::size_t runs = 100;
    std::uint64_t seed = 1;
    // 0 takes one per hardware thread.
    std::size_t threads = 0;
    std::int64_t days = 1;
    // An hour of game time per real second by default: the fast clock.
    double speed = 3600.0;
    std::vector<ValueOverride> overrides;
    ScenarioParameters parameters;
    // Where the Markdown and CSV reports go; with none, the Markdown goes to the standard output only.
    std::optional<std::filesystem::path> out;
};

// Reads a --set value: <section>.<path>=<yaml>.
core::Result<ValueOverride> parseValueOverride(std::string_view text);

// Reads the command line (without the program's name).
core::Result<SimOptions> parseSimOptions(std::span<const std::string_view> arguments);

// Applies the --set values to the active ruleset of the store, each checked as the tuning panel's are.
core::Result<void> applyOverrides(ruleset::RulesetStore& store, std::span<const ValueOverride> overrides);

} // namespace dwe::sim
