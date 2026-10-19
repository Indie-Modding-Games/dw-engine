#pragma once

#include <expected>
#include <string>
#include <utility>

namespace dwe::core {

// A recoverable failure, described so that whoever reads the log can act on it.
struct Error {
    std::string message;
};

// The value of an operation that can fail.
template <typename T>
using Result = std::expected<T, Error>;

// Builds the failure of a Result.
inline std::unexpected<Error> fail(std::string message) {
    return std::unexpected<Error>{Error{std::move(message)}};
}

} // namespace dwe::core
