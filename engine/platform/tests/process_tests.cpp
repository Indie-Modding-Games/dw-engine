#include <dwe/platform/process.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace dwe::platform {
namespace {

// A program and its arguments are UTF-8, as on every system the project builds for.
std::string utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return {text.begin(), text.end()};
}

} // namespace

TEST_CASE("a process runs with its arguments as they are and gives back its output and exit code") {
  // Spaces, quotes and a shell's special characters reach the program untouched: no shell is in
  // between.
  const std::array<std::string, 5> arguments{DWE_PROCESS_PROBE, "3", "two words", "\"quoted\"",
                                             "$HOME; exit 9"};
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
  CHECK(std::filesystem::path{DWE_PROCESS_PROBE}.filename() ==
        executableFileName("dwe_process_probe"));
}

// The probe is built next to the tests' executable.
TEST_CASE("the executable's folder is the folder of the running tests") {
  const auto folder = executableFolder();
  REQUIRE_MESSAGE(folder.has_value(), folder.error().message);
  CHECK(
      std::filesystem::equivalent(*folder, std::filesystem::path{DWE_PROCESS_PROBE}.parent_path()));
  // No separator at the end: the folder's parent is the folder above it.
  CHECK(folder->filename() == std::filesystem::path{DWE_PROCESS_PROBE}.parent_path().filename());
}

} // namespace dwe::platform

namespace dwe::platform {

TEST_CASE("a child process's output and error lines are read as they come, then its exit code") {
  const std::array<std::string, 4> arguments{DWE_PROCESS_PROBE, "4", "first line", "second"};
  auto child = ChildProcess::start(arguments);
  REQUIRE_MESSAGE(child.has_value(), child.error().message);
  std::vector<std::string> lines;
  std::optional<int> code;
  // The probe ends at once; a generous limit keeps a broken read from hanging the tests.
  const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{20};
  while (!code && std::chrono::steady_clock::now() < limit) {
    std::ranges::move(child->readLines(), std::back_inserter(lines));
    code = child->exitCode();
    if (!code) {
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
  }
  REQUIRE(code.has_value());
  CHECK(*code == 4);
  CHECK(std::ranges::find(lines, "first line") != lines.end());
  CHECK(std::ranges::find(lines, "second") != lines.end());
  // The standard error comes in the same lines.
  CHECK(std::ranges::find(lines, "process probe: done") != lines.end());
  CHECK(child->readLines().empty());
}

TEST_CASE("a child process that cannot start is an error") {
  CHECK_FALSE(ChildProcess::start({}).has_value());
  const std::array<std::string, 1> missing{
      (std::filesystem::path{DWE_PROCESS_PROBE}.parent_path() / "no-such-program").string()};
  CHECK_FALSE(ChildProcess::start(missing).has_value());
}

} // namespace dwe::platform
