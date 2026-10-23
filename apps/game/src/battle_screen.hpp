#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/screens/battle_view.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/session_music.hpp>
#include <dwe/ui/ui.hpp>

#include <optional>
#include <string_view>

namespace dwe::app {

// The battle screen's document and the commands it sends.
inline constexpr std::string_view BattleDocument = "battle";
inline constexpr std::string_view BattleCommandName = "battle_command";
inline constexpr std::string_view BattleChargeCommand = "battle_charge";
inline constexpr std::string_view BattleCloseCommand = "battle_close";
// The tamer's bag (battle_tick.c: tamerTickBattle opens it with its own button): open, close, and
// throw the item of a slot (the command's number) to the partner.
inline constexpr std::string_view BattleBagOpenCommand = "battle_bag_open";
inline constexpr std::string_view BattleBagCloseCommand = "battle_bag_close";
inline constexpr std::string_view BattleThrowCommand = "battle_throw";
// What the battle screen shows: the fight or the end box.
inline constexpr std::string_view BattlePhaseSetting = "battle.phase";
inline constexpr std::string_view FightPhase = "fight";
inline constexpr std::string_view EndPhase = "end";

struct BattleScreenDesc {
  session::GameSession* session = nullptr;
  session::BattleAdapter* battle = nullptr;
  session::InventoryAdapter* inventory = nullptr;
  // For the fighters' names; it may be null.
  const assets::DigimonTable* species = nullptr;
  ui::Ui* ui = nullptr;
};

// Plays a battle on the battle document: starts it through the session's battle adapter, runs it on
// the game loop's ticks, passes the player's commands to it, publishes its view, and once it ends
// gives the result to the session and shows the end box until the player closes it. The rules are
// the battle's; this only shows and forwards.
class BattleScreen {
public:
  explicit BattleScreen(BattleScreenDesc desc) : desc_(desc) {}

  core::Result<void> start(session::BattleRequest request);

  // Whether the battle or its end box is on screen: the field and the game clock wait meanwhile.
  [[nodiscard]] bool active() const { return battle_.has_value() || ending_; }
  // The outcome of the last battle, once, after its end box is closed: what a story battle reads.
  [[nodiscard]] std::optional<game::battle::Outcome> takeOutcome();

  // One logic tick of the battle and the view of it.
  void tick(double seconds);

  // Takes the battle document's commands; false for any other command.
  bool handle(const ui::UiCommand& command);

private:
  [[nodiscard]] screens::NameKeys names() const;
  void publish() const;
  void openBag();
  void throwItem(double slot);
  void finish();
  void close();
  [[nodiscard]] session::BattleMusicFacts musicFactsOf(const session::BattleRequest& request) const;

  BattleScreenDesc desc_;
  std::optional<game::battle::Battle> battle_;
  bool ending_ = false;
  std::optional<game::battle::Outcome> outcome_;
  // Whether the battle on the end box was won: the field's music comes back when it closes.
  bool won_ = false;
};

} // namespace dwe::app
