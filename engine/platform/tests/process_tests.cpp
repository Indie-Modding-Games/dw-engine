#include <dwe/platform/process.hpp>

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <string>

namespace dwe::platform {
namespace {

// A program and its arguments are UTF-8, as on every system the project builds for.
std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

} // namespace

TEST_CASE("a process runs with its arguments as they are and gives back its output and exit code") {
    // Spaces, quotes and a shell's special characters reach the program untouched: no shell is in between.
    const std::array<std::string, 5> arguments{DWE_PROCESS_PROBE, "3", "two words", "\"quoted\"", "$HOME; exit 9"};
    const auto result = runProcess(arguments);
    REQUIRE_MESSAGE(result.has_value(), result.error().message);
    CHECK(result->exitCode == 3);
    CHECK(result->output == "two words\n\"quoted\"\n$HOME; exit 9\n");
}

TEST_CASE("a process that ends well gives exit code 0") {
    const std::array<std::string, 1> arguments{DWE_PROCESS_PROBE};
    const auto result = runProcess(arguments);
    REQUIRE_MESSAGE(result.has_value(), result.error().message);
    CHECK(result->exitCode == 0);
    CHECK(result->output.empty());
}

TEST_CASE("running nothing, or a program that does not exist, is an error") {
    CHECK_FALSE(runProcess({}).has_value());
    const std::array<std::string, 1> missing{
        utf8(std::filesystem::path{DWE_PROCESS_PROBE}.parent_path() / "no-such-program")};
    CHECK_FALSE(runProcess(missing).has_value());
}

TEST_CASE("a program's file name has the system's extension for programs") {
    CHECK(std::filesystem::path{DWE_PROCESS_PROBE}.filename() == executableFileName("dwe_process_probe"));
}

// The probe is built next to the tests' executable.
TEST_CASE("the executable's folder is the folder of the running tests") {
    const auto folder = executableFolder();
    REQUIRE_MESSAGE(folder.has_value(), folder.error().message);
    CHECK(std::filesystem::equivalent(*folder, std::filesystem::path{DWE_PROCESS_PROBE}.parent_path()));
    // No separator at the end: the folder's parent is the folder above it.
    CHECK(folder->filename() == std::filesystem::path{DWE_PROCESS_PROBE}.parent_path().filename());
}

} // namespace dwe::platform
