#pragma once

#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// A medal of `data/medals` (content/schemas/medals.schema.json): the executable's MEDAL_NAMES and
// MEDAL_DESCRIPTIONS, which the medals chart shows (game_menu.c: renderMedalView).
struct MedalEntry {
  std::string id;
  std::int32_t index = 0;
  std::string name;
  // The three lines under the name; a missing line is empty.
  std::array<std::string, 3> description;
};

struct MedalTable {
  std::vector<MedalEntry> medals;

  // The medal with this number, or nullptr.
  [[nodiscard]] const MedalEntry* find(std::int32_t index) const;
};

// Parses the text of `data/medals`. Unknown and missing keys are errors.
core::Result<MedalTable> parseMedalTable(std::string_view yaml);

// Reads and parses `data/medals`; nothing when no pack has it.
core::Result<std::optional<MedalTable>> loadMedalTable(const VirtualFileSystem& vfs);

} // namespace dwe::assets
