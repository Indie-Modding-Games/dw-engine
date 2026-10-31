#include <dwe/content/disc_install.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace dwe::content;
using dwe::test_support::TempFolder;

TEST_CASE("a content folder has a base pack when base/pack.yaml is there") {
  const TempFolder temp;
  CHECK_FALSE(hasBasePack(temp.path()));
  std::filesystem::create_directories(temp.path() / "base");
  CHECK_FALSE(hasBasePack(temp.path()));
  std::ofstream{temp.path() / "base/pack.yaml"} << "id: base\n";
  CHECK(hasBasePack(temp.path()));
}

TEST_CASE("without a local content folder the game mounts the installed one") {
  // The tests run in the build folder, which has no ./content with a base pack.
  REQUIRE_FALSE(hasBasePack("content"));
  CHECK(defaultContentFolder(std::filesystem::path{"/data/dwe"}) == "/data/dwe/content");
  CHECK(defaultContentFolder(std::nullopt) == "content");
}

TEST_CASE("the team's packs are looked for next to a package's executable folder first") {
  const auto candidates = teamPacksCandidates(std::filesystem::path{"/opt/dwe/bin"});
  REQUIRE(candidates.size() == 2);
  CHECK(candidates.front() == "/opt/dwe/packs");
  CHECK(teamPacksCandidates(std::nullopt).size() == 1);
  // In the build tree no packs/ is next to the program's folder: the source tree's are used.
  CHECK(std::filesystem::is_directory(teamPacksFolder() / "game"));
}

TEST_CASE("a CUE sheet's files that are not next to it are missing") {
  const TempFolder temp;
  std::ofstream{temp.path() / "Here.bin"} << "x";
  std::ofstream{temp.path() / "disc.cue"}
      << "FILE \"Here.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n"
      << "FILE \"Gone (Track 2).bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n"
      << "FILE plain.bin BINARY\n";
  CHECK(missingCueFiles(temp.path() / "disc.cue") ==
        std::vector<std::string>{"Gone (Track 2).bin", "plain.bin"});
  // Only CUE sheets name other files.
  CHECK(missingCueFiles(temp.path() / "Here.bin").empty());
}

TEST_CASE("dwx extract's progress lines give a percent and a step") {
  const auto line = parseProgressLine("progress 42 extract");
  REQUIRE(line.has_value());
  CHECK(line->percent == 42);
  CHECK(line->step == "extract");
  CHECK_FALSE(parseProgressLine("Disc: SLUS, 600 files").has_value());
  CHECK_FALSE(parseProgressLine("progress 101 extract").has_value());
  CHECK_FALSE(parseProgressLine("progress x extract").has_value());
  CHECK_FALSE(parseProgressLine("progress 5").has_value());
}

TEST_CASE("dwx's last error line is the failure's detail") {
  const std::vector<std::string> lines{"dwx: warning: one", "Disc: X", "dwx: cannot open a.bin"};
  CHECK(dwxError(lines) == "cannot open a.bin");
  CHECK(dwxError(std::vector<std::string>{"nothing"}).empty());
}

TEST_CASE("an install whose image is missing fails before running dwx") {
  const TempFolder temp;
  const DiscInstall install =
      DiscInstall::start(temp.path() / "none.cue", temp.path() / "data", temp.path() / "dwx");
  CHECK(install.stage() == DiscInstall::Stage::Failed);
  CHECK(install.problem() == InstallProblem::MissingFile);
}

TEST_CASE("an install whose CUE sheet names a missing BIN says which") {
  const TempFolder temp;
  std::ofstream{temp.path() / "disc.cue"} << "FILE \"disc.bin\" BINARY\n";
  const DiscInstall install =
      DiscInstall::start(temp.path() / "disc.cue", temp.path() / "data", temp.path() / "dwx");
  CHECK(install.problem() == InstallProblem::MissingFile);
  CHECK(install.detail() == "disc.bin");
}

TEST_CASE("an install with no dwx to run fails") {
  const TempFolder temp;
  std::ofstream{temp.path() / "disc.iso"} << "x";
  const DiscInstall install =
      DiscInstall::start(temp.path() / "disc.iso", temp.path() / "data", temp.path() / "no-dwx");
  CHECK(install.stage() == DiscInstall::Stage::Failed);
  CHECK(install.problem() == InstallProblem::NoTool);
}
