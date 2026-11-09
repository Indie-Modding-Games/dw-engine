#pragma once

#include <dwe/platform/process.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "disc_fixtures.hpp"

// Runs the dwx command line as the game's first start and the launcher do, and writes the
// synthetic discs it reads.
namespace dwe::dwx::testing {

inline std::vector<std::byte> textBytes(std::string_view text) {
  const auto bytes = std::as_bytes(std::span{text});
  return {bytes.begin(), bytes.end()};
}

inline std::filesystem::path writeImage(const std::filesystem::path& folder,
                                        std::span<const std::byte> bytes) {
  const std::filesystem::path image = folder / "disc.iso";
  std::ofstream{image, std::ios::binary}.write(reinterpret_cast<const char*>(bytes.data()),
                                               static_cast<std::streamsize>(bytes.size()));
  return image;
}

// A disc with a boot executable that is no known release: dwx extract names its assets by number.
inline std::vector<std::byte> unknownGameDisc() {
  ::fixtures::IsoBuilder builder;
  builder.addFile("SYSTEM.CNF", textBytes("BOOT = cdrom:\\SLUS_000.01;1\r\nTCB=4\r\n"));
  builder.addFile("SLUS_000.01", ::fixtures::FakeExecutable::build({}));
  return builder.build();
}

struct CommandOutput {
  int exitCode = -1;
  std::vector<std::string> lines;
};

// Runs dwx as the game's first start does, reading its output and error lines as they come.
inline CommandOutput runDwx(std::span<const std::string> arguments) {
  std::vector<std::string> command{DWE_DWX};
  command.insert(command.end(), arguments.begin(), arguments.end());
  auto child = dwe::platform::ChildProcess::start(command);
  REQUIRE_MESSAGE(child.has_value(), child.error().message);
  CommandOutput output;
  const auto limit = std::chrono::steady_clock::now() + std::chrono::minutes{2};
  std::optional<int> code;
  while (!code && std::chrono::steady_clock::now() < limit) {
    std::ranges::move(child->readLines(), std::back_inserter(output.lines));
    code = child->exitCode();
    if (!code) {
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
  }
  REQUIRE(code.has_value());
  output.exitCode = code.value_or(-1);
  return output;
}

inline bool has(const CommandOutput& output, std::string_view line) {
  return std::ranges::find(output.lines, line) != output.lines.end();
}

} // namespace dwe::dwx::testing
