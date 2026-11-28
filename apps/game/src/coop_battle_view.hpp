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
#include <utility>
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

// Without the partner's moves; with `folder` the strip names its stances.
[[nodiscard]] std::string coopOrderLabelKey(game::battle::Command command, bool folder);

// A helper's HUD shows the starter's partner; frames list starter, enemies, then allies.
[[nodiscard]] std::size_t coopOwnFighterOf(const game::coop::BattleBegin& begin,
                                           game::coop::BattleRole role, game::coop::PlayerId self);

// Enemies' move ids stay empty; `own` is the guest's partner (the fighting one for a helper).
[[nodiscard]] std::vector<game::coop::BattleMoveIds>
coopBattleMovesOf(const game::battle::Battle& battle);
[[nodiscard]] game::coop::BattleFrame coopBattleFrameOf(const game::battle::Battle& battle,
                                                        game::battle::FighterId own);

// `step` is -1 or 1; nullopt is its own partner, then the ally rows' order, wrapping.
[[nodiscard]] std::optional<std::size_t>
coopNextThrowTarget(const game::coop::BattleBegin& begin, game::coop::BattleRole role,
                    game::coop::PlayerId self, std::optional<std::size_t> current, int step);

struct CoopBattleViewSource {
  const game::coop::BattleBegin* begin = nullptr;
  const game::coop::BattleFrame* frame = nullptr;
  std::span<const game::coop::BattleFighterState> fighters;
  game::coop::BattleRole role = game::coop::BattleRole::Helper;
  game::coop::PlayerId self = game::coop::HostPlayerId;
  const game::battle::Fighter* partner = nullptr;
  const game::battle::MoveCatalog* moves = nullptr;
  bool bagOpen = false;
  // nullopt: its own partner (or the fighting one).
  std::optional<std::size_t> throwTarget{};
};
[[nodiscard]] screens::BattleView coopBattleViewOf(const CoopBattleViewSource& source,
                                                   const screens::NameKeys& names);

// nullopt at the end box, which the coop_battle document draws.
[[nodiscard]] std::optional<std::string_view> coopHudPhaseOf(game::coop::BattlePhase phase);

// Whether the bag shown is a guest's battle bag whose tab keys pick the partner an item flies to.
inline constexpr std::string_view BagThrowPickSetting = "bag.throw_pick";

// The HUD learns the bag is open before it shows, so the battle document yields the focus.
void showCoopHudBag(ui::Ui& ui, const screens::ScreenSettings& bag, const screens::BattleView& hud);
class CoopThrowPick;
void closeCoopHudBag(ui::Ui& ui, CoopThrowPick& throwTarget);

// Empty while the battle has not ended.
[[nodiscard]] std::string_view coopOutcomeName(std::optional<game::coop::BattleOutcome> outcome);

// The bag's Use (sent once the bag closed, partner_menu/logic.lua) or the battle throw.
[[nodiscard]] std::optional<std::size_t> coopHudThrownSlot(const ui::UiCommand& command);

// The host's battle takes this screen: the field bag's Use would feed the field partner.
void closeFieldBag(ui::Ui& ui);

// An ally whose player left its battle (its link went, or it changed maps) runs off: the battle
// counts it as fled. In a battle with no fleeing it fights on its own call.
void withdrawCoopAlly(game::battle::Battle& battle, game::battle::FighterId ally);

// The partner the guest's items fly to (a fighter's number; nullopt: its own, or the fighting
// one). The bag's Use comes once the bag closed (partner_menu/logic.lua): closing darkens the card
// and keeps the pick for that Use.
class CoopThrowPick {
public:
  [[nodiscard]] const std::optional<std::size_t>& lit() const { return lit_; }
  void pick(std::optional<std::size_t> target) {
    lit_ = target;
    kept_.reset();
  }
  void close() { kept_ = std::exchange(lit_, std::nullopt); }
  // The throw's target for the host: the fighter's number + 1, 0 for its own or the fighting one.
  [[nodiscard]] std::int32_t take();
  void reset() {
    lit_.reset();
    kept_.reset();
  }

private:
  std::optional<std::size_t> lit_;
  std::optional<std::size_t> kept_;
};

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

// Throws are paid once the host's frames show them (coop-combat.md §7.1); a refused one is free.
// One throw at a time waits for the host's answer, at most WaitFrames.
class CoopThrowPurse {
public:
  static constexpr std::uint32_t WaitFrames = 40;

  [[nodiscard]] bool canAsk() const { return !asked_; }
  void asked(std::uint32_t frame) { asked_ = frame; }
  [[nodiscard]] std::vector<std::string> due(std::span<const game::coop::BattleThrow> throws,
                                             game::coop::PlayerId self, std::uint32_t frame);
  void reset();

private:
  std::optional<std::uint32_t> asked_;
  std::set<std::uint16_t> paid_;
};

} // namespace dwe::app
