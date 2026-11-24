#include <dwe/game/battle/folder_formulas.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace dwe::game::battle::folder {

namespace {

constexpr std::int32_t Hundred = 100;

std::int64_t scaled(const FolderRules& rules, std::int32_t stat) {
  return std::clamp(stat, 0, rules.statScale);
}

// `base` shortened by up to `cutPercent` at full `stat`, rounded to the nearest frame.
std::int32_t shortened(const FolderRules& rules, std::int32_t base, std::int32_t cutPercent,
                       std::int32_t stat) {
  const std::int64_t scale = static_cast<std::int64_t>(rules.statScale) * Hundred;
  const std::int64_t kept = scale - (cutPercent * scaled(rules, stat));
  return static_cast<std::int32_t>(((base * kept) + (scale / 2)) / scale);
}

std::optional<std::int32_t> beaten(const FolderRules& rules, std::int32_t special) {
  if (special < 0 || std::cmp_greater_equal(special, rules.beats.size())) {
    return std::nullopt;
  }
  return rules.beats[static_cast<std::size_t>(special)];
}

// Vaccine beats Virus, Virus beats Data, Data beats Vaccine.
bool typeBeats(DigimonType attacker, DigimonType defender) {
  return (attacker == DigimonType::Vaccine && defender == DigimonType::Virus) ||
         (attacker == DigimonType::Virus && defender == DigimonType::Data) ||
         (attacker == DigimonType::Data && defender == DigimonType::Vaccine);
}

} // namespace

std::int32_t movementPercent(const FolderRules& rules, std::int32_t vel) {
  return rules.movementMinPercent + static_cast<std::int32_t>(rules.movementSpanPercent *
                                                              scaled(rules, vel) / rules.statScale);
}

std::int32_t executionFrames(const FolderRules& rules, std::int32_t base, std::int32_t vel) {
  return shortened(rules, base, rules.executionCutPercent, vel);
}

TechniqueTiming timingOf(const FolderRules& rules, const BattleMove& move) {
  if (const auto own = rules.techniques.find(move.id); own != rules.techniques.end()) {
    return own->second;
  }
  if (move.finisher) {
    return rules.finisherTiming;
  }
  for (const TimingTier& tier : rules.timingTiers) {
    if (move.mpCost <= tier.mpUpTo) {
      return tier.timing;
    }
  }
  return {.preparationFrames = rules.preparationFrames, .recoveryFrames = rules.recoveryFrames};
}

std::int32_t processingFrames(const FolderRules& rules, std::int32_t base,
                              std::int32_t intelligence) {
  return shortened(rules, base, rules.processingCutPercent, intelligence);
}

std::int32_t elementPercent(const FolderRules& rules, std::int32_t moveSpecial,
                            const Specials& attacker, const Specials& defender) {
  std::int32_t percent = 0;
  const std::optional<std::int32_t> moveBeats = beaten(rules, moveSpecial);
  for (const std::optional<std::int32_t>& special : defender) {
    if (!special) {
      continue;
    }
    if (moveBeats == *special) {
      percent += rules.elementPercent;
    }
    if (beaten(rules, *special) == moveSpecial && moveBeats != *special) {
      percent -= rules.elementPercent;
    }
  }
  if (std::ranges::contains(attacker, std::optional<std::int32_t>{moveSpecial})) {
    percent += rules.stabPercent;
  }
  return percent;
}

std::int32_t typePercent(const FolderRules& rules, DigimonType attacker, DigimonType defender) {
  if (typeBeats(attacker, defender)) {
    return rules.typePercent;
  }
  if (typeBeats(defender, attacker)) {
    return -rules.typePercent;
  }
  return 0;
}

std::int32_t damage(const FolderRules& rules, const DamageInputs& inputs) {
  const std::int64_t attackModifier =
      static_cast<std::int64_t>(inputs.attack) * rules.attackPercent / Hundred;
  const std::int64_t modifiers =
      static_cast<std::int64_t>(inputs.power) * inputs.modifierPercent / Hundred;
  const std::int64_t times = inputs.covered ? rules.coverDefenseTimes : 1;
  const std::int64_t total = inputs.power + attackModifier + modifiers -
                             (static_cast<std::int64_t>(inputs.defense) * times);
  const std::int64_t floor = inputs.covered ? 0 : 1;
  return static_cast<std::int32_t>(std::max(total, floor));
}

std::int32_t coverPercent(const FolderRules& rules, std::int32_t defense) {
  return static_cast<std::int32_t>(rules.coverMaxPercent * scaled(rules, defense) /
                                   rules.statScale);
}

std::int32_t barCapacity(const FolderRules& rules, std::int32_t defense) {
  return std::max(rules.barDefenseTimes * std::max(defense, 0), 1);
}

std::int32_t statCutPercent(const FolderRules& rules, std::int32_t vel, std::int32_t defense) {
  const std::int64_t cut = (rules.perStatCutPercent * scaled(rules, vel) / rules.statScale) +
                           (rules.perStatCutPercent * scaled(rules, defense) / rules.statScale);
  return static_cast<std::int32_t>(std::min<std::int64_t>(cut, rules.statCutCapPercent));
}

std::int32_t breakFrames(const FolderRules& rules, std::int32_t vel, std::int32_t defense) {
  return rules.breakFrames * (Hundred - statCutPercent(rules, vel, defense)) / Hundred;
}

std::int32_t barDrain(const FolderRules& rules, std::int32_t capacity, std::int32_t vel,
                      std::int32_t defense) {
  const std::int64_t frames =
      std::max<std::int64_t>(static_cast<std::int64_t>(rules.barDrainFrames) *
                                 (Hundred - statCutPercent(rules, vel, defense)) / Hundred,
                             1);
  return static_cast<std::int32_t>((capacity + frames - 1) / frames);
}

std::int32_t brokenStat(const FolderRules& rules, std::int32_t stat) {
  return stat * (Hundred - rules.breakDebuffPercent) / Hundred;
}

} // namespace dwe::game::battle::folder
