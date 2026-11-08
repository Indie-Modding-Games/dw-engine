#include <dwe/assets/file_system.hpp>
#include <dwe/core/sha1.hpp>
#include <dwe/platform/process.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "base_pack.hpp"
#include "disc_cache.hpp"
#include "disc_fixtures.hpp"
#include "dwx.hpp"
#include "effect_fixtures.hpp"
#include "model_fixtures.hpp"
#include "tim_builder.hpp"

using namespace dwe::dwx;
using dwe::test_support::TempFolder;

namespace {

// What dwx extract writes from the synthetic disc, one row per extractor version. The table only
// grows: when the output changes, bump ExtractorVersion (disc_cache.hpp) and append a row with the
// new version and fingerprint, never edit one, so a version never names two outputs. A version
// whose change only reaches extractors the synthetic disc does not run (they need a known
// executable's tables) keeps the previous fingerprint and says so with unseenBySyntheticDisc.
struct FingerprintRow {
  int version = 0;
  std::string_view fingerprint;
  bool unseenBySyntheticDisc = false;
};
constexpr std::array Fingerprints{
    FingerprintRow{.version = 1, .fingerprint = "bb77f40dc5892b246c1467fef107ffa30bb73fdc"},
    FingerprintRow{.version = 2, .fingerprint = "d86dbc6ff1eda81d846b98db571eb01f522c36ef"},
    FingerprintRow{.version = 3, .fingerprint = "9cbf4dbabde49fd3e75f98157d5c8cb3f3392d15"},
    FingerprintRow{.version = 4, .fingerprint = "a15a1611f17e6b57f42ccbf97dfc45003f671101"},
    FingerprintRow{.version = 5,
                   .fingerprint = "a15a1611f17e6b57f42ccbf97dfc45003f671101",
                   .unseenBySyntheticDisc = true},
    FingerprintRow{.version = 6, .fingerprint = "c959df6d1682a5d0cbae84bdbdfb4ca301d43384"},
    FingerprintRow{.version = 7,
                   .fingerprint = "c959df6d1682a5d0cbae84bdbdfb4ca301d43384",
                   .unseenBySyntheticDisc = true},
    FingerprintRow{.version = 8, .fingerprint = "e589545a1f6265dff472c7a35f17bf6702ee3635"},
    FingerprintRow{.version = 9,
                   .fingerprint = "e589545a1f6265dff472c7a35f17bf6702ee3635",
                   .unseenBySyntheticDisc = true},
    FingerprintRow{.version = 10, .fingerprint = "70513072c099bf21d22916d0c65454af3f259af8"},
    FingerprintRow{.version = 11,
                   .fingerprint = "70513072c099bf21d22916d0c65454af3f259af8",
                   .unseenBySyntheticDisc = true},
    FingerprintRow{.version = 12,
                   .fingerprint = "70513072c099bf21d22916d0c65454af3f259af8",
                   .unseenBySyntheticDisc = true},
    FingerprintRow{.version = 13, .fingerprint = "f267bd3d03d77cd687031714369bfdb5e343ed6c"},
    // The bundled recipe no longer grows the title's pictures (the skin smooths them): the
    // synthetic disc has none of them.
    FingerprintRow{.version = 14,
                   .fingerprint = "f267bd3d03d77cd687031714369bfdb5e343ed6c",
                   .unseenBySyntheticDisc = true},
};

std::vector<std::byte> bytesOf(std::initializer_list<unsigned> values) {
  std::vector<std::byte> bytes;
  for (const unsigned value : values) {
    bytes.push_back(static_cast<std::byte>(value));
  }
  return bytes;
}

std::vector<std::byte> textBytes(std::string_view text) {
  const auto bytes = std::as_bytes(std::span{text});
  return {bytes.begin(), bytes.end()};
}

// A cooked disc image with a file for each extractor that works without the executable's tables,
// and an executable that is not a known release.
std::vector<std::byte> syntheticDisc() {
  namespace image = dwe::formats::image;
  fixtures::IsoBuilder builder;
  builder.addFile("SYSTEM.CNF", textBytes("BOOT = cdrom:\\SLUS_000.01;1\r\nTCB=4\r\n"));
  builder.addFile("SLUS_000.01", fixtures::FakeExecutable::build({}));
  builder.addFile("ETCDAT/ONE.TIM",
                  image::test::buildTim(image::test::directTim(
                      image::TimPixelMode::Direct16, {.x = 0, .y = 0, .width = 2, .height = 1},
                      bytesOf({0x1F, 0x00, 0x00, 0x7C}))));
  // An invented 4x2 checkerboard under the name of an image the bundled UI recipe grows, so the
  // extraction writes its remaster pack too.
  std::vector<image::Color15> clut(16, image::Color15{0x7FFF});
  clut.at(2) = 0x001F;
  builder.addFile("ETCDAT/SYSTEM_W.TIM",
                  image::test::buildTim(image::test::indexedTim(
                      image::TimPixelMode::Indexed4, {.x = 0, .y = 480, .width = 16, .height = 1},
                      std::move(clut), {.x = 640, .y = 0, .width = 1, .height = 2},
                      bytesOf({0x21, 0x21, 0x12, 0x12}))));
  std::vector<std::byte> arena(10000);
  arena.front() = std::byte{0x80};
  builder.addFile("STDDAT/B_TOGI.ATR", std::move(arena));
  builder.addFile("SCN/MAPHEAD.SCN", bytesOf({0x08, 0x00, 0xFE, 0x00, 0x08, 0x00, 0xFF, 0xFF, 0xFE,
                                              0x00, 0xFF, 0x00}));
  builder.addFile("ETCHI/EFEDAT.EFE", dwe::formats::effect::testing::buildSlot(
                                          dwe::formats::effect::testing::sampleSlotParts()));
  builder.addFile("TEST/BOX.TMD", dwe::formats::model::testing::buildSampleTmd());
  return builder.build();
}

std::filesystem::path writeDisc(const std::filesystem::path& folder) {
  const std::filesystem::path image = folder / "disc.iso";
  const std::vector<std::byte> bytes = syntheticDisc();
  std::ofstream{image, std::ios::binary}.write(reinterpret_cast<const char*>(bytes.data()),
                                               static_cast<std::streamsize>(bytes.size()));
  return image;
}

struct FileCloser {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): std::tmpfile gives a C file, which only fclose closes.
  void operator()(std::FILE* file) const { std::fclose(file); }
};

// Extracts the disc for the cache, with the progress report out of the test's output.
ContentWriter realExtraction(const std::filesystem::path& image) {
  return [image](const std::filesystem::path& folder) {
    const std::unique_ptr<std::FILE, FileCloser> report{std::tmpfile()};
    return extractBasePack(image, folder / BasePackFolder, false, report.get());
  };
}

// A path as UTF-8, the encoding of a program's arguments and output on every system.
std::string utf8(const std::filesystem::path& path) {
  return dwe::assets::pathText(path);
}

// The output of a program with each \r\n as \n: on Windows the CRT writes text that way.
std::string withNewlines(std::string_view output) {
  std::string text;
  for (std::size_t i = 0; i < output.size(); ++i) {
    if (output[i] != '\r' || i + 1 == output.size() || output[i + 1] != '\n') {
      text += output[i];
    }
  }
  return text;
}

std::vector<std::string> names(const std::filesystem::path& folder) {
  std::vector<std::string> found;
  for (const auto& entry : std::filesystem::directory_iterator{folder}) {
    found.push_back(utf8(entry.path().filename()));
  }
  std::ranges::sort(found);
  return found;
}

std::string readText(const std::filesystem::path& file) {
  std::ifstream stream{file, std::ios::binary};
  return std::string{std::istreambuf_iterator<char>{stream}, {}};
}

// The SHA-1 of every file of a folder: its path inside the folder and its bytes, in path order.
std::string treeFingerprint(const std::filesystem::path& folder) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator{folder}) {
    if (entry.is_regular_file()) {
      files.push_back(entry.path().lexically_relative(folder));
    }
  }
  std::ranges::sort(files, {},
                    [](const std::filesystem::path& file) { return file.generic_string(); });
  dwe::core::Sha1 hasher;
  for (const std::filesystem::path& file : files) {
    const std::string name = file.generic_string() + '\0';
    const std::string bytes = readText(folder / file);
    hasher.update(std::as_bytes(std::span{name}));
    hasher.update(std::as_bytes(std::span{bytes}));
  }
  return dwe::core::toHex(hasher.finish());
}

// Makes an entry as dwx extract --cache leaves it, with a base pack written by dwx extract.
void writeExtractedEntry(const std::filesystem::path& entry) {
  const DiscInfo info{
      .volumeId = "FIXTURE", .executablePath = "", .executableSha1 = "", .version = ""};
  REQUIRE(prepareOutputFolder(entry / BasePackFolder, false).has_value());
  REQUIRE(writeBasePack(entry / BasePackFolder, ExtractionRun{}, info).has_value());
  std::ofstream{entry / CacheRecordFile} << "extractor_version: 0\n";
}

} // namespace

TEST_CASE("a disc's cache key is the SHA-1 of its image and the extractor version") {
  const TempFolder temp;
  const std::filesystem::path image = writeDisc(temp.path());
  const auto key = discCacheKey(image);
  REQUIRE_MESSAGE(key.has_value(), key.error().message);
  const auto digest = hashFile(image);
  REQUIRE(digest.has_value());
  CHECK(key->discSha1 == dwe::core::toHex(*digest));
  CHECK(key->extractorVersion == ExtractorVersion);
  CHECK(key->name() == std::format("{}-v{}", key->discSha1, ExtractorVersion));
}

TEST_CASE("a CUE sheet's cache key is the SHA-1 of the BIN it names") {
  const TempFolder temp;
  const std::filesystem::path bin = temp.path() / "Some Disc.bin";
  const std::vector<std::byte> raw = fixtures::toRawImage(syntheticDisc());
  std::ofstream{bin, std::ios::binary}.write(reinterpret_cast<const char*>(raw.data()),
                                             static_cast<std::streamsize>(raw.size()));
  std::ofstream{temp.path() / "Some Disc.cue"}
      << "FILE \"Some Disc.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";

  const auto key = discCacheKey(temp.path() / "Some Disc.cue");
  REQUIRE_MESSAGE(key.has_value(), key.error().message);
  const auto digest = hashFile(bin);
  REQUIRE(digest.has_value());
  CHECK(key->discSha1 == dwe::core::toHex(*digest));
}

TEST_CASE("a disc is extracted into the cache once and reused after that") {
  const TempFolder temp;
  const std::filesystem::path image = writeDisc(temp.path());
  const std::filesystem::path cache = temp.path() / "dev-cache";
  const auto key = discCacheKey(image);
  REQUIRE(key.has_value());

  int extractions = 0;
  const ContentWriter extract = [&](const std::filesystem::path& folder) {
    ++extractions;
    return realExtraction(image)(folder);
  };
  const auto first = cachedContent(cache, *key, extract);
  REQUIRE_MESSAGE(first.has_value(), first.error().message);
  CHECK(first->extracted);
  CHECK(first->folder == cache / key->name());
  CHECK(writtenByExtract(first->folder / BasePackFolder));
  CHECK(std::filesystem::is_regular_file(first->folder / BasePackFolder / "pack.yaml"));
  // The entry records its disc and extractor version, and nothing else is left in the cache.
  CHECK(readText(first->folder / CacheRecordFile)
            .contains(std::format("extractor_version: {}", ExtractorVersion)));
  CHECK(readText(first->folder / CacheRecordFile).contains(key->discSha1));
  CHECK(names(cache) == std::vector<std::string>{key->name()});

  const auto second = cachedContent(cache, *key, extract);
  REQUIRE(second.has_value());
  CHECK_FALSE(second->extracted);
  CHECK(second->folder == first->folder);
  CHECK(extractions == 1);
}

TEST_CASE(
    "dwx extract into a folder named with a trailing slash puts the bundled recipe's pack next to it") {
  const TempFolder temp;
  const std::filesystem::path image = writeDisc(temp.path());
  const std::unique_ptr<std::FILE, FileCloser> report{std::tmpfile()};
  const auto extracted = extractBasePack(image, temp.path() / "content/base/", false, report.get());
  REQUIRE_MESSAGE(extracted.has_value(), extracted.error().message);
  CHECK(std::filesystem::is_regular_file(temp.path() / "content/base/pack.yaml"));
  CHECK(std::filesystem::is_regular_file(temp.path() / "content/ui_original/pack.yaml"));
  CHECK_FALSE(std::filesystem::exists(temp.path() / "content/base/ui_original"));
}

TEST_CASE("a failed extraction leaves nothing in the cache") {
  const TempFolder temp;
  const std::filesystem::path cache = temp.path() / "dev-cache";
  const DiscCacheKey key{.discSha1 = "ab", .extractorVersion = ExtractorVersion};
  const ContentWriter failing = [](const std::filesystem::path& folder) -> dwe::core::Result<void> {
    std::filesystem::create_directories(folder / BasePackFolder / "half");
    return dwe::core::fail("the disc stopped reading");
  };
  const auto content = cachedContent(cache, key, failing);
  REQUIRE_FALSE(content.has_value());
  CHECK(content.error().message == "the disc stopped reading");
  CHECK(names(cache).empty());
}

TEST_CASE(
    "an entry that another extraction moved into place first is used, and the own copy dropped") {
  const TempFolder temp;
  const std::filesystem::path cache = temp.path() / "dev-cache";
  const DiscCacheKey key{.discSha1 = "ab", .extractorVersion = ExtractorVersion};
  const ContentWriter racing = [&](const std::filesystem::path& folder) -> dwe::core::Result<void> {
    writeExtractedEntry(cache / key.name());
    std::filesystem::create_directories(folder / BasePackFolder);
    return {};
  };
  const auto content = cachedContent(cache, key, racing);
  REQUIRE_MESSAGE(content.has_value(), content.error().message);
  CHECK_FALSE(content->extracted);
  CHECK(content->folder == cache / key.name());
  CHECK(names(cache) == std::vector<std::string>{key.name()});
}

TEST_CASE(
    "a new extractor version leaves the disc's older entries for removal and keeps everything else") {
  const TempFolder temp;
  const std::filesystem::path cache = temp.path() / "dev-cache";
  const DiscCacheKey key{.discSha1 = "ab", .extractorVersion = 5};
  writeExtractedEntry(cache / "ab-v5");
  writeExtractedEntry(cache / "ab-v4");                          // stale: removed
  writeExtractedEntry(cache / "ab-v6");                          // a newer dwx's: kept
  writeExtractedEntry(cache / "cd-v4");                          // another disc's: kept
  std::filesystem::create_directories(cache / "ab-v3" / "base"); // not written by dwx extract: kept
  writeExtractedEntry(cache / ".ab-v4.partial-0123");            // an extraction in progress: kept

  const auto removed = removeStaleEntries(cache, key);
  REQUIRE_MESSAGE(removed.has_value(), removed.error().message);
  CHECK(*removed == std::vector<std::filesystem::path>{cache / "ab-v4"});
  CHECK(names(cache) ==
        std::vector<std::string>{".ab-v4.partial-0123", "ab-v3", "ab-v5", "ab-v6", "cd-v4"});
}

TEST_CASE("only a disc's SHA-1, -v and digits name an entry of an older version") {
  const TempFolder temp;
  const std::filesystem::path cache = temp.path() / "dev-cache";
  const DiscCacheKey key{.discSha1 = "ab", .extractorVersion = 5};
  for (const std::string_view name : {"ab-v-1", "ab-v+1", "ab-v", "ab-v1x", "ab-v 1"}) {
    writeExtractedEntry(cache / name);
  }
  const auto removed = removeStaleEntries(cache, key);
  REQUIRE_MESSAGE(removed.has_value(), removed.error().message);
  CHECK(removed->empty());
  CHECK(names(cache).size() == 5);
}

TEST_CASE(
    "the cache never follows a link: a linked older entry is not removed and a linked entry is not used") {
  const TempFolder temp;
  const std::filesystem::path cache = temp.path() / "dev-cache";
  const std::filesystem::path outside = temp.path() / "outside";
  writeExtractedEntry(outside);
  std::filesystem::create_directories(cache);
  std::error_code error;
  std::filesystem::create_directory_symlink(outside, cache / "ab-v4", error);
  if (error) {
    // Windows only lets an administrator or developer mode make links: there is nothing to test
    // without one.
    MESSAGE("cannot make a link here: " << error.message());
    return;
  }
  std::filesystem::create_directory_symlink(outside, cache / "ab-v5");

  const DiscCacheKey key{.discSha1 = "ab", .extractorVersion = 5};
  const auto removed = removeStaleEntries(cache, key);
  REQUIRE_MESSAGE(removed.has_value(), removed.error().message);
  CHECK(removed->empty());

  int extractions = 0;
  const ContentWriter extract =
      [&](const std::filesystem::path& folder) -> dwe::core::Result<void> {
    ++extractions;
    std::filesystem::create_directories(folder / BasePackFolder);
    return {};
  };
  const auto content = cachedContent(cache, key, extract);
  REQUIRE_MESSAGE(content.has_value(), content.error().message);
  CHECK(content->extracted);
  CHECK(extractions == 1);
  CHECK_FALSE(std::filesystem::is_symlink(cache / "ab-v5"));
  // The link was replaced, never followed: what it pointed to is as it was.
  CHECK(writtenByExtract(outside / BasePackFolder));
  CHECK(std::filesystem::is_regular_file(outside / CacheRecordFile));
}

TEST_CASE("a folder with an entry's name but without its record is extracted again") {
  const TempFolder temp;
  const std::filesystem::path cache = temp.path() / "dev-cache";
  const DiscCacheKey key{.discSha1 = "ab", .extractorVersion = ExtractorVersion};
  writeExtractedEntry(cache / key.name());
  std::filesystem::remove(cache / key.name() / CacheRecordFile);
  std::filesystem::create_directories(cache / key.name() / "left-over");

  const ContentWriter extract = [](const std::filesystem::path& folder) -> dwe::core::Result<void> {
    std::filesystem::create_directories(folder / BasePackFolder);
    return {};
  };
  const auto content = cachedContent(cache, key, extract);
  REQUIRE_MESSAGE(content.has_value(), content.error().message);
  CHECK(content->extracted);
  CHECK(std::filesystem::is_regular_file(content->folder / CacheRecordFile));
  CHECK_FALSE(std::filesystem::exists(content->folder / "left-over"));
}

TEST_CASE("only temporary folders older than the warning age are reported") {
  const TempFolder temp;
  const std::filesystem::path cache = temp.path() / "dev-cache";
  std::filesystem::create_directories(cache / ".ab-v1.partial-old");
  std::filesystem::create_directories(cache / ".ab-v1.partial-new");
  std::filesystem::create_directories(cache / "ab-v1");
  const auto longAgo = std::filesystem::file_time_type::clock::now() - PartialEntryWarningAge -
                       std::chrono::hours{1};
  std::filesystem::last_write_time(cache / ".ab-v1.partial-old", longAgo);
  std::filesystem::last_write_time(cache / "ab-v1", longAgo);

  CHECK(oldPartialEntries(cache, PartialEntryWarningAge) ==
        std::vector<std::filesystem::path>{cache / ".ab-v1.partial-old"});
  CHECK(oldPartialEntries(temp.path() / "missing", PartialEntryWarningAge).empty());
}

TEST_CASE("the extractor's output for the synthetic disc matches the fingerprint of its version") {
  const TempFolder temp;
  const std::filesystem::path image = writeDisc(temp.path());
  for (const std::string_view run : {"first", "second"}) {
    REQUIRE(realExtraction(image)(temp.path() / run).has_value());
  }
  const std::filesystem::path content = temp.path() / "first";
  const std::filesystem::path pack = content / BasePackFolder;
  // The disc reaches several extractors, so the fingerprint follows changes in each of them.
  for (const std::string_view output :
       {"disc/boot.yaml", "texture/etcdat/one.png", "arena/b_togi/collision.yaml",
        "script/maphead.yaml", "effect/000/effect.yaml", "effect/000/model.glb",
        "model/test/box.glb", "dwx/coverage.yaml"}) {
    CHECK_MESSAGE(std::filesystem::is_regular_file(pack / output), output);
  }
  // The bundled recipe's pack, next to the base pack.
  for (const std::string_view output :
       {"ui_original/pack.yaml", "ui_original/texture/etcdat/system_w.png",
        "ui_original/texture/etcdat/system_w_meta.yaml"}) {
    CHECK_MESSAGE(std::filesystem::is_regular_file(content / output), output);
  }
  const std::string fingerprint = treeFingerprint(content);
  CHECK(fingerprint == treeFingerprint(temp.path() / "second"));
  CHECK_MESSAGE(
      fingerprint == Fingerprints.back().fingerprint,
      std::format(
          "the output of dwx extract changed (fingerprint {}): bump ExtractorVersion and update the "
          "fingerprint (append a row to Fingerprints)",
          fingerprint));
  CHECK_MESSAGE(
      ExtractorVersion == Fingerprints.back().version,
      "ExtractorVersion changed: append a row to Fingerprints with it and the fingerprint of the new output");
}

TEST_CASE("the fingerprint table only grows: rising versions, each with its own output") {
  for (std::size_t row = 1; row < Fingerprints.size(); ++row) {
    CHECK(Fingerprints.at(row - 1).version < Fingerprints.at(row).version);
    if (Fingerprints.at(row).unseenBySyntheticDisc) {
      CHECK(Fingerprints.at(row - 1).fingerprint == Fingerprints.at(row).fingerprint);
      continue;
    }
    for (std::size_t earlier = 0; earlier < row; ++earlier) {
      CHECK(Fingerprints.at(earlier).fingerprint != Fingerprints.at(row).fingerprint);
    }
  }
}

TEST_CASE(
    "dwx extract --cache prints only the content folder, extracting the disc the first time") {
  const TempFolder temp;
  const std::filesystem::path image = writeDisc(temp.path());
  const std::filesystem::path cache = temp.path() / "dev-cache";
  const std::array<std::string, 5> command{DWE_DWX, "extract", "--cache", utf8(cache), utf8(image)};
  const auto key = discCacheKey(image);
  REQUIRE(key.has_value());
  const std::string expected = utf8(cache / key->name()) + "\n";
  for (int run = 0; run < 2; ++run) {
    const auto result = dwe::platform::runProcess(command);
    REQUIRE_MESSAGE(result.has_value(), result.error().message);
    CHECK(result->exitCode == ExitOk);
    CHECK(withNewlines(result->output) == expected);
  }
  CHECK(names(cache) == std::vector<std::string>{key->name()});
}

TEST_CASE("dwx extract --cache goes with no other option of extract") {
  const TempFolder temp;
  const std::filesystem::path image = writeDisc(temp.path());
  const std::array<std::string, 6> command{
      DWE_DWX, "extract", "--cache", utf8(temp.path() / "c"), "--force", utf8(image)};
  const auto result = dwe::platform::runProcess(command);
  REQUIRE(result.has_value());
  CHECK(result->exitCode == ExitFailed);
  CHECK(result->output.empty());
  CHECK_FALSE(std::filesystem::exists(temp.path() / "c"));
}
