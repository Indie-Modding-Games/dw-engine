#include <dwe/assets/media_headers.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace dwe::assets {

namespace {

// Reads of a span of bytes. Callers check fits() first; past the end, the standard library's
// hardening stops the program rather than read out of bounds.
class Bytes {
public:
  explicit Bytes(std::span<const std::byte> bytes) : bytes_(bytes) {}

  [[nodiscard]] std::size_t size() const { return bytes_.size(); }

  // Whether size bytes start at offset.
  [[nodiscard]] bool fits(std::size_t offset, std::size_t size) const {
    return offset <= bytes_.size() && bytes_.size() - offset >= size;
  }

  [[nodiscard]] std::uint16_t u16(std::size_t offset) const {
    return static_cast<std::uint16_t>(read(offset, 2, false));
  }
  [[nodiscard]] std::uint32_t u32(std::size_t offset) const { return read(offset, 4, false); }
  [[nodiscard]] std::uint16_t u16be(std::size_t offset) const {
    return static_cast<std::uint16_t>(read(offset, 2, true));
  }
  [[nodiscard]] std::uint32_t u32be(std::size_t offset) const { return read(offset, 4, true); }

  // Whether the bytes at offset spell the ASCII tag.
  [[nodiscard]] bool hasTag(std::size_t offset, std::string_view tag) const {
    if (!fits(offset, tag.size())) {
      return false;
    }
    return std::ranges::equal(bytes_.subspan(offset, tag.size()), tag,
                              [](std::byte b, char c) { return b == static_cast<std::byte>(c); });
  }

  [[nodiscard]] std::span<const std::byte> span() const { return bytes_; }

private:
  [[nodiscard]] std::uint32_t read(std::size_t offset, std::size_t size, bool bigEndian) const {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
      const std::size_t at = bigEndian ? offset + i : offset + size - 1 - i;
      value = (value << 8U) | std::to_integer<std::uint32_t>(bytes_[at]);
    }
    return value;
  }

  std::span<const std::byte> bytes_;
};

// A chunk of a RIFF file: its four-character tag and its body, without the padding byte of odd
// sizes.
struct Chunk {
  std::string tag;
  std::span<const std::byte> body;
};

std::string tagAt(std::span<const std::byte> bytes, std::size_t offset) {
  std::string tag(4, ' ');
  std::ranges::transform(bytes.subspan(offset, 4), tag.begin(),
                         [](std::byte b) { return static_cast<char>(b); });
  return tag;
}

// Splits a run of chunks; each one has to fit in the bytes.
core::Result<std::vector<Chunk>> riffChunks(std::span<const std::byte> bytes) {
  const Bytes view{bytes};
  std::vector<Chunk> chunks;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    if (!view.fits(offset, 8) || !view.fits(offset + 8, view.u32(offset + 4))) {
      return core::fail(
          std::format("the chunk at byte {} goes past the end of its parent", offset));
    }
    const std::uint32_t size = view.u32(offset + 4);
    chunks.push_back({.tag = tagAt(bytes, offset), .body = bytes.subspan(offset + 8, size)});
    offset += 8 + std::size_t{size} + (size % 2);
  }
  return chunks;
}

const Chunk* findChunk(std::span<const Chunk> chunks, std::string_view tag) {
  const auto found = std::ranges::find(chunks, tag, &Chunk::tag);
  return found == chunks.end() ? nullptr : &*found;
}

// The chunks inside a RIFF file of the given form type, such as WAVE.
core::Result<std::vector<Chunk>> riffBody(std::span<const std::byte> bytes, std::string_view form) {
  const Bytes view{bytes};
  if (!view.hasTag(0, "RIFF") || !view.hasTag(8, form)) {
    return core::fail(std::format("not a RIFF {} file", form));
  }
  const std::uint32_t size = view.u32(4);
  if (size < 4 || size > bytes.size() - 8) {
    return core::fail(
        std::format("the RIFF size {} does not fit the file's {} bytes", size, bytes.size()));
  }
  return riffChunks(bytes.subspan(12, size - 4));
}

// The chunks of a LIST chunk of the given type.
core::Result<std::vector<Chunk>> listBody(std::span<const Chunk> chunks, std::string_view type) {
  for (const Chunk& chunk : chunks) {
    if (chunk.tag == "LIST" && Bytes{chunk.body}.hasTag(0, type)) {
      return riffChunks(chunk.body.subspan(4));
    }
  }
  return core::fail(std::format("the LIST {} chunk is missing", type));
}

constexpr std::uint16_t WaveFormatPcm = 1;
constexpr std::uint16_t WaveFormatFloat = 3;
constexpr std::uint16_t WaveFormatExtensible = 0xFFFE;

// The SoundFont 2.04 record tables of pdta and the size of their records.
constexpr std::array<std::pair<std::string_view, std::size_t>, 9> SoundFontTables = {{
    {"phdr", 38},
    {"pbag", 4},
    {"pmod", 10},
    {"pgen", 4},
    {"inst", 22},
    {"ibag", 4},
    {"imod", 10},
    {"igen", 4},
    {"shdr", 46},
}};

constexpr std::size_t MaxY4mHeader = 1024;

std::optional<std::uint32_t> parseDimension(std::string_view text) {
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
    return std::nullopt;
  }
  return value;
}

} // namespace

core::Result<WavInfo> readWavHeader(std::span<const std::byte> wav) {
  auto chunks = riffBody(wav, "WAVE");
  if (!chunks) {
    return std::unexpected{chunks.error()};
  }
  const Chunk* format = findChunk(*chunks, "fmt ");
  const Chunk* data = findChunk(*chunks, "data");
  if (format == nullptr || data == nullptr) {
    return core::fail("a WAV file needs a 'fmt ' and a 'data' chunk");
  }
  const Bytes fields{format->body};
  if (fields.size() < 16) {
    return core::fail("the 'fmt ' chunk is shorter than 16 bytes");
  }
  const std::uint16_t tag = fields.u16(0);
  const WavInfo info{
      .channels = fields.u16(2), .sampleRate = fields.u32(4), .bitsPerSample = fields.u16(14)};
  const std::uint16_t blockAlign = fields.u16(12);
  if (tag != WaveFormatPcm && tag != WaveFormatFloat && tag != WaveFormatExtensible) {
    return core::fail(std::format("WAV format {:#x} is neither PCM nor float", tag));
  }
  if (info.channels == 0 || info.sampleRate == 0 || info.bitsPerSample == 0 ||
      info.bitsPerSample % 8 != 0 || blockAlign != info.channels * (info.bitsPerSample / 8)) {
    return core::fail(
        std::format("a WAV file with {} channels, {} Hz, {} bits and blocks of {} bytes",
                    info.channels, info.sampleRate, info.bitsPerSample, blockAlign));
  }
  return WavInfo{.channels = info.channels,
                 .sampleRate = info.sampleRate,
                 .bitsPerSample = info.bitsPerSample,
                 .frames = data->body.size() / blockAlign};
}

core::Result<SoundFontInfo> readSoundFontHeader(std::span<const std::byte> sf2) {
  auto chunks = riffBody(sf2, "sfbk");
  if (!chunks) {
    return std::unexpected{chunks.error()};
  }
  auto info = listBody(*chunks, "INFO");
  if (!info) {
    return std::unexpected{info.error()};
  }
  if (const Chunk* version = findChunk(*info, "ifil");
      version == nullptr || version->body.size() != 4) {
    return core::fail("the INFO list has no 4-byte 'ifil' version");
  }
  if (auto samples = listBody(*chunks, "sdta"); !samples) {
    return std::unexpected{samples.error()};
  }
  auto presets = listBody(*chunks, "pdta");
  if (!presets) {
    return std::unexpected{presets.error()};
  }
  for (const auto& [tag, recordSize] : SoundFontTables) {
    const Chunk* table = findChunk(*presets, tag);
    if (table == nullptr || table->body.empty() || table->body.size() % recordSize != 0) {
      return core::fail(std::format("the pdta '{}' table is missing or not made of {}-byte records",
                                    tag, recordSize));
    }
  }
  // Both tables end with a terminal record.
  return SoundFontInfo{.presets = (findChunk(*presets, "phdr")->body.size() / 38) - 1,
                       .samples = (findChunk(*presets, "shdr")->body.size() / 46) - 1};
}

core::Result<MidiInfo> readMidiHeader(std::span<const std::byte> midi) {
  const Bytes view{midi};
  if (!view.hasTag(0, "MThd") || !view.fits(8, 6) || view.u32be(4) < 6 ||
      !view.fits(8, view.u32be(4))) {
    return core::fail("not a standard MIDI file");
  }
  const MidiInfo info{
      .format = view.u16be(8), .tracks = view.u16be(10), .division = view.u16be(12)};
  if (info.format > 2 || info.tracks == 0 || info.division == 0 ||
      (info.format == 0 && info.tracks != 1)) {
    return core::fail(std::format("a MIDI file of format {} with {} tracks and division {}",
                                  info.format, info.tracks, info.division));
  }
  std::size_t offset = 8 + std::size_t{view.u32be(4)};
  std::size_t tracks = 0;
  while (offset < midi.size()) {
    if (!view.fits(offset, 8) || !view.fits(offset + 8, view.u32be(offset + 4))) {
      return core::fail(
          std::format("the MIDI chunk at byte {} goes past the end of the file", offset));
    }
    tracks += view.hasTag(offset, "MTrk") ? 1 : 0;
    offset += 8 + std::size_t{view.u32be(offset + 4)};
  }
  if (tracks != info.tracks) {
    return core::fail(
        std::format("the header says {} tracks, the file has {}", info.tracks, tracks));
  }
  return info;
}

core::Result<VideoInfo> readY4mHeader(std::span<const std::byte> y4m) {
  constexpr std::string_view Signature = "YUV4MPEG2 ";
  const Bytes view{y4m};
  if (!view.hasTag(0, Signature)) {
    return core::fail("not a YUV4MPEG2 video");
  }
  const auto head = y4m.first(std::min(y4m.size(), MaxY4mHeader));
  const auto lineEnd = std::ranges::find(head, std::byte{'\n'});
  if (lineEnd == head.end()) {
    return core::fail(
        std::format("the YUV4MPEG2 header has no line end in its first {} bytes", MaxY4mHeader));
  }
  std::string line(static_cast<std::size_t>(lineEnd - head.begin()), ' ');
  std::ranges::transform(head.begin(), lineEnd, line.begin(),
                         [](std::byte b) { return static_cast<char>(b); });
  VideoInfo info;
  for (std::size_t start = Signature.size(); start < line.size();) {
    const std::size_t end = std::min(line.find(' ', start), line.size());
    const std::string_view token = std::string_view{line}.substr(start, end - start);
    if (token.starts_with('W') || token.starts_with('H')) {
      const auto value = parseDimension(token.substr(1));
      if (!value) {
        return core::fail(std::format("the YUV4MPEG2 parameter '{}' is not a size", token));
      }
      (token.front() == 'W' ? info.width : info.height) = *value;
    }
    start = end + 1;
  }
  if (info.width == 0 || info.height == 0) {
    return core::fail("the YUV4MPEG2 header has no width or height");
  }
  return info;
}

} // namespace dwe::assets
