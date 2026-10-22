#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/game_systems.hpp>
#include <dwe/sim/scenario.hpp>

#include <cstdint>
#include <memory>
#include <utility>

namespace dwe::sim {

// A headless game: the session and its systems by type, for the commands a scenario or a test plays
// on it (an item, a battle). The adapters belong to the session and live as long as it does.
struct HeadlessGame {
  std::unique_ptr<session::GameSession> session;
  session::EvolutionAdapter* evolution = nullptr;
  session::InventoryAdapter* inventory = nullptr;
  session::BattleAdapter* battle = nullptr;
};

// Game sessions for a scenario's runs, without a window, a field or saves: the whole game as the
// session plays it (partner care, evolution, inventory, battle and autosave requests), on the
// scenario's ruleset with its --set values. Prepared once in setup(), with the systems' tables;
// start() reads only what was prepared, so runs on several threads may call it.
class HeadlessSessions {
public:
  // Reads the species table of the context's ruleset, unless `species` gives one (tests without the
  // disc).
  static core::Result<HeadlessSessions>
  prepare(const ScenarioContext& context,
          std::shared_ptr<const game::partner::SpeciesTable> species = nullptr);

  // A new game with this seed, its clock at the context's speed.
  [[nodiscard]] core::Result<std::unique_ptr<session::GameSession>> start(std::uint64_t seed) const;
  // The same new game, with its systems by type.
  [[nodiscard]] core::Result<HeadlessGame> startGame(std::uint64_t seed) const;

  // The systems' tables read in prepare().
  [[nodiscard]] const session::SystemTables& tables() const { return tables_; }

private:
  HeadlessSessions(const ScenarioContext& context,
                   std::shared_ptr<const game::partner::SpeciesTable> species,
                   session::SystemTables tables)
      : context_(context), species_(std::move(species)), tables_(std::move(tables)) {}

  ScenarioContext context_;
  std::shared_ptr<const game::partner::SpeciesTable> species_;
  session::SystemTables tables_;
};

} // namespace dwe::sim
