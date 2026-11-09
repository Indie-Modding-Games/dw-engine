#include <dwe/assets/extractor_version.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/content/disc_install.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <algorithm>
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

TEST_CASE("a content folder has a base pack when base.pak is there") {
  const TempFolder temp;
  std::ofstream{temp.path() / "base.pak"} << "x";
  CHECK(hasBasePack(temp.path()));
  CHECK(installIsOutdated(temp.path()));
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
  const auto bundle =
      teamPacksCandidates(std::filesystem::path{"/Applications/dwe-game.app/Contents/MacOS"});
  REQUIRE(bundle.size() >= 2);
  CHECK(bundle[1] == "/Applications/dwe-game.app/Contents/Resources/packs");
#ifdef DWE_PACKAGE
  // A package build names no folder of the build machine.
  CHECK(candidates.size() == 2);
  CHECK(teamPacksCandidates(std::nullopt).empty());
#else
  CHECK(candidates.size() == 3);
  CHECK(teamPacksCandidates(std::nullopt).size() == 1);
  // In the build tree no packs/ is next to the program's folder: the source tree's are used.
  CHECK(std::filesystem::is_directory(teamPacksFolder() / "game"));
#endif
}

TEST_CASE("the team's packs are read from their .pak archives, and from folders in development") {
  const TempFolder temp;
  temp.write("source/game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\n");
  temp.write("source/game/ui/fonts/font.ttf", "font bytes");
  const std::filesystem::path packs = temp.path() / "packs";
  std::filesystem::create_directories(packs);
  CHECK_FALSE(hasTeamGamePack(packs));
  REQUIRE(dwe::assets::writePak(dwe::assets::DiskFileSystem{}, temp.path() / "source/game",
                                packs / "game.pak")
              .has_value());
  CHECK(hasTeamGamePack(packs));
  const auto bytes = readTeamPackFile(packs, "game", "ui/fonts/font.ttf");
  REQUIRE_MESSAGE(bytes.has_value(), (bytes ? std::string{} : bytes.error().message));
  CHECK(std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()} ==
        "font bytes");
  CHECK_FALSE(readTeamPackFile(packs, "game", "ui/fonts/missing.ttf").has_value());
  // A loose folder beside the archive: a development build reads it, a package only the archive.
  temp.write("packs/game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\n");
  temp.write("packs/game/ui/fonts/font.ttf", "loose");
  const auto read = readTeamPackFile(packs, "game", "ui/fonts/font.ttf");
  REQUIRE(read.has_value());
  const std::string_view text{reinterpret_cast<const char*>(read->data()), read->size()};
#ifdef DWE_PACKAGE
  CHECK(text == "font bytes");
#else
  CHECK(text == "loose");
  std::filesystem::remove(packs / "game.pak");
  CHECK(hasTeamGamePack(packs));
#endif
}

TEST_CASE("a team pack's folder lists the same from its archive and from a loose folder") {
  const TempFolder temp;
  temp.write("source/game/pack.yaml", "id: game\nversion: 1.0.0\nlayer: base\n");
  temp.write("source/game/localization/en/first_run.yaml", "title: x\n");
  temp.write("source/game/localization/xx/first_run.yaml", "title: y\n");
  temp.write("source/game/ui/fonts/font.ttf", "font bytes");
  const std::filesystem::path packs = temp.path() / "packs";
  std::filesystem::create_directories(packs);
  REQUIRE(dwe::assets::writePak(dwe::assets::DiskFileSystem{}, temp.path() / "source/game",
                                packs / "game.pak")
              .has_value());
  auto listed = listTeamPackFiles(packs, "game", "localization");
  REQUIRE_MESSAGE(listed.has_value(), (listed ? std::string{} : listed.error().message));
  std::ranges::sort(*listed);
  CHECK(*listed == std::vector<std::string>{"en/first_run.yaml", "xx/first_run.yaml"});
  CHECK_FALSE(listTeamPackFiles(temp.path() / "none", "game", "localization").has_value());
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

constexpr std::string_view BaseManifest = "id: base\nversion: 1.0.0\nlayer: base\n";

// A real base.pak with one asset, for the stand-in's cook-pak to hand over.
std::filesystem::path baseArchive(const std::filesystem::path& folder) {
  const std::filesystem::path pack = folder / "archive-source";
  std::filesystem::create_directories(pack / "text");
  std::ofstream{pack / "pack.yaml"} << BaseManifest;
  std::ofstream{pack / "text/hello.yaml"} << "hello: world\n";
  const std::filesystem::path archive = folder / "prepared.pak";
  REQUIRE(dwe::assets::writePak(dwe::assets::DiskFileSystem{}, pack, archive).has_value());
  return archive;
}

// A stand-in for dwx: a shell script that answers identify with identifyLine and its exit code,
// whose extract writes a base pack folder and an ui_original pack beside it, and whose cook-pak
// writes the archive baseArchive made, leaving its cooked folder behind (cookCode: its exit code).
std::filesystem::path fakeDwx(const std::filesystem::path& folder, std::string_view identifyLine,
                              int identifyCode, int cookCode = 0) {
  const std::filesystem::path archive = baseArchive(folder);
  const std::filesystem::path script = folder / "fake-dwx";
  std::ofstream{script} << "#!/bin/sh\n"
                        << "case \"$1\" in\n"
                        << "identify) echo '" << identifyLine << "'; exit " << identifyCode
                        << ";;\n"
                        << "extract) mkdir -p \"$3\" \"$3/../ui_original\" && "
                           "echo 'id: base' > \"$3/pack.yaml\" && "
                           "echo 'id: ui_original' > \"$3/../ui_original/pack.yaml\" && "
                           "echo 'progress 100 done'; exit 0;;\n"
                        << "cook-pak) [ -f \"$2/pack.yaml\" ] || exit 2; echo 'progress 50 cook'; "
                        << R"(mkdir -p "$4.cook" && echo 'id: base' > "$4.cook/pack.yaml"; )"
                        << "[ " << cookCode << " -eq 0 ] || { echo 'dwx: the cook failed'; exit "
                        << cookCode << "; }; cp '" << dwe::assets::pathText(archive)
                        << "' \"$4\" && echo 'progress 100 done'; exit 0;;\n"
                        << "esac\nexit 2\n";
  std::filesystem::permissions(script, std::filesystem::perms::owner_all);
  return script;
}

// The names in a folder, sorted.
std::vector<std::string> namesIn(const std::filesystem::path& folder) {
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator{folder}) {
    names.push_back(entry.path().filename().string());
  }
  std::ranges::sort(names);
  return names;
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
  // The older install's base folder made way for the archive.
  CHECK(namesIn(data / "content") ==
        std::vector<std::string>{"base.pak", "install.yaml", "ui_original"});
  std::ifstream save{data / "saves/slot1.yaml"};
  CHECK(std::string{std::istreambuf_iterator<char>{save}, {}} == "kept\n");
  std::ifstream settings{data / "settings.yaml"};
  CHECK(std::string{std::istreambuf_iterator<char>{settings}, {}} == "language: es\n");
  CHECK_FALSE(std::filesystem::exists(data / PartialContentFolderName));
}
TEST_CASE("an install from scratch ends with base.pak alone, and the base pack mounts from it") {
  const TempFolder temp;
  const std::filesystem::path data = temp.path() / "data";
  std::ofstream{temp.path() / "disc.iso"} << "x";
  DiscInstall install =
      DiscInstall::start(temp.path() / "disc.iso", data, fakeDwx(temp.path(), "supported us", 0));
  runToEnd(install);
  REQUIRE(install.stage() == DiscInstall::Stage::Finished);
  CHECK(install.percent() == 100);
  CHECK(namesIn(data) == std::vector<std::string>{"content"});
  CHECK(namesIn(data / "content") ==
        std::vector<std::string>{"base.pak", "install.yaml", "ui_original"});
  CHECK(hasBasePack(data / "content"));
  CHECK_FALSE(installIsOutdated(data / "content"));

  // The game finds the archive as the base pack, and reads its assets from it.
  const dwe::assets::DiskFileSystem disk;
  const auto found = dwe::assets::findPacks(disk, data / "content");
  REQUIRE(found.has_value());
  const std::vector<dwe::assets::FoundPack> base{found->packs.front()};
  CHECK(base.front().path == data / "content/base.pak");
  const auto opened = dwe::assets::openPacks(disk, base, dwe::assets::PackOrigin::Engine);
  REQUIRE(opened.has_value());
  const auto files = dwe::assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE(files.has_value());
  const auto id = dwe::assets::AssetId::parse("text/hello");
  REQUIRE(id.has_value());
  const auto text = files->read(*id);
  REQUIRE(text.has_value());
  CHECK(std::string_view{reinterpret_cast<const char*>(text->data()), text->size()} ==
        "hello: world\n");
}

TEST_CASE("a cook that fails installs nothing and keeps the installed content") {
  const TempFolder temp;
  const std::filesystem::path data = temp.path() / "data";
  std::filesystem::create_directories(data / "content/base");
  std::ofstream{data / "content/base/pack.yaml"} << "id: base\nold: true\n";
  std::ofstream{temp.path() / "disc.iso"} << "x";
  DiscInstall install = DiscInstall::start(temp.path() / "disc.iso", data,
                                           fakeDwx(temp.path(), "supported us", 0, 2));
  runToEnd(install);
  CHECK(install.stage() == DiscInstall::Stage::Failed);
  CHECK(install.problem() == InstallProblem::ExtractFailed);
  CHECK(install.detail() == "the cook failed");
  CHECK(namesIn(data / "content") == std::vector<std::string>{"base"});
  CHECK_FALSE(std::filesystem::exists(data / PartialContentFolderName));
}
#endif
