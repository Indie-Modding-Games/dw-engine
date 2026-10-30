#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

// The messages of the city's prosperity and of the medals.
namespace dwe::session::progress_messages {

inline constexpr std::string_view CitySectionText = "the city section";
inline constexpr std::string_view ProsperityText = "prosperity";
inline constexpr std::string_view ClearedMapText = "a cleared_map choice";
inline constexpr std::string_view ProsperityFailed = "city: the prosperity was not rebuilt: {}";
inline constexpr std::string_view ClearedMapFailed = "city: {}";

inline constexpr std::string_view MedalsSectionText = "the medals section";
inline constexpr std::string_view ConditionsText = "the medal conditions";
inline constexpr std::string_view ConditionText = "the medal condition {}";
inline constexpr std::string_view SoundText = "award_sound";
inline constexpr std::string_view MedalCheckFailed = "medals: the check failed: {}";

} // namespace dwe::session::progress_messages
