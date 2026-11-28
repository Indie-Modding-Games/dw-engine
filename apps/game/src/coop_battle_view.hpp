#pragma once

#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/state.hpp>
#include <dwe/game/coop/battle.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/screens/battle_view.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/ui/ui.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
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

// The host's side of a guest's HUD: each fighter's equipped move ids for the battle's begin (the
// enemies' left empty), and a frame's battle part for the guest whose partner is `own` (the
// fighting one for a helper): every fighter's bars and current order, and that partner's strip,
// bag, finisher gauge and Folder plate.
[[nodiscard]] std::vector<game::coop::BattleMoveIds>
coopBattleMovesOf(const game::battle::Battle& battle);
[[nodiscard]] game::coop::BattleFrame coopBattleFrameOf(const game::battle::Battle& battle,
                                                        game::battle::FighterId own);

// The partner a guest's next item flies to after a step through its picks (-1 back, 1 on): its
// own (none picked), then each other partner in the ally rows' order, around.
[[nodiscard]] std::optional<std::size_t>
coopNextThrowTarget(const game::coop::BattleBegin& begin, game::coop::BattleRole role,
                    game::coop::PlayerId self, std::optional<std::size_t> current, int step);

// The host's battle as a guest's battle document draws it (the host's own HUD): its partner in
// zone B, the other partners as ally rows with their current orders, the enemies, the commands its
// frame offers, its finisher gauge and its Folder plate.
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
  // The partner this player's next item flies to (none: its own, or the fighting one).
  std::optional<std::size_t> throwTarget{};
};
[[nodiscard]] screens::BattleView coopBattleViewOf(const CoopBattleViewSource& source,
                                                   const screens::NameKeys& names);

// The battle document's phase on a guest's HUD for the host's: from the host's intro on, as the
// host's own HUD shows; none at the end box, which is the coop_battle document's.
[[nodiscard]] std::optional<std::string_view> coopHudPhaseOf(game::coop::BattlePhase phase);

// Whether the bag shown is a guest's battle bag whose tab keys pick the partner an item flies to.
inline constexpr std::string_view BagThrowPickSetting = "bag.throw_pick";

// Opens this player's bag over its battle HUD as BattleScreen::openBag opens the host's: the HUD
// learns the bag is open before the bag shows, so the battle document leaves the focus to it.
void showCoopHudBag(ui::Ui& ui, const screens::ScreenSettings& bag, const screens::BattleView& hud);
// Closes that bag, closed or thrown from: no ally stays picked to throw to.
void closeCoopHudBag(ui::Ui& ui, std::optional<std::size_t>& throwTarget);

// How the host's battle ended as the guest's end box names it (coop_battle.outcome): victory, fled
// or defeat; empty while it has not.
[[nodiscard]] std::string_view coopOutcomeName(std::optional<game::coop::BattleOutcome> outcome);

// The bag slot a command thrown from the bag over a guest's battle HUD names: the bag's Use (sent
// once the bag has closed, partner_menu/logic.lua) or the battle document's throw.
[[nodiscard]] std::optional<std::size_t> coopHudThrownSlot(const ui::UiCommand& command);

// The field's bag (the partner menu outside a battle) closes when the host's battle takes this
// player's screen: its Use would feed the partner on the field.
void closeFieldBag(ui::Ui& ui);

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
