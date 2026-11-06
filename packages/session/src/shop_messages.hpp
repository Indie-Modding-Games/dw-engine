#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

// The error messages of the shop counter, as std::format templates for core::formatMessage.
namespace dwe::session::messages {

using core::formatMessage;

inline constexpr std::string_view ShopSectionText = "the shop section";
inline constexpr std::string_view ShopLinesText = "the shop section's lines";
inline constexpr std::string_view MapHeadListingUnread =
    "{} has no sections and instructions to read the shopkeepers' lines from";
inline constexpr std::string_view ShopClosed = "no shop is open";
inline constexpr std::string_view ShopAmountOutside = "cannot trade {} of {}: the most is {}";

} // namespace dwe::session::messages
