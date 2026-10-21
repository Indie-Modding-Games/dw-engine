#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <string_view>

namespace dwe::script::detail {

// A whole number as text, in a fixed buffer: error messages that a Lua error raises cannot hold a std::string, whose
// destructor longjmp would skip.
class NumberText {
public:
    explicit NumberText(long long number) {
        char* const first = digits_.data();
        length_ = static_cast<std::size_t>(std::to_chars(first, first + digits_.size(), number).ptr - first);
    }

    [[nodiscard]] std::string_view text() const { return {digits_.data(), length_}; }

private:
    static constexpr std::size_t MaxDigits = 24;

    std::array<char, MaxDigits> digits_{};
    std::size_t length_ = 0;
};

} // namespace dwe::script::detail
