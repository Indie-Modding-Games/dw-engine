#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/versus/link.hpp>
#include <dwe/session/battle_adapter.hpp>

#include <functional>
#include <optional>
#include <utility>

namespace dwe::session {

// One machine's side of an online versus series (vs_scene.c: VS_initializeVS over the network):
// the versus link to the other machine and the battles both play in lockstep. Each battle frame
// this side sends its command (or none), and every tick whose two inputs are in is played here as
// on the other machine: the commands, the step and the checksum. A battle that ends tells its
// result, and the series counts it once both agree. The screens only show it and give the player's
// commands; nothing here reads or writes a save.
class VersusSeries {
public:
  // `battles` must outlive the series and have read its rules.
  VersusSeries(const BattleAdapter& battles, game::versus::VersusLink link,
               VersusPlacement placement);

  // Reads the network and checks its timeouts; once per game tick at least.
  void poll(net::Millis now);

  [[nodiscard]] game::versus::VersusLink& link() { return link_; }
  [[nodiscard]] const game::versus::VersusLink& link() const { return link_; }
  // Where the next battles place their fighters: the series' arena is known only once the host's
  // setup arrived, so the flow sets it then; both machines must set the same.
  void setPlacement(VersusPlacement placement) { placement_ = std::move(placement); }
  // This machine's fighter in the battles: the partner (1P) for the host, the rival (2P) for the
  // guest.
  [[nodiscard]] game::battle::FighterId localFighter() const;

  // Starts the series' next battle with both lineups' fighters of its number and the agreed seed:
  // only while the series plays and no battle is on. A lineup this content cannot build ends the
  // series for both (BadLineup) and fails.
  //
  // `prepare` readies the new battle before its first tick: what both machines' content gives it
  // and the battles of the story get too (the clips its fighters' models time their moves with,
  // the flights of its techniques; battle_screen.cpp). It must do the same on both machines, and
  // never anything one machine's screen alone decides, or the lockstep's checksums part.
  using BattlePrep = std::function<void(game::battle::Battle&)>;
  core::Result<void> startNext(const BattlePrep& prepare = {});
  // The battle on screen: the one that runs, or the last one until the next starts.
  [[nodiscard]] const game::battle::Battle* battle() const { return battle_ ? &*battle_ : nullptr; }
  // The command the player gives with this side's next input; a later one replaces it.
  void command(game::battle::Command command) { pending_ = command; }
  // One battle frame (20 a second): this side's input, and the ticks both sides' inputs allow.
  // Returns the events of the ticks played (none while the other machine is behind).
  game::battle::BattleEvents frame();
  // Whether the battle on screen ended and both machines agreed on its result.
  [[nodiscard]] bool roundSettled() const;

private:
  const BattleAdapter* battles_;
  game::versus::VersusLink link_;
  VersusPlacement placement_;
  std::optional<game::battle::Battle> battle_;
  std::optional<game::battle::Command> pending_;
};

} // namespace dwe::session
