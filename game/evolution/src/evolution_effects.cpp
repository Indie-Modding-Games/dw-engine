#include "evolution_effects.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace dwe::game::evolution {

namespace {

// The halves of evl.c: EVL_scaleBaseStats' rebuild from the virus backup and of the average with
// the reference.
constexpr std::int32_t Half = 2;

// A byte read as signed: its low 8 bits in two's complement.
constexpr std::int32_t ByteMask = 0xFF;
constexpr std::int32_t ByteSignBit = 0x80;
constexpr std::int32_t ByteRange = 0x100;

std::int32_t signedByte(std::int32_t value) {
  const std::int32_t byte = value & ByteMask;
  return byte >= ByteSignBit ? byte - ByteRange : byte;
}

// How much each stat goes from `before` to `after`.
BaseStats changeOf(const BaseStats& before, const BaseStats& after) {
  return {.hp = after.hp - before.hp,
          .mp = after.mp - before.mp,
          .offense = after.offense - before.offense,
          .defense = after.defense - before.defense,
          .speed = after.speed - before.speed,
          .brains = after.brains - before.brains};
}

// The slot that holds the basic attack, first of every move list.
constexpr std::int32_t BasicSlot = 0;

std::int32_t rebuiltStat(std::int32_t stat, std::int32_t kept) {
  return kept + ((stat - (kept / Half)) / Half);
}

} // namespace

EvolutionResult EvolutionEffects::apply(const EvolutionInputs& inputs, const EvolutionState& state,
                                        Species target, const StatReference& reference,
                                        EvolutionReason reason) const {
  const Species form = reference.loads;
  const SpeciesData& formData = *tables_->species(form);
  const bool suppressed = gainsSuppressed(state, reason);
  const bool scales = scalesStats(inputs, target);
  EvolutionResult result{.target = form,
                         .reason = reason,
                         .statChange = {},
                         .restoredStats = std::nullopt,
                         .weight = formData.defaultWeight,
                         .careMistakes = 0,
                         .battles = 0,
                         .evolutionTimer = inputs.evolutionTimer,
                         .lifetimeBonusHours = 0,
                         .tamerLevelChance = 0,
                         .learnedMove = learnedMove(formData),
                         .equippedMoves = {},
                         .restartsVirusBar = reason == EvolutionReason::Virus,
                         .virusBackup = std::nullopt,
                         .evolutionItemUsed = false};
  if (!suppressed && inVirusForm(inputs)) {
    result.restoredStats = rebuiltStats(inputs);
    result.restartsVirusBar = true;
  } else if (scales && !suppressed) {
    result.statChange = changeOf(inputs.stats, scaledStats(inputs.stats, reference));
  } else if (!scales && !suppressed) {
    result.statChange = changeOf(inputs.stats, gainedStats(inputs.stats, reference.stats));
    result.tamerLevelChance = inputs.scriptPaused ? tamerLevelChance(formData.level) : 0;
  }
  if (formData.level == Level::Ultimate && !suppressed) {
    result.lifetimeBonusHours = rules_->statGains.ultimateLifetimeHours;
  }
  result.equippedMoves = equippedMoves(formData, result.learnedMove);
  if (tables_->levelOf(inputs.species) != formData.level) {
    result.evolutionTimer = 0;
  }
  if (reason == EvolutionReason::Virus) {
    result.virusBackup = VirusBackup{.species = inputs.species, .stats = inputs.stats};
  }
  result.evolutionItemUsed = rules_->quirks.on(EvolutionItemSticks) &&
                             (state.evolutionItemUsed || reason == EvolutionReason::Item);
  return result;
}

EvolutionResult EvolutionEffects::rebirth(const EvolutionInputs& inputs, Species baby) {
  EvolutionResult result{.target = baby,
                         .reason = EvolutionReason::Rebirth,
                         .statChange = {},
                         .restoredStats = std::nullopt,
                         .weight = inputs.weight,
                         .careMistakes = inputs.careMistakes,
                         .battles = inputs.battles,
                         .evolutionTimer = inputs.evolutionTimer,
                         .lifetimeBonusHours = 0,
                         .tamerLevelChance = 0,
                         .learnedMove = std::nullopt,
                         .equippedMoves = {},
                         .restartsVirusBar = false,
                         .virusBackup = std::nullopt,
                         .evolutionItemUsed = false};
  result.equippedMoves.front() = BasicSlot;
  return result;
}

bool EvolutionEffects::gainsSuppressed(const EvolutionState& state, EvolutionReason reason) const {
  return reason == EvolutionReason::Item ||
         (rules_->quirks.on(EvolutionItemSticks) && state.evolutionItemUsed);
}

bool EvolutionEffects::scalesStats(const EvolutionInputs& inputs, Species target) const {
  const bool listed =
      std::ranges::any_of(rules_->statGains.scaledTargets,
                          [&](const std::string& id) { return tables_->find(id) == target; });
  const bool lowLevel =
      static_cast<std::int32_t>(tables_->levelOf(target)) < rules_->statGains.scaledBelowLevel;
  return listed || lowLevel || inVirusForm(inputs);
}

// evl.c: EVL_scaleBaseStats: back from the virus form, each stat is rebuilt from the one kept on
// entering it.
BaseStats EvolutionEffects::rebuiltStats(const EvolutionInputs& inputs) {
  const BaseStats& stats = inputs.stats;
  const BaseStats kept = inputs.virusBackup ? inputs.virusBackup->stats : BaseStats{};
  return {.hp = rebuiltStat(stats.hp, kept.hp),
          .mp = rebuiltStat(stats.mp, kept.mp),
          .offense = rebuiltStat(stats.offense, kept.offense),
          .defense = rebuiltStat(stats.defense, kept.defense),
          .speed = rebuiltStat(stats.speed, kept.speed),
          .brains = rebuiltStat(stats.brains, kept.brains)};
}

BaseStats EvolutionEffects::scaledStats(const BaseStats& stats,
                                        const StatReference& reference) const {
  // evl.c: EVL_applyEvolution passes the brains field of the row, read as a signed byte, as the
  // factor.
  const std::int32_t factor = signedByte(reference.stats.brains);
  const std::int32_t divisor = rules_->statGains.scaleDivisor;
  return {.hp = stats.hp * factor / divisor,
          .mp = stats.mp * factor / divisor,
          .offense = stats.offense * factor / divisor,
          .defense = stats.defense * factor / divisor,
          .speed = stats.speed * factor / divisor,
          .brains = stats.brains * factor / divisor};
}

BaseStats EvolutionEffects::gainedStats(const BaseStats& stats, const BaseStats& reference) const {
  return {.hp = gainedStat(stats.hp, reference.hp),
          .mp = gainedStat(stats.mp, reference.mp),
          .offense = gainedStat(stats.offense, reference.offense),
          .defense = gainedStat(stats.defense, reference.defense),
          .speed = gainedStat(stats.speed, reference.speed),
          .brains = gainedStat(stats.brains, reference.brains)};
}

// A stat that reaches the reference adds a tenth of it; one below becomes the average of the two.
std::int32_t EvolutionEffects::gainedStat(std::int32_t stat, std::int32_t reference) const {
  if (stat >= reference) {
    return stat + (reference / rules_->statGains.gainDivisor);
  }
  return (reference + stat) / Half;
}

std::int32_t EvolutionEffects::tamerLevelChance(Level level) const {
  switch (level) {
  case Level::Champion:
    return rules_->statGains.championTamerLevelChance;
  case Level::Ultimate:
    return rules_->statGains.ultimateTamerLevelChance;
  case Level::None:
  case Level::Baby:
  case Level::InTraining:
  case Level::Rookie:
    return 0;
  }
  return 0;
}

bool EvolutionEffects::inVirusForm(const EvolutionInputs& inputs) {
  return inputs.virusForm && inputs.species == *inputs.virusForm;
}

// evl.c: EVL_applyEvolution: among the moves of the form's list in its first specialty's block, the
// first with the lowest power other than 0. A form with no move in its block learns nothing: the
// original passes the list's end marker to learnMove, as for the in-training forms, whose only move
// is outside every block. A form with no specialty also learns nothing (the original would read an
// unset block; its tables have no such evolution target).
std::optional<std::string> EvolutionEffects::learnedMove(const SpeciesData& form) const {
  const std::optional<assets::EntryClass> block =
      form.firstSpecialty ? rules_->moves.specialtyBlock(*form.firstSpecialty) : std::nullopt;
  if (!block) {
    return std::nullopt;
  }
  const assets::MoveEntry* best = nullptr;
  for (const auto& move : form.moves) {
    const assets::MoveEntry* entry = move ? tables_->moves().find(*move) : nullptr;
    if (entry == nullptr || !block->contains(*entry)) {
      continue;
    }
    if (best == nullptr || (best->power > entry->power && entry->power != 0)) {
      best = entry;
    }
  }
  return best == nullptr ? std::nullopt : std::optional<std::string>{best->id};
}

// The first slot holds the learned move (the basic attack below the basic slot level), the fourth
// the last move of the list in the finisher range, and the others are emptied.
EquippedMoves EvolutionEffects::equippedMoves(const SpeciesData& form,
                                              const std::optional<std::string>& learned) const {
  EquippedMoves equipped{};
  for (std::size_t slot = 0; learned && slot < form.moves.size(); ++slot) {
    if (form.moves.at(slot) == learned) {
      equipped.front() = static_cast<std::int32_t>(slot);
      break;
    }
  }
  if (static_cast<std::int32_t>(form.level) < rules_->moves.basicSlotBelowLevel) {
    equipped.front() = BasicSlot;
  }
  for (std::size_t slot = form.moves.size(); slot > 0; --slot) {
    const std::optional<std::string>& move = form.moves.at(slot - 1);
    const assets::MoveEntry* entry = move ? tables_->moves().find(*move) : nullptr;
    if (entry != nullptr && rules_->moves.finishers.contains(*entry)) {
      equipped.back() = static_cast<std::int32_t>(slot - 1);
      break;
    }
  }
  return equipped;
}

} // namespace dwe::game::evolution
