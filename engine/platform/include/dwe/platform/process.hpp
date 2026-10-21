#pragma once

#include <dwe/core/error.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace dwe::platform {

// How a program that ran to its end finished, and what it wrote to its standard output.
struct ProcessResult {
    int exitCode = 0;
    std::string output;
};

// Runs a program and waits for it to end. arguments[0] is the program and the rest are its arguments, given to it as
// they are, never through a shell, all in UTF-8. Its standard output is captured; what it writes to its standard error
// goes to this program's, and it reads nothing. It does not need a Platform.
core::Result<ProcessResult> runProcess(std::span<const std::string> arguments);

// The file name of a program on this system: its name, with the extension the system gives programs (.exe on Windows).
[[nodiscard]] std::string executableFileName(std::string_view program);

// The folder that holds the running executable, with no separator at its end. It does not need a Platform.
core::Result<std::filesystem::path> executableFolder();

} // namespace dwe::platform
