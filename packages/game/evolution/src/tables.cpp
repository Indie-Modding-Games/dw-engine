#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/evolution/messages.hpp>
#include <dwe/game/evolution/tables.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/species_extra.hpp>

#include <algorithm>
#include <charconv>
#include <optional>
#include <utility>

namespace dwe::game::evolution {

namespace {

// A reference as dwx writes it: an id, nothing, or a raw number outside the referenced table.
std::optional<std::int32_t> rawNumber(std::string_view text) {
  std::int32_t number = 0;
  const auto [end, problem] = std::from_chars(text.data(), text.data() + text.size(), number);
  if (problem != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return number;
}

// Whether the Digimon table has a row whose original index is `index`.
bool hasDigimonIndex(const assets::DigimonTable& table, std::int32_t index) {
  return std::ranges::any_of(table.digimon,
                             [&](const assets::DigimonRow& row) { return row.index == index; });
}

// The species a row of the evolution table belongs to: the one it names, else its place in the
// original (a row whose Digimon is a raw number outside the table), counted from `first`; none for
// a row that has neither.
std::optional<Species> ownerOf(std::optional<Species> named, std::optional<std::int32_t> place,
                               Species first) {
  if (named) {
    return named;
  }
  if (place) {
    return *place + first;
  }
  return std::nullopt;
}

Level levelFrom(std::int32_t level) {
  if (level < static_cast<std::int32_t>(Level::None) ||
      level > static_cast<std::int32_t>(Level::Ultimate)) {
    return Level::None;
  }
  return static_cast<Level>(level);
}

core::Result<const assets::AssetId*> tableOf(const ruleset::Ruleset& ruleset,
                                             std::string_view role) {
  const assets::AssetId* id = ruleset.table(role);
  if (id == nullptr) {
    return core::fail(messages::formatMessage(messages::NoRulesetTable, ruleset.id(), role));
  }
  return id;
}

// The item effects the ruleset names, or none when it names none (the ruleset store checks that a
// named table is in a pack).
core::Result<assets::ItemEffectTable> loadItemEffects(const assets::VirtualFileSystem& vfs,
                                                      const ruleset::Ruleset& ruleset) {
  const assets::AssetId* id = ruleset.table(ItemEffectsTableRole);
  if (id == nullptr) {
    return assets::ItemEffectTable{};
  }
  return assets::loadItemEffectTable(vfs, *id);
}

// Resolves the references of the asset tables to indices, keeping the first problem.
class TableResolver {
public:
  TableResolver(const assets::DigimonTable& digimon, const assets::MoveTable& moves)
      : digimon_(&digimon), moves_(&moves) {}

  // A Digimon reference: nothing for none and for a raw number outside the Digimon table.
  std::optional<Species> species(const assets::TableReference& reference, std::string_view where) {
    if (!reference) {
      return std::nullopt;
    }
    if (const assets::DigimonRow* entry = digimon_->find(*reference); entry != nullptr) {
      return entry->index;
    }
    if (const auto number = rawNumber(*reference); number) {
      return hasDigimonIndex(*digimon_, *number) ? number : std::nullopt;
    }
    fail(messages::formatMessage(messages::UnknownReference, where, messages::DigimonWhat,
                                 *reference));
    return std::nullopt;
  }

  // The move of a Digimon's slot, by id. A raw number the extractor wrote is the original's move
  // with that number. An id or a number the move table lacks is an error.
  std::optional<std::string> move(const assets::TableReference& reference,
                                  std::string_view digimon) {
    if (!reference) {
      return std::nullopt;
    }
    if (const assets::MoveEntry* entry = moves_->find(*reference); entry != nullptr) {
      return entry->id;
    }
    if (const auto number = rawNumber(*reference); number) {
      if (const assets::MoveEntry* entry = moves_->findByOriginalIndex(*number); entry != nullptr) {
        return entry->id;
      }
    }
    fail(messages::formatMessage(messages::UnknownMove, DigimonTableRole, digimon, *reference));
    return std::nullopt;
  }

  void fail(std::string problem) {
    if (!error_) {
      error_ = core::Error{std::move(problem)};
    }
  }

  [[nodiscard]] const std::optional<core::Error>& error() const { return error_; }

private:
  const assets::DigimonTable* digimon_;
  const assets::MoveTable* moves_;
  std::optional<core::Error> error_;
};

Requirement requirementFrom(const assets::EvolutionRequirementEntry& row, TableResolver& resolver) {
  return {.from = resolver.species(row.digimon, EvolutionTableRole),
          .minimum = {.hp = row.hp,
                      .mp = row.mp,
                      .offense = row.offense,
                      .defense = row.defense,
                      .speed = row.speed,
                      .brains = row.brain},
          .careMistakes = row.care,
          .weight = row.weight,
          .discipline = row.discipline,
          .happiness = row.happiness,
          .battles = row.battles,
          .learnedMoves = row.techs,
          .careIsMaximum = (row.flags & CareIsMaximumFlag) != 0,
          .battlesAreMaximum = (row.flags & BattlesAreMaximumFlag) != 0};
}

} // namespace

core::Result<EvolutionTables> EvolutionTables::build(const assets::DigimonTable& digimon,
                                                     const assets::RaiseTable& raise,
                                                     const assets::MoveTable& moves,
                                                     const assets::EvolutionTable& evolution,
                                                     const assets::ItemEffectTable& itemEffects) {
  TableResolver resolver{digimon, moves};
  EvolutionTables tables;
  tables.moves_ = moves;
  for (const assets::DigimonRow& entry : digimon.digimon) {
    SpeciesData data{.id = entry.id,
                     .level = levelFrom(entry.level),
                     .firstSpecialty = entry.special.front(),
                     .moves = {},
                     .defaultWeight = 0};
    for (std::size_t slot = 0; slot < entry.moves.size(); ++slot) {
      data.moves.at(slot) = resolver.move(entry.moves.at(slot), entry.id);
    }
    if (const assets::RaiseRow* raised = raise.find(entry.id); raised != nullptr) {
      data.defaultWeight = raised->defaultWeight;
    }
    tables.speciesById_.insert_or_assign(entry.id, entry.index);
    tables.species_.insert_or_assign(entry.index, std::move(data));
  }
  // A row belongs to the Digimon it names, so a pack's new Digimon finds its rows whatever index it
  // got; a row whose Digimon is a raw number outside the table keeps its place by index, as the
  // original's.
  for (const assets::EvolutionRequirementEntry& row : evolution.requirements) {
    if (const auto target =
            ownerOf(resolver.species(row.target, EvolutionTableRole), row.index, 0)) {
      tables.requirements_.insert_or_assign(*target, requirementFrom(row, resolver));
    }
  }
  for (const assets::EvolutionStatsGainEntry& row : evolution.statsGains) {
    const auto target = ownerOf(resolver.species(row.target, EvolutionTableRole), row.index, 0);
    if (const auto loads = resolver.species(row.targetDigimon, EvolutionTableRole);
        target && loads) {
      tables.statReferences_.insert_or_assign(*target,
                                              StatReference{.stats = {.hp = row.hp,
                                                                      .mp = row.mp,
                                                                      .offense = row.offense,
                                                                      .defense = row.defense,
                                                                      .speed = row.speed,
                                                                      .brains = row.brains},
                                                            .loads = *loads});
    }
  }
  // evolution.c: EVOLUTION_PATHS[currentDigimon - 1]: entry i is the chart of type i + 1.
  for (const assets::EvolutionPathEntry& row : evolution.paths) {
    std::vector<Species> targets;
    for (const assets::TableReference& target : row.to) {
      if (const auto species = resolver.species(target, EvolutionTableRole); species) {
        targets.push_back(*species);
      }
    }
    if (const auto owner =
            ownerOf(resolver.species(row.digimon, EvolutionTableRole), row.index, 1)) {
      tables.paths_.insert_or_assign(*owner, std::move(targets));
    }
  }
  // item.c: handleEvoItems: EVOLUTION_ITEM_TARGET, as dwx extracts it into the item effects.
  for (const assets::ItemEffectEntry& row : itemEffects.effects) {
    if (row.handler != assets::ItemHandler::Evolution) {
      continue;
    }
    // A pack's own item (a row with no original number) whose form the mounted species table lacks
    // leads nowhere: the game pack's rare items name forms of the base, and a base without them
    // still loads.
    if (row.index < 0 && row.evolvesTo && digimon.find(*row.evolvesTo) == nullptr) {
      core::logWarning("{}",
                       messages::formatMessage(messages::UnknownReference, ItemEffectsTableRole,
                                               messages::DigimonWhat, *row.evolvesTo));
      continue;
    }
    if (const auto target = resolver.species(row.evolvesTo, ItemEffectsTableRole); target) {
      tables.itemTargets_.insert_or_assign(row.item, *target);
    }
  }
  for (const assets::EvolutionItemTargetEntry& row : evolution.itemTargets) {
    const auto target = resolver.species(row.target, EvolutionTableRole);
    if (row.item && target) {
      tables.itemTargets_.insert_or_assign(*row.item, *target);
    }
  }
  if (resolver.error()) {
    return std::unexpected{*resolver.error()};
  }
  return tables;
}

core::Result<EvolutionTables> EvolutionTables::load(const assets::VirtualFileSystem& vfs,
                                                    const ruleset::Ruleset& ruleset) {
  auto digimonId = tableOf(ruleset, DigimonTableRole);
  auto raiseId = tableOf(ruleset, RaiseTableRole);
  auto movesId = tableOf(ruleset, MoveTableRole);
  auto evolutionId = tableOf(ruleset, EvolutionTableRole);
  for (const auto* id : {&digimonId, &raiseId, &movesId, &evolutionId}) {
    if (!*id) {
      return std::unexpected{id->error()};
    }
  }
  // The ruleset's species patches (the hidden content) go over the disc's tables.
  auto digimon = ruleset::loadPatchedDigimonTable(vfs, ruleset);
  if (!digimon) {
    return std::unexpected{digimon.error()};
  }
  auto raise = ruleset::loadPatchedRaiseTable(vfs, ruleset);
  if (!raise) {
    return std::unexpected{raise.error()};
  }
  auto moves = assets::loadMoveTable(vfs, **movesId);
  if (!moves) {
    return std::unexpected{moves.error()};
  }
  auto evolution = ruleset::loadPatchedEvolutionTable(vfs, ruleset);
  if (!evolution) {
    return std::unexpected{evolution.error()};
  }
  auto itemEffects = loadItemEffects(vfs, ruleset);
  if (!itemEffects) {
    return std::unexpected{itemEffects.error()};
  }
  auto tables = build(*digimon, *raise, *moves, *evolution, *itemEffects);
  if (!tables) {
    return tables;
  }
  auto extra = ruleset::readSpeciesExtra(ruleset);
  if (!extra) {
    return std::unexpected{extra.error()};
  }
  if (auto used = tables->useExtra(*extra); !used) {
    return std::unexpected{used.error()};
  }
  return tables;
}

core::Result<void> EvolutionTables::useExtra(const ruleset::SpeciesExtra& extra) {
  gates_.clear();
  megas_.clear();
  megaStep_ = extra.megaStep;
  const auto require = [&](std::string_view id) -> core::Result<Species> {
    if (const auto species = find(id)) {
      return *species;
    }
    return core::fail(
        messages::formatMessage(messages::UnknownForm, ruleset::SpeciesExtraSection, id));
  };
  for (const auto& [id, patch] : extra.species) {
    if (!patch.gate) {
      continue;
    }
    auto species = require(id);
    if (!species) {
      return std::unexpected{species.error()};
    }
    gates_.insert_or_assign(*species, *patch.gate);
  }
  if (megaStep_) {
    for (const std::string& id : megaStep_->mega) {
      auto species = require(id);
      if (!species) {
        return std::unexpected{species.error()};
      }
      megas_.insert(*species);
    }
  }
  return {};
}

const ruleset::SpeciesGate* EvolutionTables::gate(Species target) const {
  const auto found = gates_.find(target);
  return found == gates_.end() ? nullptr : &found->second;
}

const SpeciesData* EvolutionTables::species(Species species) const {
  const auto found = species_.find(species);
  return found == species_.end() ? nullptr : &found->second;
}

Level EvolutionTables::levelOf(Species species) const {
  const SpeciesData* data = this->species(species);
  return data == nullptr ? Level::None : data->level;
}

std::optional<Species> EvolutionTables::find(std::string_view id) const {
  const auto found = speciesById_.find(id);
  if (found == speciesById_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::string_view EvolutionTables::idOf(Species species) const {
  const SpeciesData* data = this->species(species);
  return data == nullptr ? std::string_view{} : std::string_view{data->id};
}

std::vector<Species> EvolutionTables::allSpecies() const {
  std::vector<Species> all;
  all.reserve(species_.size());
  for (const auto& entry : species_) {
    all.push_back(entry.first);
  }
  return all;
}

const Requirement* EvolutionTables::requirement(Species target) const {
  const auto found = requirements_.find(target);
  return found == requirements_.end() ? nullptr : &found->second;
}

const StatReference* EvolutionTables::statReference(Species target) const {
  const auto found = statReferences_.find(target);
  return found == statReferences_.end() ? nullptr : &found->second;
}

std::vector<Species> EvolutionTables::evolutionTargets() const {
  std::vector<Species> targets;
  for (const auto& [target, reference] : statReferences_) {
    if (levelOf(target) != Level::None && species(reference.loads) != nullptr) {
      targets.push_back(target);
    }
  }
  return targets;
}

std::span<const Species> EvolutionTables::pathTargets(Species from) const {
  const auto found = paths_.find(from);
  if (found == paths_.end()) {
    return {};
  }
  return found->second;
}

std::optional<Species> EvolutionTables::itemTarget(std::string_view item) const {
  const auto found = itemTargets_.find(item);
  if (found == itemTargets_.end()) {
    return std::nullopt;
  }
  return found->second;
}

} // namespace dwe::game::evolution
