#pragma once

#include <string>
#include <string_view>

namespace dwe::localization {

[[nodiscard]] std::string upperCase(std::string_view text);

[[nodiscard]] std::string lowerCase(std::string_view text);

} // namespace dwe::localization
