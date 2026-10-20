#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace dwe::ruleset::testing {

// The message of a failed result. doctest builds a message even when the check passes, so it cannot call error().
template <typename T>
std::string errorOf(const core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

inline constexpr std::string_view Clock = R"(frames_per_second: 20
frames_per_minute: 20
days_per_year: 30
year_limit: 100
start:
  year: 0
  day: 0
  hour: 8
  minute: 0
)";

inline constexpr std::string_view Care = R"(hunger:
  window_minutes: 90
selection: original
)";

// Two packs in memory: a base pack with two tables, and a rules pack with the ruleset `small` (clock and care
// sections). Tests add or replace files before mounting.
class FixturePacks {
public:
    FixturePacks() {
        files.addFile("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
        files.addFile("base/data/digimon.yaml", "digimon: []\n");
        files.addFile("base/data/raise.yaml", "raise: []\n");
        files.addFile("rules/pack.yaml", "id: rules\nversion: 1.0.0\nlayer: mods\n");
        files.addFile("rules/rules/small/ruleset.yaml", R"(id: small
name: Small
version: 1.2.3
description: A ruleset for the tests.
tables:
  digimon: data/digimon
  raise: data/raise
sections: [clock, care]
)");
        files.addFile("rules/rules/small/clock.yaml", Clock);
        files.addFile("rules/rules/small/care.yaml", Care);
    }

    // Mounts the packs as they are now.
    const assets::VirtualFileSystem& mount() {
        const std::array<std::filesystem::path, 2> roots{"base", "rules"};
        auto mounted = assets::VirtualFileSystem::mount(files, roots);
        REQUIRE_MESSAGE(mounted.has_value(), errorOf(mounted));
        vfs.emplace(std::move(*mounted));
        return *vfs;
    }

    assets::MemoryFileSystem files;
    std::optional<assets::VirtualFileSystem> vfs;
};

} // namespace dwe::ruleset::testing
