#include <dwe/game/tournament/tournament.hpp>

#include <algorithm>
#include <format>
#include <iterator>
#include <utility>

namespace dwe::game::tournament {

namespace {

// The marks that end a list in the script's data (0xFE, 0xFF).
constexpr std::uint8_t ListEnd = 0xFE;
// The move the original takes for a rival as an extra "any range" pick (STD_initializeOpponent).
constexpr std::int32_t WideMove = 0x2D;
constexpr std::int32_t WideRange = 4;
// The one-in-ten roll for a wide move as the finisher, which the finisher search overwrites.
constexpr std::int32_t WideFinisherRoll = 10;
constexpr std::int32_t Percent = 100;

std::int32_t winChance(const TournamentRules& rules, std::int32_t first, std::int32_t second) {
  const auto row = static_cast<std::size_t>(std::max(first - rules.firstLevel, 0));
  const auto column = static_cast<std::size_t>(std::max(second - rules.firstLevel, 0));
  if (row >= rules.winChances.size() || column >= rules.winChances[row].size()) {
    return Percent / 2;
  }
  return rules.winChances[row][column];
}

// STD_setupParticipants: whether the first of two slots wins their match.
bool firstWins(const TournamentRules& rules, const Bracket& bracket, std::size_t first,
               std::size_t second, const std::function<std::int32_t(std::int32_t)>& levelOf,
               ruleset::Random& random) {
  return random.below(Percent) < winChance(rules, levelOf(bracket.entrants.at(first)),
                                           levelOf(bracket.entrants.at(second)));
}

std::size_t below(ruleset::Random& random, std::size_t limit) {
  return static_cast<std::size_t>(random.below(static_cast<std::int32_t>(limit)));
}

// STD_initializeOpponent: a rival's stat, base +- the spread, scaled by its level.
std::int32_t rolledStat(const TournamentRules& rules, std::int32_t base, const LevelScale& scale,
                        ruleset::Random& random) {
  const std::int32_t swing = rules.statSpread - random.below((rules.statSpread * 2) + 1);
  return scale.multiply * (base + (base * swing / Percent)) / scale.divide;
}

} // namespace

std::vector<std::int32_t> cupList(std::span<const std::uint8_t> bytes) {
  std::vector<std::int32_t> species;
  for (const std::uint8_t byte : bytes) {
    if (byte >= ListEnd) {
      break;
    }
    species.push_back(byte);
  }
  return species;
}

core::Result<std::array<std::int32_t, Rivals>>
drawRivals(const TournamentRules& rules, std::int32_t cup, const CupData& data,
           const std::function<bool(std::int32_t)>& resident, std::int32_t partnerSpecies,
           ruleset::Random& random) {
  std::vector<std::int32_t> pool;
  if (data.rivals.empty()) {
    for (std::int32_t species = 0; species < rules.speciesCount; ++species) {
      if (resident(species)) {
        pool.push_back(species);
      }
    }
  } else if (cup == rules.wholeListCup) {
    pool = data.rivals;
  } else {
    std::ranges::copy_if(data.rivals, std::back_inserter(pool), resident);
  }
  if (pool.empty()) {
    return core::fail(std::format("cup {} has no rival to draw", cup));
  }
  std::array<std::int32_t, Rivals> rivals{};
  if (pool.size() <= Rivals) {
    // Fewer than seven: the pool as it is, then repeats drawn from it.
    const std::size_t count = pool.size();
    for (std::size_t slot = 0; slot < Rivals; ++slot) {
      rivals.at(slot) = slot < count ? pool[slot] : pool[below(random, count)];
    }
    return rivals;
  }
  // More: not the partner's species (its first place), then seven different ones.
  if (const auto partner = std::ranges::find(pool, partnerSpecies); partner != pool.end()) {
    pool.erase(partner);
  }
  for (std::int32_t& rival : rivals) {
    const std::size_t pick = below(random, pool.size());
    rival = pool[pick];
    pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(pick));
  }
  return rivals;
}

std::int32_t standIn(const TournamentRules& rules, std::int32_t species) {
  const auto found =
      std::ranges::find(rules.standIns, species, &std::pair<std::int32_t, std::int32_t>::first);
  return found != rules.standIns.end() ? found->second : species;
}

Bracket drawBracket(const TournamentRules& rules, std::array<std::int32_t, Rivals> rivals,
                    std::int32_t partnerSpecies,
                    const std::function<std::int32_t(std::int32_t)>& levelOf,
                    ruleset::Random& random) {
  for (std::int32_t& rival : rivals) {
    rival = standIn(rules, rival);
  }
  Bracket bracket;
  bracket.playerSlot = below(random, Entrants);
  bracket.entrants.at(bracket.playerSlot) = partnerSpecies;
  // Each rival trades places with a random one (list[1..7], swapByte).
  for (std::size_t slot = 0; slot < Rivals; ++slot) {
    std::swap(rivals.at(slot), rivals.at(below(random, Rivals)));
  }
  std::size_t next = 0;
  for (std::size_t slot = 0; slot < Entrants; ++slot) {
    if (slot != bracket.playerSlot) {
      bracket.entrants.at(slot) = rivals.at(next++);
    }
  }
  const auto match = [&](std::size_t first, std::size_t second) {
    return firstWins(rules, bracket, first, second, levelOf, random) ? first : second;
  };
  // Every first-round match is rolled, the partner's too (its result is not used).
  for (std::size_t pair = 0; pair < Entrants / 2; ++pair) {
    bracket.firstRound.at(pair) = match(pair * 2, (pair * 2) + 1);
  }
  const std::array<std::size_t, 4>& first = bracket.firstRound;
  if (bracket.playerSlot < Entrants / 2) {
    bracket.secondRound[0] = bracket.playerSlot < 2 ? first[1] : first[0];
    bracket.secondRound[1] = match(first[2], first[3]);
  } else {
    bracket.secondRound[0] = match(first[0], first[1]);
    bracket.secondRound[1] = bracket.playerSlot < 6 ? first[3] : first[2];
  }
  const std::size_t neighbour =
      bracket.playerSlot % 2 == 0 ? bracket.playerSlot + 1 : bracket.playerSlot - 1;
  bracket.opponents[0] = bracket.entrants.at(neighbour);
  // The semi-final: the other pair of the partner's quarter; the final: the other half.
  static constexpr std::array<std::size_t, 4> SemiPair{1, 0, 3, 2};
  static constexpr std::array<std::size_t, 4> FinalHalf{1, 1, 0, 0};
  const std::size_t quarter = bracket.playerSlot / 2;
  bracket.opponents[1] = bracket.entrants.at(first.at(SemiPair.at(quarter)));
  bracket.opponents[2] = bracket.entrants.at(bracket.secondRound.at(FinalHalf.at(quarter)));
  return bracket;
}

Rival makeRival(const TournamentRules& rules, std::int32_t cup, const RivalSpecies& species,
                ruleset::Random& random) {
  const std::int32_t base = cup >= 0 && std::cmp_less(cup, rules.opponentBaseStats.size())
                                ? rules.opponentBaseStats[static_cast<std::size_t>(cup)]
                                : 0;
  const auto found = std::ranges::find(rules.levels, species.level, &LevelScale::level);
  const LevelScale scale = found != rules.levels.end() ? *found : LevelScale{};
  Rival rival;
  rival.offense = std::min(rolledStat(rules, base, scale, random), rules.statCap);
  rival.defense = std::min(rolledStat(rules, base, scale, random), rules.statCap);
  rival.speed = std::min(rolledStat(rules, base, scale, random), rules.statCap);
  rival.brain = std::min(rolledStat(rules, base, scale, random), rules.statCap);
  const std::int32_t vitality = base * rules.vitalityFactor;
  rival.hp = std::min(rolledStat(rules, vitality, scale, random), rules.vitalityCap);
  rival.mp = std::min(rolledStat(rules, vitality, scale, random), rules.vitalityCap);

  const auto finisher = [&](std::int32_t number) {
    return number >= rules.finishers[0] && number < rules.finishers[1];
  };
  std::vector<std::size_t> wide;
  std::vector<std::size_t> usable;
  const auto slots = std::min(static_cast<std::size_t>(rules.moveSlots), species.moves.size());
  for (std::size_t slot = 0; slot < slots; ++slot) {
    const std::optional<SlotMove>& move = species.moves.at(slot);
    if (!move || finisher(move->number) ||
        rival.mp / rules.mpDivisor < move->mpCost * rules.mpCostFactor) {
      continue;
    }
    if (move->range == WideRange || move->number == WideMove) {
      wide.push_back(slot);
    }
    usable.push_back(slot);
  }
  if (usable.empty()) {
    rival.slots[0] = 0;
  } else {
    for (std::size_t index = 0; index < usable.size(); ++index) {
      std::swap(usable[index], usable[below(random, usable.size())]);
    }
    for (std::size_t place = 0; place < 3 && place < usable.size(); ++place) {
      rival.slots.at(place) = usable[place];
    }
  }
  // The wide moves are shuffled and one in ten would be the finisher, but the finisher search
  // below always sets the fourth slot again: the draws are spent all the same.
  if (!wide.empty()) {
    for (std::size_t index = 0; index < wide.size(); ++index) {
      std::swap(wide[index], wide[below(random, wide.size())]);
    }
    static_cast<void>(random.below(WideFinisherRoll));
  }
  for (std::size_t slot = 0; slot < species.moves.size(); ++slot) {
    if (species.moves.at(slot) && finisher(species.moves.at(slot)->number)) {
      rival.slots[3] = slot;
      break;
    }
  }
  return rival;
}

std::int32_t winsNeeded(const TournamentRules& rules, std::int32_t cup) {
  const auto found =
      std::ranges::find(rules.cupWinsNeeded, cup, &std::pair<std::int32_t, std::int32_t>::first);
  return found != rules.cupWinsNeeded.end() ? found->second : rules.winsNeeded;
}

} // namespace dwe::game::tournament
