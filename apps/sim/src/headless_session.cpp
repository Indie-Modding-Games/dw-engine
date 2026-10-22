#include <dwe/sim/headless_session.hpp>
#include <dwe/sim/messages.hpp>

#include <string>
#include <utility>

namespace dwe::sim {

core::Result<HeadlessSessions>
HeadlessSessions::prepare(const ScenarioContext& context,
                          std::shared_ptr<const game::partner::SpeciesTable> species) {
  if (context.files == nullptr || context.rules == nullptr) {
    return core::fail(std::string{messages::NoScenarioContext});
  }
  if (!species) {
    auto table = game::partner::loadSpeciesTable(*context.files, *context.rules);
    if (!table) {
      return std::unexpected{table.error()};
    }
    species = std::make_shared<const game::partner::SpeciesTable>(std::move(*table));
  }
  auto tables = session::loadSystemTables(*context.files, *context.rules);
  if (!tables) {
    return std::unexpected{tables.error()};
  }
  return HeadlessSessions{context, std::move(species), std::move(*tables)};
}

core::Result<std::unique_ptr<session::GameSession>>
HeadlessSessions::start(std::uint64_t seed) const {
  auto game = startGame(seed);
  if (!game) {
    return std::unexpected{game.error()};
  }
  return std::move(game->session);
}

core::Result<HeadlessGame> HeadlessSessions::startGame(std::uint64_t seed) const {
  session::GameSystems systems = session::createGameSystems(*context_.files, tables_);
  HeadlessGame game{.session = nullptr,
                    .evolution = systems.evolution,
                    .inventory = systems.inventory,
                    .battle = systems.battle};
  auto session = session::GameSession::create({.files = context_.files,
                                               .rulesetId = context_.rules->id(),
                                               .rules = *context_.rules,
                                               .species = species_,
                                               .seed = seed,
                                               .storage = nullptr,
                                               .gameVersion = {},
                                               .wallClock = {},
                                               .systems = std::move(systems.systems)});
  if (!session) {
    return std::unexpected{session.error()};
  }
  if (auto started = (*session)->startNewGame(); !started) {
    return std::unexpected{started.error()};
  }
  (*session)->clock().setSpeed(context_.speed);
  game.session = std::move(*session);
  return game;
}

} // namespace dwe::sim
