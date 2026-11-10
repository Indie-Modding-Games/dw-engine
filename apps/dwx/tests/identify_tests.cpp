#include <dwe/assets/file_system.hpp>
#include <dwe/platform/process.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
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
#include "dwx.hpp"
#include "dwx_command.hpp"

using namespace dwe::dwx;
using namespace dwe::dwx::testing;
using dwe::test_support::TempFolder;

TEST_CASE("dwx identify prints what the disc is and exits with its code") {
  const TempFolder temp;
  const std::array arguments{std::string{"identify"},
                             dwe::assets::pathText(writeImage(temp.path(), unknownGameDisc()))};
  const CommandOutput output = runDwx(arguments);
  CHECK(output.exitCode == ExitUnknown);
  CHECK(has(output, "unknown -"));

  const std::array missing{std::string{"identify"},
                           dwe::assets::pathText(temp.path() / "missing.iso")};
  CHECK(runDwx(missing).exitCode == ExitFailed);
}

TEST_CASE("dwx extract --progress tells each step and ends at 100") {
  const TempFolder temp;
  const std::array arguments{std::string{"extract"}, std::string{"--progress"},
                             std::string{"--out"},
                             dwe::assets::pathText(temp.path() / "content/base"),
                             dwe::assets::pathText(writeImage(temp.path(), unknownGameDisc()))};
  const CommandOutput output = runDwx(arguments);
  REQUIRE(output.exitCode == ExitOk);
  CHECK(has(output, "progress 0 read"));
  CHECK(has(output, "progress 80 write"));
  CHECK(has(output, "progress 100 done"));
  CHECK(std::filesystem::is_regular_file(temp.path() / "content/base/pack.yaml"));
}

TEST_CASE("a disc whose executable is no known release is unknown") {
  const TempFolder temp;
  const auto identity = identifyDisc(writeImage(temp.path(), unknownGameDisc()));
  REQUIRE_MESSAGE(identity.has_value(), identity.error().message);
  CHECK(identity->kind == DiscKind::Unknown);
  CHECK(identity->version.empty());
}

TEST_CASE(
    "only a release whose tables are located is supported; other known releases are another region's") {
  CHECK(discKind("us") == DiscKind::Supported);
  // The EU and JP releases are known by their executables' digests, but their tables are not
  // located.
  CHECK(discKind("eu") == DiscKind::OtherVersion);
  CHECK(discKind("jp") == DiscKind::OtherVersion);
  CHECK(discKind("jp_rev1") == DiscKind::OtherVersion);
  CHECK(discKind("") == DiscKind::Unknown);
}

TEST_CASE("a disc with no boot executable is unknown") {
  const TempFolder temp;
  fixtures::IsoBuilder builder;
  builder.addFile("README.TXT", textBytes("not a game"));
  const auto identity = identifyDisc(writeImage(temp.path(), builder.build()));
  REQUIRE_MESSAGE(identity.has_value(), identity.error().message);
  CHECK(identity->kind == DiscKind::Unknown);
}

TEST_CASE("an image that is missing, or a CUE sheet whose BIN is missing, cannot be identified") {
  const TempFolder temp;
  CHECK_FALSE(identifyDisc(temp.path() / "nothing.cue").has_value());
  std::ofstream{temp.path() / "lone.cue"}
      << "FILE \"lone.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
  CHECK_FALSE(identifyDisc(temp.path() / "lone.cue").has_value());
}

TEST_CASE("a file that is not a disc image cannot be identified") {
  const TempFolder temp;
  const std::filesystem::path text = temp.path() / "notes.bin";
  std::ofstream{text} << "just some text, far shorter than a volume descriptor\n";
  CHECK_FALSE(identifyDisc(text).has_value());
}

TEST_CASE("the user's disc is a supported release") {
  const std::optional<std::filesystem::path> disc = dwe::test_support::userDisc();
  if (!disc) {
    MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
    return;
  }
  const auto identity = identifyDisc(*disc);
  REQUIRE_MESSAGE(identity.has_value(), identity.error().message);
  CHECK(identity->kind == DiscKind::Supported);
  CHECK(identity->version == "us");
}
