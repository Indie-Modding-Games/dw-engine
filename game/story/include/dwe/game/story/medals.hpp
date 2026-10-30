#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/story_state.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// The medals the player earns while walking the field (tamer.c: checkMedalConditions): each frame
// the player walks freely, every medal not held yet whose condition holds is given
// (script_common.c: unlockMedal sets trigger firstTrigger + medal) and a notice is due
// (MEDAL_AWARD_PENDING). The tournaments' medals are the story's (tournament.c:
// checkTournamentMedalConditions).
namespace dwe::game::story {

// The partner's stats a medal asks for, as the original compares them.
struct MedalStats {
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brain = 0;

  friend bool operator==(const MedalStats&, const MedalStats&) = default;
};

// Each condition's medal and its numbers; a condition without a value is not checked.
struct MedalRules {
  // Medal n is trigger firstTrigger + n, for n below medalLimit (unlockMedal).
  std::int32_t firstTrigger = 0;
  std::int32_t medalLimit = 0;
  // Every move with a number below `count` learned (hasMove).
  struct AllMoves {
    std::int32_t medal = 0;
    std::int32_t count = 0;
  };
  // The partner's stats exactly these.
  struct MaxStats {
    std::int32_t medal = 0;
    MedalStats stats;
  };
  // The money exactly this.
  struct Money {
    std::int32_t medal = 0;
    std::int64_t amount = 0;
  };
  // The calendar's year exactly this.
  struct Year {
    std::int32_t medal = 0;
    std::int32_t year = 0;
  };
  // Every species from `first` up to `end` (not included) raised (hasDigimonRaised); it also adds
  // tamerLevels to the tamer's level, with no notice of its own.
  struct AllRaised {
    std::int32_t medal = 0;
    std::int32_t first = 0;
    std::int32_t end = 0;
    std::int32_t tamerLevels = 0;
  };
  // At least `count` fish caught.
  struct Fish {
    std::int32_t medal = 0;
    std::int32_t count = 0;
  };
  // At least one copy of every card with a number below `count` (getCardAmount).
  struct AllCards {
    std::int32_t medal = 0;
    std::int32_t count = 0;
  };
  // In the order the original checks them.
  std::optional<AllMoves> allMoves;
  std::optional<MaxStats> maxStats;
  std::optional<Money> money;
  std::optional<Year> year;
  std::optional<AllRaised> allRaised;
  std::optional<Fish> fish;
  std::optional<AllCards> allCards;
};

// What the conditions read from the rest of the game.
struct MedalFacts {
  std::function<bool(std::int32_t move)> knowsMove;
  MedalStats stats;
  std::int64_t money = 0;
  std::int32_t year = 0;
  std::function<bool(std::int32_t species)> raised;
  std::int32_t fishCaught = 0;
  std::function<std::int32_t(std::int32_t card)> cardCopies;
};

// The medals one check gave, and the tamer levels they add.
struct EarnedMedals {
  std::vector<std::int32_t> medals;
  std::int32_t tamerLevels = 0;
};

// Whether the player holds a medal (script_common.c: hasMedal); a medal from medalLimit up is never
// held.
[[nodiscard]] core::Result<bool> hasMedal(const StoryState& state, const MedalRules& rules,
                                          std::int32_t medal);
// Gives a medal (script_common.c: unlockMedal); one from medalLimit up changes nothing.
core::Result<void> unlockMedal(StoryState& state, const MedalRules& rules, std::int32_t medal);

// One frame's check (tamer.c: checkMedalConditions): gives each medal whose condition now holds and
// returns them.
core::Result<EarnedMedals> checkMedalConditions(StoryState& state, const MedalRules& rules,
                                                const MedalFacts& facts);

} // namespace dwe::game::story
