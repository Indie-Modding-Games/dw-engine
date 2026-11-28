#pragma once

#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/state.hpp>
#include <dwe/game/coop/battle.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/screens/battle_view.hpp>
#include <dwe/screens/name_keys.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace dwe::app {

struct CoopBattleSounds {
  std::vector<std::string> enemies;
  std::vector<std::string> partners;
  std::map<std::int16_t, std::int64_t> slots;

  [[nodiscard]] std::int64_t slotOf(std::int16_t figureSlot) const;
};
[[nodiscard]] CoopBattleSounds coopBattleSoundsOf(const game::coop::BattleBegin& begin,
                                                  game::coop::PlayerId self);

// A command's label without its partner's moves: the original's command names, the techniques'
// and the finisher's own words; the Folder battle's strip names its stances.
[[nodiscard]] std::string coopOrderLabelKey(game::battle::Command command, bool folder);

// The fighter a guest's HUD shows as its own partner: its own when it fights, the starter's for a
// helper. The frames list the starter's partner, then the enemies, then the allies.
[[nodiscard]] std::size_t coopOwnFighterOf(const game::coop::BattleBegin& begin,
                                           game::coop::BattleRole role, game::coop::PlayerId self);

// The host's battle as a guest's battle document draws it (the host's own HUD): its partner in
// zone B, the other partners as ally rows, the enemies, and the commands its frame offers.
struct CoopBattleViewSource {
  const game::coop::BattleBegin* begin = nullptr;
  const game::coop::BattleFrame* frame = nullptr;
  std::span<const game::coop::BattleFighterState> fighters;
  game::coop::BattleRole role = game::coop::BattleRole::Helper;
  game::coop::PlayerId self = game::coop::HostPlayerId;
  // This player's partner with its moves, for its techniques' names and icons.
  const game::battle::Fighter* partner = nullptr;
  const game::battle::MoveCatalog* moves = nullptr;
  bool bagOpen = false;
};
[[nodiscard]] screens::BattleView coopBattleViewOf(const CoopBattleViewSource& source,
                                                   const screens::NameKeys& names);

class CoopThrowFollower {
public:
  struct Step {
    std::vector<game::coop::PlayerId> started;
    std::vector<game::coop::BattleThrow> landed;
    std::optional<game::coop::BattleThrow> shown;
  };

  [[nodiscard]] Step follow(std::span<const game::coop::BattleThrow> throws);
  void reset();

private:
  std::set<std::uint16_t> started_;
  std::set<std::uint16_t> landed_;
};

// The items this player throws in the host's battle are paid from its own bag once the host's
// frames show the throw (docs/architecture/coop-combat.md §7.1): one the host refused costs
// nothing. One throw at a time waits for the host's answer, at most WaitFrames.
class CoopThrowPurse {
public:
  static constexpr std::uint32_t WaitFrames = 40;

  [[nodiscard]] bool canAsk() const { return !asked_; }
  void asked(std::uint32_t frame) { asked_ = frame; }
  // The items `self`'s throws in this frame that were not paid yet cost.
  [[nodiscard]] std::vector<std::string> due(std::span<const game::coop::BattleThrow> throws,
                                             game::coop::PlayerId self, std::uint32_t frame);
  void reset();

private:
  std::optional<std::uint32_t> asked_;
  std::set<std::uint16_t> paid_;
};

} // namespace dwe::app
