#pragma once

#include <string_view>

// The log and error messages of ui. Templates take their arguments in the order of their placeholders.
namespace dwe::ui::messages {

inline constexpr std::string_view NoTexts = "cannot read the texts of the packs: {}";

} // namespace dwe::ui::messages
