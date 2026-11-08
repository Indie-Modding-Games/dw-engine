#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/modes/game_mode.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::modes {

// A mode the catalog left out, and why.
struct SkippedMode {
  std::string id;
  std::string reason;
};

// The game modes of the mounted packs (modes/<id>/mode), in the order the mode list shows them. A
// pack replaces a mode of a pack below it by giving the same folder, as with any asset. A mode that
// does not parse, or names a ruleset no pack has, is left out with its reason and logged; the
// others stay, so a broken mod cannot keep the game from starting.
class ModeCatalog {
public:
  // Reads every mode of the packs.
  static ModeCatalog load(const assets::VirtualFileSystem& files);

  // A catalog of these modes, sorted (tests, tools).
  explicit ModeCatalog(std::vector<GameMode> modes, std::vector<SkippedMode> skipped = {});

  [[nodiscard]] std::span<const GameMode> modes() const { return modes_; }
  [[nodiscard]] std::span<const SkippedMode> skipped() const { return skipped_; }
  [[nodiscard]] const GameMode* find(std::string_view id) const;

  // The mode a new game plays when the player is not asked: the original's story, else the first
  // of the list; nullptr when there is none.
  [[nodiscard]] const GameMode* defaultMode() const;

  // Whether the player chooses a mode before a new game: only when there is more than one.
  [[nodiscard]] bool asksMode() const { return modes_.size() > 1; }

private:
  std::vector<GameMode> modes_;
  std::vector<SkippedMode> skipped_;
};

} // namespace dwe::game::modes
