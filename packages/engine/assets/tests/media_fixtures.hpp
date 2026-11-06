#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Small files of every type the engine opens, built byte by byte, for the tests of the assets
// module and of dwx.
namespace dwe::assets::testing {

class ByteWriter {
public:
  ByteWriter& text(std::string_view text) {
    for (const char c : text) {
      bytes_.push_back(static_cast<std::byte>(c));
    }
    return *this;
  }

  ByteWriter& u16(std::uint16_t value) { return little(value, 2); }
  ByteWriter& u32(std::uint32_t value) { return little(value, 4); }

  ByteWriter& u16be(std::uint16_t value) { return big(value, 2); }
  ByteWriter& u32be(std::uint32_t value) { return big(value, 4); }

  ByteWriter& zeros(std::size_t count) {
    bytes_.insert(bytes_.end(), count, std::byte{0});
    return *this;
  }

  ByteWriter& bytes(const std::vector<std::byte>& more) {
    bytes_.insert(bytes_.end(), more.begin(), more.end());
    return *this;
  }

  // A RIFF chunk: tag, size and body, padded to an even size.
  ByteWriter& chunk(std::string_view tag, const std::vector<std::byte>& body) {
    text(tag).u32(static_cast<std::uint32_t>(body.size())).bytes(body);
    return body.size() % 2 == 0 ? *this : zeros(1);
  }

  [[nodiscard]] const std::vector<std::byte>& data() const { return bytes_; }

private:
  ByteWriter& little(std::uint32_t value, int size) {
    for (int i = 0; i < size; ++i) {
      bytes_.push_back(static_cast<std::byte>((value >> (8U * static_cast<unsigned>(i))) & 0xFFU));
    }
    return *this;
  }

  ByteWriter& big(std::uint32_t value, int size) {
    for (int i = size - 1; i >= 0; --i) {
      bytes_.push_back(static_cast<std::byte>((value >> (8U * static_cast<unsigned>(i))) & 0xFFU));
    }
    return *this;
  }

  std::vector<std::byte> bytes_;
};

// A RIFF file of the given form with these chunks already written.
inline std::vector<std::byte> riffFile(std::string_view form,
                                       const std::vector<std::byte>& chunks) {
  return ByteWriter{}
      .text("RIFF")
      .u32(static_cast<std::uint32_t>(4 + chunks.size()))
      .text(form)
      .bytes(chunks)
      .data();
}

// A 16-bit mono PCM WAV with the given number of frames.
inline std::vector<std::byte> wavFile(std::uint32_t frames = 4) {
  const auto format = ByteWriter{}.u16(1).u16(1).u32(22050).u32(44100).u16(2).u16(16).data();
  return riffFile("WAVE", ByteWriter{}
                              .chunk("fmt ", format)
                              .chunk("data", ByteWriter{}.zeros(std::size_t{frames} * 2).data())
                              .data());
}

// A SoundFont with one preset and one sample, plus the terminal records.
inline std::vector<std::byte> soundFontFile() {
  const auto list = [](std::string_view type, const std::vector<std::byte>& chunks) {
    return ByteWriter{}.chunk("LIST", ByteWriter{}.text(type).bytes(chunks).data()).data();
  };
  ByteWriter tables;
  for (const auto& [tag, size] :
       {std::pair{"phdr", 38}, std::pair{"pbag", 4}, std::pair{"pmod", 10}, std::pair{"pgen", 4},
        std::pair{"inst", 22}, std::pair{"ibag", 4}, std::pair{"imod", 10}, std::pair{"igen", 4},
        std::pair{"shdr", 46}}) {
    tables.chunk(tag, ByteWriter{}.zeros(static_cast<std::size_t>(size) * 2).data());
  }
  return riffFile(
      "sfbk",
      ByteWriter{}
          .bytes(list("INFO", ByteWriter{}.chunk("ifil", ByteWriter{}.u16(2).u16(4).data()).data()))
          .bytes(list("sdta", ByteWriter{}.chunk("smpl", ByteWriter{}.zeros(8).data()).data()))
          .bytes(list("pdta", tables.data()))
          .data());
}

// A MIDI file of format 1 with the given number of empty tracks.
inline std::vector<std::byte> midiFile(std::uint16_t tracks = 2) {
  ByteWriter midi;
  midi.text("MThd").u32be(6).u16be(1).u16be(tracks).u16be(480);
  for (std::uint16_t i = 0; i < tracks; ++i) {
    midi.text("MTrk").u32be(4).u32be(0x00FF2F00); // end of track
  }
  return midi.data();
}

// A YUV4MPEG2 video header and one empty frame line.
inline std::vector<std::byte> y4mFile() {
  return ByteWriter{}.text("YUV4MPEG2 W320 H240 F30:1 Ip A1:1 C420\nFRAME\n").data();
}

// A glTF JSON model with a skin of three joints under a node that is not one, an animation and two
// external files.
inline std::string skinnedGltf() {
  return R"({"asset": {"version": "2.0"},
        "buffers": [{"byteLength": 32, "uri": "anim.bin"}],
        "bufferViews": [{"buffer": 0, "byteLength": 32}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR"},
                      {"bufferView": 0, "byteOffset": 8, "componentType": 5126, "count": 2, "type": "VEC3"}],
        "nodes": [{"children": [1]}, {"children": [2]}, {}, {"children": [0]}],
        "skins": [{"joints": [0, 1, 2]}],
        "animations": [{"name": "walk", "channels": [{"sampler": 0, "target": {"node": 1, "path": "translation"}}],
                        "samplers": [{"input": 0, "output": 1}]}],
        "images": [{"uri": "skin.png"}, {"uri": "data:image/png;base64,AA=="}]})";
}

} // namespace dwe::assets::testing
