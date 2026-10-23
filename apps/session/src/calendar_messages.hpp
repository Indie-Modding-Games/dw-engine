#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::session::calendar_messages {

inline constexpr std::string_view SectionText = "the calendar section";
inline constexpr std::string_view NewDayText = "new_day";
inline constexpr std::string_view MarksText = "a daily mark";
inline constexpr std::string_view NotAByte = "{} must be from 0 to 255";
inline constexpr std::string_view NotRead = "the calendar rules were not read";
inline constexpr std::string_view NewDayFailed = "calendar: a new day failed: {}";

} // namespace dwe::session::calendar_messages
