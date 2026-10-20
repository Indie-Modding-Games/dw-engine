#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/ruleset_store.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dwe::debug {

// What the tuning panel (R3.4) works on: the packs, the game's ruleset store and its game clock. The clock may be
// missing, and then the panel has no clock controls. All must outlive the panel.
struct TuningTarget {
    const assets::VirtualFileSystem* files = nullptr;
    ruleset::RulesetStore* store = nullptr;
    ruleset::GameClock* clock = nullptr;
};

// A line of a system's tab: a name and its value as text, and a fill from 0 to 1 when it is a meter (hunger, energy).
struct TuningRow {
    std::string label;
    std::string value;
    std::optional<float> meter;
};

// A tab a game system adds to the tuning panel to show what it sees: called every frame the tab is open.
struct TuningTab {
    std::string name;
    std::function<std::vector<TuningRow>()> rows;
};

} // namespace dwe::debug
