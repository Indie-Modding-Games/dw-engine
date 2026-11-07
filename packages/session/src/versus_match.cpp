#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/sha1.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/session/battle_registry.hpp>
#include <dwe/session/versus_match.hpp>

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

namespace dwe::session {

namespace {

// The low byte of an input carries the command; the rest is free for later protocol versions.
constexpr std::uint32_t CommandMask = 0xFF;
constexpr auto LastCommand = static_cast<std::uint32_t>(game::battle::Command::Finisher);

// Hashes a piece of text with its length first, so "ab" + "c" and "a" + "bc" differ.
void addText(core::Sha1& hash, std::string_view text) {
  const auto size = static_cast<std::uint64_t>(text.size());
  hash.update(std::as_bytes(std::span{&size, 1}));
  hash.update(std::as_bytes(std::span{text.data(), text.size()}));
}

} // namespace

game::versus::VersusFighter versusFighterOf(const RegisteredPartner& partner) {
  return {.name = partner.name,
          .species = partner.species,
          .hp = partner.hp,
          .mp = partner.mp,
          .offense = partner.offense,
          .defense = partner.defense,
          .speed = partner.speed,
          .brains = partner.brains,
          .discipline = partner.discipline,
          .techniques = partner.techniques};
}

core::Result<std::string> versusContentHash(const ruleset::Ruleset& rules,
                                            const assets::VirtualFileSystem& files) {
  core::Sha1 hash;
  addText(hash, rules.id());
  addText(hash, rules.info().version);
  for (const std::string& link : rules.info().chain) {
    addText(hash, link);
  }
  for (const auto& [name, section] : rules.sections()) {
    auto text = core::emitYaml(section.node);
    if (!text) {
      return std::unexpected{text.error()};
    }
    addText(hash, name);
    addText(hash, *text);
  }
  for (const auto& [role, id] : rules.tables()) {
    auto bytes = files.read(id);
    if (!bytes) {
      return std::unexpected{bytes.error()};
    }
    addText(hash, role);
    addText(hash, id.str());
    const auto size = static_cast<std::uint64_t>(bytes->size());
    hash.update(std::as_bytes(std::span{&size, 1}));
    hash.update(*bytes);
  }
  return core::toHex(hash.finish());
}

core::Result<game::versus::Identity> versusIdentity(std::string gameVersion,
                                                    const ruleset::Ruleset& rules,
                                                    const assets::VirtualFileSystem& files) {
  auto content = versusContentHash(rules, files);
  if (!content) {
    return std::unexpected{content.error()};
  }
  return game::versus::Identity{.protocol = game::versus::ProtocolVersion,
                                .gameVersion = std::move(gameVersion),
                                .contentHash = std::move(*content)};
}

std::uint32_t versusInputOf(game::battle::Command command) {
  return static_cast<std::uint32_t>(command);
}

game::battle::Command versusCommandOf(std::uint32_t input) {
  const std::uint32_t command = input & CommandMask;
  return command > LastCommand ? game::battle::Command::None
                               : static_cast<game::battle::Command>(command);
}

void commandVersus(game::battle::Battle& battle, const std::array<std::uint32_t, 2>& inputs) {
  const std::array<game::battle::FighterId, 2> sides{game::battle::PartnerId,
                                                     game::battle::RivalId};
  for (std::size_t side = 0; side < sides.size(); ++side) {
    const game::battle::Command command = versusCommandOf(inputs.at(side));
    if (command != game::battle::Command::None) {
      // A refused command is dropped alike on both machines.
      (void)battle.command(command, sides.at(side));
    }
  }
}

game::versus::RoundResult roundResultOf(const game::battle::BattleResult& result) {
  if (result.draw) {
    return game::versus::RoundResult::BothLost;
  }
  return result.outcome == game::battle::Outcome::Victory ? game::versus::RoundResult::HostWon
                                                          : game::versus::RoundResult::GuestWon;
}

std::uint64_t roundSeed(const game::versus::SeriesSetup& setup, std::int32_t round) {
  return setup.seed + static_cast<std::uint64_t>(round);
}

} // namespace dwe::session
