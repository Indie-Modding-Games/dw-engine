#include <dwe/assets/extractor_version.hpp>
#include <dwe/content/disc_install.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
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

TEST_CASE("an install record keeps the extractor version and the disc image") {
  const TempFolder temp;
  CHECK_FALSE(readInstallRecord(temp.path()).has_value());
  const std::filesystem::path image{"/discs/my game: \"us\" (1).cue"};
  REQUIRE(writeInstallRecord(temp.path(), {.extractorVersion = 7, .image = image}).has_value());
  const auto record = readInstallRecord(temp.path());
  REQUIRE(record.has_value());
  CHECK(record->extractorVersion == 7);
  CHECK(record->image == image);
  std::ofstream{temp.path() / InstallRecordFileName} << "extractor_version: seven\n";
  CHECK_FALSE(readInstallRecord(temp.path()).has_value());
}

TEST_CASE("installed content of another extractor version is read again") {
  const TempFolder temp;
  // No base pack: the first start installs it anyway, there is nothing to read again.
  CHECK_FALSE(installIsOutdated(temp.path()));
  std::filesystem::create_directories(temp.path() / "base");
  std::ofstream{temp.path() / "base/pack.yaml"} << "id: base\n";
  // An install older than the record.
  CHECK(installIsOutdated(temp.path()));
  REQUIRE(writeInstallRecord(temp.path(), {.extractorVersion = dwe::assets::ExtractorVersion - 1,
                                           .image = "/discs/game.cue"})
              .has_value());
  CHECK(installIsOutdated(temp.path()));
  REQUIRE(writeInstallRecord(temp.path(), {.extractorVersion = dwe::assets::ExtractorVersion,
                                           .image = "/discs/game.cue"})
              .has_value());
  CHECK_FALSE(installIsOutdated(temp.path()));
}

TEST_CASE("without a local content folder the game mounts the installed one") {
  // The tests run in the build folder, which has no ./content with a base pack.
  REQUIRE_FALSE(hasBasePack("content"));
  CHECK(defaultContentFolder(std::filesystem::path{"/data/dwe"}) == "/data/dwe/content");
  CHECK(defaultContentFolder(std::nullopt) == "content");
}

TEST_CASE("the team's packs are looked for next to a package's executable folder first") {
  const auto candidates = teamPacksCandidates(std::filesystem::path{"/opt/dwe/bin"});
  REQUIRE_FALSE(candidates.empty());
  CHECK(candidates.front() == "/opt/dwe/packs");
#ifdef DWE_PACKAGE
  // A package build names no folder of the build machine.
  CHECK(candidates.size() == 1);
  CHECK(teamPacksCandidates(std::nullopt).empty());
#else
  CHECK(candidates.size() == 2);
  CHECK(teamPacksCandidates(std::nullopt).size() == 1);
  // In the build tree no packs/ is next to the program's folder: the source tree's are used.
  CHECK(std::filesystem::is_directory(teamPacksFolder() / "game"));
#endif
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

#ifndef _WIN32
namespace {

// A stand-in for dwx: a shell script that answers identify with identifyLine and its exit code,
// and whose extract writes a base pack.
std::filesystem::path fakeDwx(const std::filesystem::path& folder, std::string_view identifyLine,
                              int identifyCode) {
  const std::filesystem::path script = folder / "fake-dwx";
  std::ofstream{script} << "#!/bin/sh\n"
                        << "case \"$1\" in\n"
                        << "identify) echo '" << identifyLine << "'; exit " << identifyCode
                        << ";;\n"
                        << "extract) mkdir -p \"$3\" && echo 'id: base' > \"$3/pack.yaml\" && "
                           "echo 'progress 100 done'; exit 0;;\n"
                        << "esac\nexit 2\n";
  std::filesystem::permissions(script, std::filesystem::perms::owner_all);
  return script;
}

// Runs an install until it finishes or fails, for at most half a minute.
void runToEnd(DiscInstall& install) {
  const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while ((install.stage() == DiscInstall::Stage::Identifying ||
          install.stage() == DiscInstall::Stage::Extracting) &&
         std::chrono::steady_clock::now() < limit) {
    install.update();
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
}

} // namespace

TEST_CASE("another region's disc is named by its release and installs nothing") {
  const TempFolder temp;
  std::ofstream{temp.path() / "disc.iso"} << "x";
  DiscInstall install = DiscInstall::start(temp.path() / "disc.iso", temp.path() / "data",
                                           fakeDwx(temp.path(), "other-version eu", 3));
  runToEnd(install);
  CHECK(install.stage() == DiscInstall::Stage::Failed);
  CHECK(install.problem() == InstallProblem::OtherRegion);
  CHECK(install.detail() == "eu");
  CHECK_FALSE(std::filesystem::exists(temp.path() / "data/content"));
}

TEST_CASE("reading the disc again replaces the packs and its record, never the saves or settings") {
  const TempFolder temp;
  const std::filesystem::path data = temp.path() / "data";
  std::filesystem::create_directories(data / "saves");
  std::ofstream{data / "saves/slot1.yaml"} << "kept\n";
  std::ofstream{data / "settings.yaml"} << "language: es\n";
  // Content of an older install: a base pack and no record.
  std::filesystem::create_directories(data / "content/base");
  std::ofstream{data / "content/base/pack.yaml"} << "id: base\nold: true\n";
  REQUIRE(installIsOutdated(data / "content"));
  std::ofstream{temp.path() / "disc.iso"} << "x";

  DiscInstall install =
      DiscInstall::start(temp.path() / "disc.iso", data, fakeDwx(temp.path(), "supported us", 0));
  runToEnd(install);
  REQUIRE(install.stage() == DiscInstall::Stage::Finished);
  CHECK_FALSE(installIsOutdated(data / "content"));
  const auto record = readInstallRecord(data / "content");
  REQUIRE(record.has_value());
  CHECK(record->extractorVersion == dwe::assets::ExtractorVersion);
  CHECK(record->image == std::filesystem::absolute(temp.path() / "disc.iso"));
  std::ifstream pack{data / "content/base/pack.yaml"};
  CHECK(std::string{std::istreambuf_iterator<char>{pack}, {}} == "id: base\n");
  std::ifstream save{data / "saves/slot1.yaml"};
  CHECK(std::string{std::istreambuf_iterator<char>{save}, {}} == "kept\n");
  std::ifstream settings{data / "settings.yaml"};
  CHECK(std::string{std::istreambuf_iterator<char>{settings}, {}} == "language: es\n");
  CHECK_FALSE(std::filesystem::exists(data / PartialContentFolderName));
}
#endif
