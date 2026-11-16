#pragma once

#include <string_view>

namespace dwe::game::shop_job::messages {

// The error messages of the shop job module, as std::format templates.

// What is being read.
inline constexpr std::string_view SectionText = "the shop_job section";
inline constexpr std::string_view SpeciesText = "a customer species";
inline constexpr std::string_view ItemText = "an item";
inline constexpr std::string_view MoodText = "a mood";
inline constexpr std::string_view ReactionText = "a reaction";
inline constexpr std::string_view TalkText = "talk";
inline constexpr std::string_view PerfectScoreText = "perfect_score";
inline constexpr std::string_view BonusText = "bonus";

// {}: the rules file.
inline constexpr std::string_view BagsDiffer =
    "{}: the species and the items do not make the same number of customers";
inline constexpr std::string_view EmptyBag = "{}: there are no customers";
inline constexpr std::string_view BadReactions =
    "{}: the reactions are not delighted, pleased, doubtful, annoyed and furious in that order";
inline constexpr std::string_view BadAcceptance =
    "{}: an acceptance table is not 0 to 100 and never lower at a cheaper level";
inline constexpr std::string_view BadLevel = "{}: the regular level is not a price level";
inline constexpr std::string_view BadMoods = "{}: the moods' weights are not positive";
inline constexpr std::string_view BadWindow = "{}: the perfect score window is empty";

} // namespace dwe::game::shop_job::messages
