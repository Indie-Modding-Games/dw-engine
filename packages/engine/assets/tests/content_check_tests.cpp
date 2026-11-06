#include <dwe/assets/content_check.hpp>
#include <dwe/assets/media_headers.hpp>
#include <dwe/assets/model.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>
#include <lodepng.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "media_fixtures.hpp"

using namespace dwe::assets;
using dwe::assets::testing::ByteWriter;

namespace {

// The message of a failed result. doctest builds a message even when the check passes, so it cannot
// call error().
template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

std::vector<std::byte> bytesOf(std::string_view text) {
  return ByteWriter{}.text(text).data();
}

std::vector<std::byte> pngFile() {
  std::vector<unsigned char> png;
  REQUIRE(lodepng::encode(png, std::vector<unsigned char>(std::size_t{2} * 2 * 4, 0x80), 2, 2) ==
          0);
  std::vector<std::byte> bytes(png.size());
  std::ranges::transform(png, bytes.begin(), [](unsigned char c) { return std::byte{c}; });
  return bytes;
}

// The same bytes with one 32-bit little-endian field changed.
std::vector<std::byte> patched(std::vector<std::byte> bytes, std::size_t offset,
                               std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
  }
  return bytes;
}

} // namespace

TEST_CASE("a WAV header gives the sample format and length") {
  const auto wav = readWavHeader(testing::wavFile(5));
  REQUIRE_MESSAGE(wav.has_value(), errorOf(wav));
  CHECK(wav->channels == 1);
  CHECK(wav->sampleRate == 22050);
  CHECK(wav->bitsPerSample == 16);
  CHECK(wav->frames == 5);
  CHECK(errorOf(readWavHeader(bytesOf("RIFF\4\0\0\0AVI "))) == "not a RIFF WAVE file");
  CHECK(errorOf(readWavHeader(patched(testing::wavFile(), 4, 1000))).contains("does not fit"));
  CHECK(
      errorOf(readWavHeader(patched(testing::wavFile(), 16, 1000))).contains("goes past the end"));
  CHECK(errorOf(readWavHeader(testing::riffFile("WAVE", {})))
            .contains("needs a 'fmt ' and a 'data' chunk"));
}

TEST_CASE("a SoundFont needs its three lists and whole pdta records") {
  const auto sf2 = readSoundFontHeader(testing::soundFontFile());
  REQUIRE_MESSAGE(sf2.has_value(), errorOf(sf2));
  CHECK(sf2->presets == 1);
  CHECK(sf2->samples == 1);
  CHECK(errorOf(readSoundFontHeader(testing::wavFile())) == "not a RIFF sfbk file");
  const auto infoOnly = testing::riffFile(
      "sfbk",
      ByteWriter{}
          .chunk("LIST",
                 ByteWriter{}.text("INFO").chunk("ifil", ByteWriter{}.u16(2).u16(4).data()).data())
          .data());
  CHECK(errorOf(readSoundFontHeader(infoOnly)) == "the LIST sdta chunk is missing");
}

TEST_CASE("a MIDI header must agree with the tracks in the file") {
  const auto midi = readMidiHeader(testing::midiFile(3));
  REQUIRE_MESSAGE(midi.has_value(), errorOf(midi));
  CHECK(midi->format == 1);
  CHECK(midi->tracks == 3);
  CHECK(midi->division == 480);
  auto truncated = testing::midiFile(2);
  truncated.resize(truncated.size() - 6);
  CHECK(errorOf(readMidiHeader(truncated)).contains("goes past the end"));
  CHECK(
      errorOf(readMidiHeader(ByteWriter{}.text("MThd").u32be(6).u16be(0).u16be(2).u16be(96).data()))
          .contains("format 0 with 2 tracks"));
  CHECK(errorOf(readMidiHeader(bytesOf("RIFF"))) == "not a standard MIDI file");
}

TEST_CASE("a Y4M header gives the picture size") {
  const auto video = readY4mHeader(testing::y4mFile());
  REQUIRE_MESSAGE(video.has_value(), errorOf(video));
  CHECK(video->width == 320);
  CHECK(video->height == 240);
  CHECK(errorOf(readY4mHeader(bytesOf("YUV4MPEG2 W320 F30:1\n"))) ==
        "the YUV4MPEG2 header has no width or height");
  CHECK(errorOf(readY4mHeader(bytesOf("YUV4MPEG2 W0 H1\n"))).contains("is not a size"));
  CHECK(errorOf(readY4mHeader(bytesOf("YUV4MPEG2 W1 H1"))).contains("no line end"));
}

TEST_CASE("a glTF model gives its external files, the parents of its joints and its animations") {
  const auto model = readModel(bytesOf(testing::skinnedGltf()));
  REQUIRE_MESSAGE(model.has_value(), errorOf(model));
  CHECK(model->externalUris == std::vector<std::string>{"anim.bin", "skin.png"});
  REQUIRE(model->skins.size() == 1);
  CHECK(model->skins[0] == std::vector<std::int32_t>{-1, 0, 1});
  CHECK(model->animations == std::vector<std::string>{"walk"});
  CHECK(errorOf(readModel(bytesOf("not a model"))).contains("not a glTF model"));
  CHECK(errorOf(readModel(bytesOf(R"({"asset": {"version": "2.0"}, "buffers": [{"byteLength": 4}],
        "bufferViews": [{"buffer": 0, "byteLength": 8}]})")))
            .contains("does not validate"));
}

TEST_CASE("every asset of a stack is opened with the reader of its type") {
  MemoryFileSystem memory;
  memory.addFile("pack/pack.yaml", "id: test\nversion: 1.0.0\nlayer: mods\n");
  memory.addFile("pack/data/table.yaml", "rows: [1, 2]\n");
  memory.addFile("pack/data/broken.yaml", "rows: [1, 2\n");
  memory.addFile("pack/image/good.png", pngFile());
  memory.addFile("pack/image/bad.png", bytesOf("not a png"));
  memory.addFile("pack/model/skinned.gltf", testing::skinnedGltf());
  memory.addFile("pack/sound/hit.wav", testing::wavFile());
  memory.addFile("pack/sound/bank.sf2", testing::soundFontFile());
  memory.addFile("pack/music/theme.mid", testing::midiFile());
  memory.addFile("pack/video/intro.y4m", testing::y4mFile());
  memory.addFile("pack/notes/readme.doc", "hello");
  const std::vector<std::filesystem::path> roots{"pack"};
  const auto vfs = VirtualFileSystem::mount(memory, roots);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  const auto problems = checkContent(*vfs);
  REQUIRE(problems.size() == 3);
  CHECK(problems[0].id.str() == "data/broken");
  CHECK(problems[0].file == "pack/data/broken.yaml");
  CHECK(problems[1].id.str() == "image/bad");
  CHECK(problems[1].message.contains("not a PNG"));
  CHECK(problems[2].id.str() == "notes/readme");
  CHECK(problems[2].message == "the engine has no reader for .doc files");
}

namespace {

// A file system whose first read of one file fails, as one does while another program holds it.
class FailsOnceFileSystem final : public FileSystem {
public:
  FailsOnceFileSystem(const FileSystem& files, std::filesystem::path failing)
      : files_(files), failing_(std::move(failing)) {}

  dwe::core::Result<std::vector<std::string>>
  listFiles(const std::filesystem::path& folder) const override {
    return files_.listFiles(folder);
  }
  dwe::core::Result<std::vector<std::byte>>
  readFile(const std::filesystem::path& file) const override {
    if (file == failing_ && !failed_.exchange(true)) {
      return dwe::core::fail("cannot open 'held': it is being used by another process");
    }
    return files_.readFile(file);
  }
  dwe::core::Result<std::uint64_t> fileSize(const std::filesystem::path& file) const override {
    return files_.fileSize(file);
  }

private:
  const FileSystem& files_;
  std::filesystem::path failing_;
  mutable std::atomic<bool> failed_{false};
};

} // namespace

TEST_CASE("an asset that fails only once is read again alone and does not count as a problem") {
  MemoryFileSystem memory;
  memory.addFile("pack/pack.yaml", "id: test\nversion: 1.0.0\nlayer: mods\n");
  memory.addFile("pack/image/held.png", pngFile());
  memory.addFile("pack/image/bad.png", bytesOf("not a png"));
  const FailsOnceFileSystem files{memory, "pack/image/held.png"};
  const std::vector<std::filesystem::path> roots{"pack"};
  const auto vfs = VirtualFileSystem::mount(files, roots);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  std::vector<ContentProblem> passedAlone;
  const auto problems = checkContent(*vfs, &passedAlone);
  REQUIRE(problems.size() == 1);
  CHECK(problems[0].id.str() == "image/bad");
  REQUIRE(passedAlone.size() == 1);
  CHECK(passedAlone[0].id.str() == "image/held");
  CHECK(passedAlone[0].message.contains("another process"));
}

TEST_CASE(
    "the UI's documents, scripts and notes must be UTF-8, and its fonts TrueType or OpenType") {
  CHECK(openAsset("rml", bytesOf("<rml><body>Men\xc3\xba</body></rml>")).has_value());
  CHECK(openAsset("lua", bytesOf("")).has_value());
  CHECK(openAsset("txt", bytesOf("\xe2\x82\xac \xf0\x9f\x98\x80")).has_value());
  CHECK(errorOf(openAsset("rcss", bytesOf("body { color: \xff; }"))) ==
        "not UTF-8 text: byte 14 starts no character");
  CHECK(errorOf(openAsset("lua", bytesOf("x = '\xc3'"))) ==
        "not UTF-8 text: byte 5 starts no character");
  CHECK(openAsset("ttf",
                  bytesOf(std::string_view{"\x00\x01\x00\x00\x00\x0a\x00\x80\x00\x03\x00\x20", 12}))
            .has_value());
  CHECK(openAsset("otf", bytesOf(std::string_view{"OTTO\x00\x0a\x00\x80\x00\x03\x00\x20", 12}))
            .has_value());
  CHECK(errorOf(openAsset("ttf", bytesOf("<html>not a font</html>"))) ==
        "not a TrueType or OpenType font");
}

TEST_CASE("the assets of a .pak layer are opened like those of a folder") {
  MemoryFileSystem memory;
  memory.addFile("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  memory.addFile("base/image/good.png", pngFile());
  memory.addFile("base/image/bad.png", bytesOf("not a png"));
  memory.addFile("base/model/skinned.gltf", testing::skinnedGltf());
  memory.addFile("base/sound/hit.wav", testing::wavFile());
  memory.addFile("mod/pack.yaml", "id: mod\nversion: 1.0.0\nlayer: mods\n");
  memory.addFile("mod/sound/hit.wav", bytesOf("RIFF"));
  const dwe::test_support::TempFolder folder;
  const auto archive = folder.path() / "base.pak";
  const auto written = writePak(memory, "base", archive);
  REQUIRE_MESSAGE(written.has_value(), errorOf(written));
  const auto pak = PakFileSystem::open(archive);
  REQUIRE_MESSAGE(pak.has_value(), errorOf(pak));
  const std::vector<PackLocation> packs{{.fileSystem = pak->get(), .root = archive},
                                        {.fileSystem = &memory, .root = "mod"}};
  const auto vfs = VirtualFileSystem::mount(packs);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  const auto problems = checkContent(*vfs);
  REQUIRE(problems.size() == 2);
  // The bad image is read from the archive; the mod's sound overrides the archive's and is the one
  // opened.
  CHECK(problems[0].id.str() == "image/bad");
  CHECK(problems[0].file == pathText(archive / "image/bad.png"));
  CHECK(problems[0].message.contains("not a PNG"));
  CHECK(problems[1].id.str() == "sound/hit");
  CHECK(problems[1].file == "mod/sound/hit.wav");
}

TEST_CASE("cooked sounds are opened by decoding them") {
  // A valid Ogg Opus file opens in the cook tests of dwx; here the decoder's refusal shows which
  // reader ran.
  for (const std::string_view extension : {"opus", "ogg"}) {
    const auto opened = openAsset(extension, bytesOf("OggS but not really"));
    REQUIRE_FALSE(opened.has_value());
    CHECK_MESSAGE(opened.error().message.contains("Ogg"), opened.error().message);
  }
}

TEST_CASE("cooked textures are opened by transcoding them") {
  // A valid texture opens in the cook tests of dwx; here the decoder's refusal shows which reader
  // ran.
  const auto opened = openAsset("ktx2", bytesOf("not a texture"));
  REQUIRE_FALSE(opened.has_value());
  CHECK_MESSAGE(opened.error().message.contains("KTX2"), opened.error().message);
}

// A 2 x 2 PNG of one colour type, as is: lodepng would otherwise pick a palette for so few colours.
std::vector<std::byte> pngOfType(LodePNGColorType type) {
  lodepng::State state;
  std::vector<unsigned char> pixels(std::size_t{2} * 2 * 4, 0x80);
  if (type == LCT_PALETTE) {
    REQUIRE(lodepng_palette_add(&state.info_png.color, 0, 0, 0, 255) == 0);
    REQUIRE(lodepng_palette_add(&state.info_raw, 0, 0, 0, 255) == 0);
    state.info_raw.colortype = LCT_PALETTE;
    pixels.assign(4, 0);
  }
  state.info_png.color.colortype = type;
  state.info_png.color.bitdepth = 8;
  state.encoder.auto_convert = 0;
  std::vector<unsigned char> encoded;
  REQUIRE(lodepng::encode(encoded, pixels, 2, 2, state) == 0);
  std::vector<std::byte> bytes(encoded.size());
  std::ranges::transform(encoded, bytes.begin(), [](unsigned char c) { return std::byte{c}; });
  return bytes;
}

TEST_CASE("an image the game reads from its PNG must be a PNG it reads that way") {
  const std::vector<std::byte> indexed = pngOfType(LCT_PALETTE);
  const std::vector<std::byte> rgba = pngOfType(LCT_RGBA);
  MemoryFileSystem memory;
  memory.addFile("pack/pack.yaml", "id: test\nversion: 1.0.0\nlayer: mods\n");
  memory.addFile("pack/map/field/background_day.png", indexed);
  memory.addFile("pack/map/mod/town/texture8/000.png", indexed);
  memory.addFile("pack/map/mod/town/texture8/000_meta.yaml", "palettes: []\n");
  memory.addFile("pack/font/main.png", rgba);
  memory.addFile("pack/font/main_meta.yaml", "glyphs: []\n");
  // An older dwx cook turned map backgrounds and the font atlas into textures, which their readers
  // cannot read.
  memory.addFile("pack/map/field/background_night.ktx2", bytesOf("texture"));
  memory.addFile("pack/font/small.ktx2", bytesOf("texture"));
  memory.addFile("pack/map/field/texture4/001.png", rgba);
  // Any other image may be a texture.
  memory.addFile("pack/map/field/sky.png", rgba);
  const std::vector<std::filesystem::path> roots{"pack"};
  const auto vfs = VirtualFileSystem::mount(memory, roots);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));

  const auto problems = checkContent(*vfs);
  REQUIRE(problems.size() == 3);
  CHECK(problems[0].id.str() == "font/small");
  CHECK(problems[0].message.contains("as exact pixels"));
  CHECK(problems[1].id.str() == "map/field/background_night");
  CHECK(
      problems[1].message ==
      "the game reads this image from a PNG, as palette indices, so a .ktx2 file cannot replace it: keep it as PNG");
  CHECK(problems[2].id.str() == "map/field/texture4/001");
  CHECK(problems[2].message ==
        "the game reads this image's palette indices: the PNG has no palette");
}
