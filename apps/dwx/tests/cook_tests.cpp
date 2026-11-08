#include <dwe/assets/content_check.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/sound.hpp>
#include <dwe/assets/texture.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/formats/audio/wav.hpp>
#include <dwe/formats/image/png.hpp>
#include <dwe/test_support/temp_folder.hpp>
#include <dwe/video/av1_reader.hpp>
#include <dwe/video/picture.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cook.hpp"
#include "video_cooker.hpp"

using namespace dwe::dwx;
using dwe::assets::DiskFileSystem;
using dwe::test_support::TempFolder;

namespace {

std::uint32_t u32At(const std::vector<std::byte>& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
  }
  return value;
}

// Whether a cooked texture holds sRGB colour, as the engine reads it.
bool srgbOf(const std::vector<std::byte>& ktx2) {
  const auto texture = dwe::assets::decodeKtx2(ktx2, dwe::assets::TexelEncoding::Rgba8);
  REQUIRE_MESSAGE(texture.has_value(), texture.error().message);
  return texture->srgb;
}

std::string pngText(std::uint8_t red) {
  dwe::formats::image::RgbaImage image{.width = 2, .height = 2, .pixels = {}};
  image.pixels.assign(4, {.r = red, .g = 20, .b = 30, .a = 255});
  const auto png = dwe::formats::image::encodePngRgba(image);
  REQUIRE(png.has_value());
  return {reinterpret_cast<const char*>(png->data()), png->size()};
}

std::string indexedPngText(std::uint8_t last = 1) {
  const std::vector<std::uint8_t> indices{0, 1, 2, last};
  const std::vector<dwe::formats::image::Rgba8> palette{{.r = 0, .g = 0, .b = 0, .a = 0},
                                                        {.r = 255, .g = 0, .b = 0, .a = 255},
                                                        {.r = 0, .g = 0, .b = 255, .a = 255}};
  const auto png = dwe::formats::image::encodePngIndexed(2, 2, indices, palette);
  REQUIRE(png.has_value());
  return {reinterpret_cast<const char*>(png->data()), png->size()};
}

// A binary glTF with only its header, which is all the cooker checks.
std::string glbText() {
  return std::string{"glTF\x02\x00\x00\x00\x0C\x00\x00\x00", 12};
}

void writeSourcePack(const TempFolder& folder) {
  folder.write("src/pack.yaml",
               "id: mod\nversion: 1.0.0\nlayer: mods\ndependencies:\n  - id: base\n"
               "    min_version: 1.0.0\n");
  folder.write("src/map/start/background.png", pngText(10));
  folder.write("src/map/start/collision.yaml", "cells: []\n");
  folder.write("src/digimon/x/model.glb", glbText());
}

std::vector<std::byte> readOrEmpty(const std::filesystem::path& file) {
  return DiskFileSystem{}.readFile(file).value_or(std::vector<std::byte>{});
}

CookSummary cooked(const TempFolder& folder, std::string_view out = "out") {
  const auto summary = cookPack(cookers(), folder.path() / "src", folder.path() / out);
  REQUIRE(summary.has_value());
  return *summary;
}

std::string cookError(const TempFolder& folder, std::string_view out = "out") {
  const auto summary = cookPack(cookers(), folder.path() / "src", folder.path() / out);
  REQUIRE_FALSE(summary.has_value());
  return summary.error().message;
}

} // namespace

TEST_CASE("cooking a pack keeps its ids and runs each file through its cooker") {
  const TempFolder folder;
  writeSourcePack(folder);
  const CookSummary summary = cooked(folder);
  CHECK(summary.assets == 3);
  CHECK(summary.cooked == 2);
  CHECK(summary.copied == 1);
  CHECK(summary.kept == 0);

  const std::filesystem::path out = folder.path() / "out";
  CHECK(readOrEmpty(out / "pack.yaml") == readOrEmpty(folder.path() / "src/pack.yaml"));
  CHECK(readOrEmpty(out / "map/start/collision.yaml") ==
        readOrEmpty(folder.path() / "src/map/start/collision.yaml"));
  CHECK(readOrEmpty(out / "digimon/x/model.glb") ==
        readOrEmpty(folder.path() / "src/digimon/x/model.glb"));
  const auto texture = readOrEmpty(out / "map/start/background.ktx2");
  REQUIRE(texture.size() > 80);
  CHECK(u32At(texture, 12) == 0); // Basis Universal, transcoded when loaded
  CHECK(u32At(texture, 40) == 2);
  CHECK(srgbOf(texture));
  const auto opened = dwe::assets::openAsset("ktx2", texture);
  CHECK_MESSAGE(opened.has_value(), (opened ? std::string{} : opened.error().message));
  CHECK_FALSE(std::filesystem::exists(out / "map/start/background.png"));

  const auto files = dwe::assets::listPackFiles(DiskFileSystem{}, out);
  REQUIRE(files.has_value());
  CHECK(files->size() == 3);

  // The same pack cooks to the same bytes in another folder.
  cooked(folder, "again");
  for (const auto& file : *files) {
    CHECK_MESSAGE(readOrEmpty(out / file.path) == readOrEmpty(folder.path() / "again" / file.path),
                  file.path);
  }
}

TEST_CASE("cooking again only cooks what changed") {
  const TempFolder folder;
  writeSourcePack(folder);
  cooked(folder);
  const std::filesystem::path out = folder.path() / "out";
  const auto first = readOrEmpty(out / "map/start/background.ktx2");

  CookSummary summary = cooked(folder);
  CHECK(summary.kept == 3);
  CHECK(summary.cooked == 0);

  // A changed source cooks again; an output edited by hand is cooked again too.
  folder.write("src/map/start/background.png", pngText(99));
  folder.write("out/digimon/x/model.glb", "edited");
  summary = cooked(folder);
  CHECK(summary.kept == 1);
  CHECK(summary.cooked == 2);
  CHECK(readOrEmpty(out / "map/start/background.ktx2") != first);
  CHECK(readOrEmpty(out / "digimon/x/model.glb") ==
        readOrEmpty(folder.path() / "src/digimon/x/model.glb"));

  // A second asset with the same bytes is copied from the first; an asset that is gone loses its
  // output.
  folder.write("src/map/zone/background.png", pngText(99));
  std::filesystem::remove(folder.path() / "src/map/start/collision.yaml");
  summary = cooked(folder);
  CHECK(summary.shared == 1);
  CHECK(summary.removed == 1);
  CHECK(readOrEmpty(out / "map/zone/background.ktx2") ==
        readOrEmpty(out / "map/start/background.ktx2"));
  CHECK_FALSE(std::filesystem::exists(out / "map/start/collision.yaml"));

  // A cache that does not parse only makes everything cook again.
  folder.write("out/.dwx-cook", "outputs: [");
  summary = cooked(folder);
  CHECK(summary.kept == 0);
  CHECK(summary.cooked == 2);
  CHECK(summary.shared == 1);
}

TEST_CASE("cooking refuses folders it does not own and files that do not cook") {
  const TempFolder folder;
  writeSourcePack(folder);
  folder.write("mine/notes.txt", "keep");
  CHECK(cookError(folder, "mine")
            .ends_with("is not empty and dwx cook did not write it; choose another folder"));
  CHECK(cookError(folder, "src/out").contains("must not hold each other"));

  folder.write("src/digimon/x/model.glb", "not a glb at all");
  CHECK(cookError(folder).ends_with("model.glb: not a binary glTF file"));
  folder.write("src/digimon/x/model.glb", glbText());
  folder.write("src/map/start/background.png", "not a png");
  CHECK(cookError(folder).contains("background.png: not a PNG"));
}

TEST_CASE(
    "images whose palette is chosen at runtime pass through as PNG and sidecars set the colour space") {
  const TempFolder folder;
  writeSourcePack(folder);
  // Indexed, with no sidecar: a map background, whose palette the time of day picks.
  folder.write("src/map/start/background_palette3.png", indexedPngText(0));
  // Indexed, and its CLUT has several rows for the same pixels.
  folder.write("src/card/001.png", indexedPngText());
  folder.write("src/card/001_meta.yaml",
               "mode: indexed4\nclut_rect: {x: 0, y: 480, width: 16, height: 2}\n"
               "png: indexed\npalettes:\n- \"0000 7fff\"\n- \"0000 001f\"\n");
  // Indexed, from a map: its sprites choose the row.
  folder.write("src/map/start/texture8/000.png", indexedPngText(2));
  folder.write("src/map/start/texture8/000_meta.yaml",
               "source: \"MAP/MAP1/START.MAP\"\nclut_rect: {x: 0, y: 480, width: 256, height: 1}\n"
               "palettes:\n- \"0000 7fff\"\n");
  folder.write("src/ui/mask.png", pngText(50));
  folder.write("src/ui/mask_meta.yaml", "color_space: linear\n");

  CookSummary summary = cooked(folder);
  CHECK(summary.assets == 10);
  CHECK(summary.cooked == 3);
  CHECK(summary.passed == 3);
  CHECK(summary.copied == 4);
  const std::filesystem::path out = folder.path() / "out";
  CHECK(readOrEmpty(out / "map/start/background_palette3.png") ==
        readOrEmpty(folder.path() / "src/map/start/background_palette3.png"));
  CHECK_FALSE(std::filesystem::exists(out / "map/start/background_palette3.ktx2"));
  CHECK(readOrEmpty(out / "card/001.png") == readOrEmpty(folder.path() / "src/card/001.png"));
  CHECK_FALSE(std::filesystem::exists(out / "card/001.ktx2"));
  CHECK(readOrEmpty(out / "map/start/texture8/000.png") ==
        readOrEmpty(folder.path() / "src/map/start/texture8/000.png"));
  CHECK_FALSE(srgbOf(readOrEmpty(out / "ui/mask.ktx2")));
  CHECK(srgbOf(readOrEmpty(out / "map/start/background.ktx2")));

  // A sidecar that changes how its image cooks makes it cook again, though the image is the same.
  folder.write("src/card/001_meta.yaml",
               "mode: indexed4\npng: indexed\npalettes:\n- \"0000 7fff\"\n");
  folder.write("src/ui/mask_meta.yaml", "color_space: srgb\n");
  summary = cooked(folder);
  CHECK(summary.cooked == 2);
  CHECK(summary.removed == 1);
  CHECK_FALSE(std::filesystem::exists(out / "card/001.png"));
  CHECK(srgbOf(readOrEmpty(out / "card/001.ktx2")));
  CHECK(srgbOf(readOrEmpty(out / "ui/mask.ktx2")));

  folder.write("src/ui/mask_meta.yaml", "color_space: [");
  CHECK(cookError(folder).contains("mask_meta.yaml"));
  folder.write("src/ui/mask_meta.yaml", "color_space: hdr\n");
  CHECK(cookError(folder).contains("mask.png: its sidecar's color_space is not srgb or linear"));
}

TEST_CASE(
    "cooking again replaces a texture an older cook made of an image the game reads from its PNG") {
  const TempFolder folder;
  writeSourcePack(folder);
  folder.write("src/map/start/background_day.png", indexedPngText());
  // Before the texture cooker knew such images, its plan only read sidecars, and a background had
  // none.
  std::vector<Cooker> older{cookers().begin(), cookers().end()};
  std::ranges::find(older, std::string_view{"texture"}, &Cooker::name)->plan = nullptr;
  REQUIRE(cookPack(older, folder.path() / "src", folder.path() / "out").has_value());
  const std::filesystem::path out = folder.path() / "out";
  REQUIRE(std::filesystem::exists(out / "map/start/background_day.ktx2"));

  const CookSummary summary = cooked(folder);
  CHECK(summary.passed == 1);
  CHECK(summary.removed == 1);
  CHECK_FALSE(std::filesystem::exists(out / "map/start/background_day.ktx2"));
  CHECK(readOrEmpty(out / "map/start/background_day.png") ==
        readOrEmpty(folder.path() / "src/map/start/background_day.png"));
}

TEST_CASE("a bitmap font atlas passes through as PNG, with its glyphs") {
  const TempFolder folder;
  writeSourcePack(folder);
  folder.write("src/font/main.png", pngText(90));
  folder.write("src/font/main_meta.yaml", "cell_width: 12\ncell_height: 12\nglyphs: []\n");
  const CookSummary summary = cooked(folder);
  CHECK(summary.passed == 1);
  const std::filesystem::path out = folder.path() / "out";
  CHECK(readOrEmpty(out / "font/main.png") == readOrEmpty(folder.path() / "src/font/main.png"));
  CHECK_FALSE(std::filesystem::exists(out / "font/main.ktx2"));
  CHECK(readOrEmpty(out / "font/main_meta.yaml") ==
        readOrEmpty(folder.path() / "src/font/main_meta.yaml"));
}

TEST_CASE("a WAV in a pack cooks to Ogg Opus with the same id") {
  const TempFolder folder;
  writeSourcePack(folder);
  const std::vector<std::int16_t> pcm(4410, 1000);
  const auto wav = dwe::formats::audio::writeWav(
      dwe::formats::audio::WavSound{.pcm = pcm, .sampleRate = 11025, .loop = {}, .unityNote = 60});
  folder.write("src/sound/x/sample_001.wav",
               std::string{reinterpret_cast<const char*>(wav.data()), wav.size()});
  const CookSummary summary = cooked(folder);
  CHECK(summary.cooked == 3);
  const std::filesystem::path out = folder.path() / "out";
  CHECK_FALSE(std::filesystem::exists(out / "sound/x/sample_001.wav"));
  const auto sound = dwe::assets::decodeOpus(readOrEmpty(out / "sound/x/sample_001.opus"));
  REQUIRE(sound.has_value());
  CHECK(sound->pcm.size() == 19200);
  // dwx validate and dwe-game --check-content open it too.
  const auto opened = dwe::assets::openAsset("opus", readOrEmpty(out / "sound/x/sample_001.opus"));
  CHECK_MESSAGE(opened.has_value(), (opened ? std::string{} : opened.error().message));
}

TEST_CASE("a cook tells how many of the files it cooks or copies are done") {
  const TempFolder folder;
  writeSourcePack(folder);
  std::vector<std::size_t> done;
  std::size_t total = 0;
  const auto summary = cookPack(cookers(), folder.path() / "src", folder.path() / "out",
                                [&](std::size_t finished, std::size_t of) {
                                  done.push_back(finished);
                                  total = of;
                                });
  REQUIRE(summary.has_value());
  CHECK(total == 3);
  CHECK(done == std::vector<std::size_t>{1, 2, 3});
  // Nothing is left to do the second time: no file is cooked, so nothing is told.
  done.clear();
  REQUIRE(cookPack(cookers(), folder.path() / "src", folder.path() / "out",
                   [&](std::size_t finished, std::size_t /*of*/) { done.push_back(finished); })
              .has_value());
  CHECK(done.empty());
}

// A small Y4M as dwx extract writes the movies: full-range 4:2:0, a gradient that moves a little
// each frame.
std::string movieY4m(std::uint32_t width, std::uint32_t height, std::uint32_t frames) {
  std::string y4m =
      std::format("YUV4MPEG2 W{} H{} F30:1 Ip A1:1 C420jpeg XCOLORRANGE=FULL\n", width, height);
  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    y4m += "FRAME\n";
    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        y4m += static_cast<char>((((x + frame) * 255) / width + (y * 64) / height) & 0xFFU);
      }
    }
    y4m.append(2 * std::size_t{(width + 1) / 2} * ((height + 1) / 2), static_cast<char>(128));
  }
  return y4m;
}

TEST_CASE("movies cook to AV1 video and Opus sound that the engine plays back") {
  const TempFolder folder;
  writeSourcePack(folder);
  const std::vector<std::int16_t> pcm(3780, 1000);
  const auto wav = dwe::formats::audio::writeWav(
      dwe::formats::audio::WavSound{.pcm = pcm, .sampleRate = 37800, .loop = {}, .unityNote = 60});
  folder.write("src/movie/op1/audio.wav",
               std::string{reinterpret_cast<const char*>(wav.data()), wav.size()});
  constexpr std::uint32_t Width = 64;
  constexpr std::uint32_t Height = 48;
  constexpr std::uint32_t Frames = 4;
  const std::string y4m = movieY4m(Width, Height, Frames);
  folder.write("src/movie/op1/video.y4m", y4m);
  folder.write("src/movie/op1/movie.yaml", "source: MOVIE/OP1.STR\n");
  const CookSummary summary = cooked(folder);
  CHECK(summary.assets == 6);
  CHECK(summary.cooked == 4);
  CHECK(summary.copied == 2);
  const std::filesystem::path out = folder.path() / "out";
  CHECK(readOrEmpty(out / "movie/op1/movie.yaml") ==
        readOrEmpty(folder.path() / "src/movie/op1/movie.yaml"));
  CHECK_FALSE(std::filesystem::exists(out / "movie/op1/video.y4m"));
  CHECK_FALSE(std::filesystem::exists(out / "movie/op1/audio.wav"));

  const auto sound = dwe::assets::decodeOpus(readOrEmpty(out / "movie/op1/audio.opus"));
  REQUIRE_MESSAGE(sound.has_value(), (sound ? std::string{} : sound.error().message));
  // A tenth of a second at 37 800 Hz, resampled to 48 kHz.
  CHECK(sound->pcm.size() == 4800);

  // The engine's reader gives back every frame at the source's size, close to the source.
  const auto ivf = readOrEmpty(out / "movie/op1/video.ivf");
  const auto opened = dwe::assets::openAsset("ivf", ivf);
  CHECK_MESSAGE(opened.has_value(), (opened ? std::string{} : opened.error().message));
  const DiskFileSystem disk;
  auto file = disk.openFile(out / "movie/op1/video.ivf");
  REQUIRE(file.has_value());
  auto reader = dwe::video::Av1Reader::open(std::move(*file), "video.ivf");
  REQUIRE_MESSAGE(reader.has_value(), (reader ? std::string{} : reader.error().message));
  CHECK(reader->stream().width == Width);
  CHECK(reader->stream().height == Height);
  dwe::video::YuvPicture picture;
  std::uint32_t frames = 0;
  for (;;) {
    const auto read = reader->read(picture);
    REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
    if (!*read) {
      break;
    }
    const std::size_t lumaStart = y4m.find("FRAME\n", 0);
    std::size_t at = lumaStart;
    for (std::uint32_t skipped = 0; skipped < frames; ++skipped) {
      at = y4m.find("FRAME\n", at + 1);
    }
    at += 6;
    double error = 0.0;
    for (std::size_t i = 0; i < picture.luma.size(); ++i) {
      error += std::abs(static_cast<int>(picture.luma[i]) -
                        static_cast<int>(static_cast<std::uint8_t>(y4m[at + i])));
    }
    CHECK(error / static_cast<double>(picture.luma.size()) < 4.0);
    ++frames;
  }
  CHECK(frames == Frames);
}

TEST_CASE("the same movie cooks to the same AV1 bytes") {
  const std::string y4m = movieY4m(32, 16, 3);
  const auto bytes = std::as_bytes(std::span{y4m});
  const auto first = cookVideo(bytes, {});
  const auto second = cookVideo(bytes, {});
  REQUIRE_MESSAGE(first.has_value(), (first ? std::string{} : first.error().message));
  REQUIRE(second.has_value());
  CHECK(*first == *second);
  CHECK(u32At(*first, 24) == 3);
}

// The same gradient as movieY4m in a 10-bit stream (C420p10), as dwx extract now writes the movies:
// each luma sample four times the 8-bit one plus a fraction the 8-bit stream cannot hold.
std::string movieY4m10(std::uint32_t width, std::uint32_t height, std::uint32_t frames) {
  std::string y4m =
      std::format("YUV4MPEG2 W{} H{} F30:1 Ip A1:1 C420p10 XCOLORRANGE=FULL\n", width, height);
  const auto put = [&](unsigned sample) {
    y4m += static_cast<char>(sample & 0xFFU);
    y4m += static_cast<char>(sample >> 8U);
  };
  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    y4m += "FRAME\n";
    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        put(((((x + frame) * 1020) / width) + ((y * 256) / height) + (x % 4)) & 0x3FFU);
      }
    }
    for (std::size_t i = 0; i < 2 * std::size_t{(width + 1) / 2} * ((height + 1) / 2); ++i) {
      put(512);
    }
  }
  return y4m;
}

TEST_CASE("a 10-bit movie cooks to 10-bit AV1 that the engine reads at full depth") {
  constexpr std::uint32_t Width = 64;
  constexpr std::uint32_t Height = 48;
  const std::string y4m = movieY4m10(Width, Height, 2);
  const auto cooked = cookVideo(std::as_bytes(std::span{y4m}), {});
  REQUIRE_MESSAGE(cooked.has_value(), (cooked ? std::string{} : cooked.error().message));
  CHECK(u32At(*cooked, 24) == 2);
  const TempFolder folder;
  const auto path = folder.path() / "video.ivf";
  folder.write("video.ivf",
               std::string{reinterpret_cast<const char*>(cooked->data()), cooked->size()});
  const DiskFileSystem disk;
  auto file = disk.openFile(path);
  REQUIRE(file.has_value());
  auto reader = dwe::video::Av1Reader::open(std::move(*file), "video.ivf");
  REQUIRE_MESSAGE(reader.has_value(), (reader ? std::string{} : reader.error().message));
  dwe::video::YuvPicture picture;
  const auto read = reader->read(picture);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  REQUIRE(*read);
  CHECK(picture.bitDepth == 10);
  const std::size_t start = y4m.find("FRAME\n") + 6;
  double error = 0.0;
  for (std::size_t i = 0; i < picture.luma.size(); ++i) {
    const unsigned source =
        static_cast<unsigned char>(y4m[start + (2 * i)]) |
        (static_cast<unsigned>(static_cast<unsigned char>(y4m[start + (2 * i) + 1])) << 8U);
    error += std::abs(static_cast<int>(picture.luma[i]) - static_cast<int>(source));
  }
  // Within a quarter of an 8-bit level on average.
  CHECK(error / static_cast<double>(picture.luma.size()) < 8.0);
  CHECK(std::ranges::all_of(picture.blue,
                            [](std::uint16_t sample) { return std::abs(int{sample} - 512) <= 2; }));
}

TEST_CASE("pictures convert to RGB with full-range BT.601 and interpolated chroma") {
  dwe::video::YuvPicture picture{.width = 4,
                                 .height = 2,
                                 .bitDepth = 10,
                                 .luma = std::vector<std::uint16_t>(8, 514),
                                 .blue = {512, 512},
                                 .red = {512, 1021}};
  std::vector<std::byte> rgba;
  dwe::video::toRgba(picture, rgba);
  REQUIRE(rgba.size() == 32);
  const auto red = [&](std::size_t x) { return std::to_integer<int>(rgba[x * 4]); };
  const auto green = [&](std::size_t x) { return std::to_integer<int>(rgba[(x * 4) + 1]); };
  // Grey on the left (128), the red chroma rising to the right through 3/4 - 1/4 mixes.
  CHECK(red(0) == 128);
  CHECK(green(0) == 128);
  CHECK(std::to_integer<int>(rgba[3]) == 255);
  CHECK(red(1) > red(0));
  CHECK(red(2) > red(1));
  CHECK(red(3) == 255);
  CHECK(green(3) < green(2));
  // Cr 1021 is +126.9 levels: R = 128.1 + 1.402 * 126.9 saturates, G = 128.1 - 0.714 * 126.9.
  CHECK(green(3) == 38);
  // The 8-bit grey of an old extraction converts to the same grey.
  picture.bitDepth = 8;
  picture.luma.assign(8, 128);
  picture.blue = {128, 128};
  picture.red = {128, 128};
  dwe::video::toRgba(picture, rgba);
  CHECK(std::ranges::all_of(std::span{rgba}.first(3),
                            [](std::byte value) { return std::to_integer<int>(value) == 128; }));
}

TEST_CASE("the video cooker turns down what is not an 8- or 10-bit 4:2:0 progressive Y4M") {
  const auto fails = [](std::string_view text) {
    return !cookVideo(std::as_bytes(std::span{text}), {}).has_value();
  };
  CHECK(fails("not a video\n"));
  CHECK(fails("YUV4MPEG2 W4 H4 F30:1 Ip C444\nFRAME\n"));
  CHECK(fails("YUV4MPEG2 W4 H4 F30:1 Ip C420p12\nFRAME\n"));
  // A 10-bit frame takes two bytes a sample.
  CHECK(fails("YUV4MPEG2 W4 H4 F30:1 Ip C420p10\nFRAME\n" + std::string(24, '\0')));
  CHECK(fails("YUV4MPEG2 W4 H4 F30:1 It C420jpeg\nFRAME\n"));
  CHECK(fails("YUV4MPEG2 W4 H4 F30:1 Ip C420jpeg\n"));
  // A frame cut short.
  CHECK(fails("YUV4MPEG2 W4 H4 F30:1 Ip C420jpeg\nFRAME\nabc"));
}
