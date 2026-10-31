#pragma once

#include <dwe/game/save/slots.hpp>

#include <string>

namespace dwe::app {

// One slot's line in --list-saves: why it cannot be read (a damaged file, or a newer game's),
// empty, or its header and summary.
[[nodiscard]] std::string describeSlot(const game::save::SlotInfo& info);

} // namespace dwe::app
