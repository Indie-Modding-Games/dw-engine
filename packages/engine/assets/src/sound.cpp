#include <dwe/assets/sound.hpp>

#include <opus.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "ogg.hpp"

namespace dwe::assets {

namespace {

constexpr std::string_view HeadMagic = "OpusHead";
constexpr std::string_view TagsMagic = "OpusTags";
constexpr std::size_t HeadSize = 19;
// The most frames one Opus packet decodes to: 120 ms at 48 kHz.
constexpr int MaxPacketFrames = 5760;
constexpr std::uint32_t MaxComments = 1024;

struct Head {
  std::uint16_t channels = 0;
  std::uint16_t preSkip = 0;
  std::uint32_t sourceRate = 0;
  std::int16_t gain = 0;
};

struct Comments {
  std::optional<std::uint64_t> loopStart;
  std::optional<std::uint64_t> loopLength;
  std::optional<std::uint8_t> unityNote;
};

struct FreeDecoder {
  void operator()(OpusDecoder* decoder) const { opus_decoder_destroy(decoder); }
};

template <typename T>
T fieldAt(std::span<const std::byte> bytes, std::size_t offset) {
  T value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value = static_cast<T>(
        value | (static_cast<T>(std::to_integer<std::uint8_t>(bytes[offset + i])) << (8U * i)));
  }
  return value;
}

std::string_view textOf(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

core::Result<Head> readHead(std::span<const std::byte> packet) {
  if (packet.size() < HeadSize || textOf(packet.first(HeadMagic.size())) != HeadMagic) {
    return core::fail("its first packet is not an OpusHead");
  }
  const auto version = std::to_integer<std::uint8_t>(packet[8]);
  if ((version & 0xF0U) != 0) {
    return core::fail(std::format("OpusHead version {} is not supported", version));
  }
  const Head head{.channels = std::to_integer<std::uint16_t>(packet[9]),
                  .preSkip = fieldAt<std::uint16_t>(packet, 10),
                  .sourceRate = fieldAt<std::uint32_t>(packet, 12),
                  .gain = static_cast<std::int16_t>(fieldAt<std::uint16_t>(packet, 16))};
  if (std::to_integer<std::uint8_t>(packet[18]) != 0 || head.channels < 1 || head.channels > 2) {
    return core::fail("only mono and stereo Opus (channel mapping family 0) are read");
  }
  return head;
}

std::optional<std::uint64_t> numberOf(std::string_view text) {
  std::uint64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// Reads the comments dwx cook writes; the others are skipped. Comment names are case-insensitive.
core::Result<Comments> readTags(std::span<const std::byte> packet) {
  const auto fail = [] { return core::fail("its second packet is not a valid OpusTags"); };
  if (packet.size() < TagsMagic.size() + 8 || textOf(packet.first(TagsMagic.size())) != TagsMagic) {
    return fail();
  }
  std::size_t at = TagsMagic.size();
  const auto vendor = fieldAt<std::uint32_t>(packet, at);
  if (vendor > packet.size() - at - 8) {
    return fail();
  }
  at += 4 + vendor;
  const auto count = fieldAt<std::uint32_t>(packet, at);
  at += 4;
  if (count > MaxComments) {
    return fail();
  }
  Comments comments;
  for (std::uint32_t i = 0; i < count; ++i) {
    if (packet.size() - at < 4) {
      return fail();
    }
    const auto length = fieldAt<std::uint32_t>(packet, at);
    at += 4;
    if (length > packet.size() - at) {
      return fail();
    }
    const std::string_view comment = textOf(packet.subspan(at, length));
    at += length;
    const std::size_t equals = comment.find('=');
    if (equals == std::string_view::npos) {
      continue;
    }
    std::string name{comment.substr(0, equals)};
    std::ranges::transform(name, name.begin(), [](char c) {
      return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    const auto value = numberOf(comment.substr(equals + 1));
    if (name == "LOOPSTART" || name == "LOOPLENGTH" || name == "DWE_UNITY_NOTE") {
      if (!value) {
        return core::fail(std::format("its comment {} is not a number", name));
      }
      if (name == "LOOPSTART") {
        comments.loopStart = value;
      } else if (name == "LOOPLENGTH") {
        comments.loopLength = value;
      } else if (*value > 127) {
        return core::fail(std::format("its unity note {} is not a MIDI note", *value));
      } else {
        comments.unityNote = static_cast<std::uint8_t>(*value);
      }
    }
  }
  return comments;
}

} // namespace

core::Result<Sound> decodeOpus(std::span<const std::byte> file) {
  const auto stream = readOggStream(file);
  if (!stream) {
    return std::unexpected{stream.error()};
  }
  if (stream->packets.size() < 2) {
    return core::fail("the Ogg stream has no Opus headers");
  }
  const auto head = readHead(stream->packets[0]);
  if (!head) {
    return std::unexpected{head.error()};
  }
  const auto comments = readTags(stream->packets[1]);
  if (!comments) {
    return std::unexpected{comments.error()};
  }
  if (std::cmp_less(stream->lastGranule, head->preSkip) ||
      std::cmp_greater(stream->lastGranule - head->preSkip, MaxSoundFrames)) {
    return core::fail(std::format("its length ({} frames with a pre-skip of {}) is out of range",
                                  stream->lastGranule, head->preSkip));
  }
  const auto granule = static_cast<std::size_t>(stream->lastGranule);

  int error = OPUS_OK;
  const std::unique_ptr<OpusDecoder, FreeDecoder> decoder{
      opus_decoder_create(static_cast<opus_int32>(OpusSampleRate), head->channels, &error)};
  if (error != OPUS_OK || !decoder) {
    return core::fail(std::format("cannot create an Opus decoder: {}", opus_strerror(error)));
  }
  if (head->gain != 0) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): Opus's ctl interface is variadic.
    if (const int set = opus_decoder_ctl(decoder.get(), OPUS_SET_GAIN(head->gain));
        set != OPUS_OK) {
      return core::fail(
          std::format("cannot apply its output gain of {}: {}", head->gain, opus_strerror(set)));
    }
  }
  Sound sound{.sampleRate = OpusSampleRate,
              .channels = head->channels,
              .pcm = {},
              .loop = {},
              .unityNote = comments->unityNote.value_or(60),
              .sourceRate = head->sourceRate};
  std::vector<std::int16_t> frames(static_cast<std::size_t>(MaxPacketFrames) * head->channels);
  std::size_t decoded = 0;
  for (std::size_t i = 2; i < stream->packets.size(); ++i) {
    const auto& packet = stream->packets[i];
    const int count =
        opus_decode(decoder.get(), reinterpret_cast<const unsigned char*>(packet.data()),
                    static_cast<opus_int32>(packet.size()), frames.data(), MaxPacketFrames, 0);
    if (count < 0) {
      return core::fail(std::format("Opus packet {} does not decode: {}", i, opus_strerror(count)));
    }
    decoded += static_cast<std::size_t>(count);
    // The last packet may decode to more than the stream's length; any further would be damage.
    if (decoded - static_cast<std::size_t>(count) > granule) {
      return core::fail("it decodes to more frames than its last page says");
    }
    sound.pcm.insert(sound.pcm.end(), frames.begin(),
                     frames.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(count) *
                                                                  head->channels));
  }
  if (decoded < granule) {
    return core::fail("it decodes to fewer frames than its last page says");
  }
  // Drops the encoder's lead-in and the padding of the last packet.
  sound.pcm.resize(granule * head->channels);
  sound.pcm.erase(sound.pcm.begin(),
                  sound.pcm.begin() +
                      static_cast<std::ptrdiff_t>(std::size_t{head->preSkip} * head->channels));

  const std::uint64_t length = granule - head->preSkip;
  if (comments->loopStart || comments->loopLength) {
    if (!comments->loopStart || !comments->loopLength || *comments->loopLength == 0 ||
        *comments->loopStart > length || *comments->loopLength > length - *comments->loopStart) {
      return core::fail("its loop does not fit in the sound");
    }
    sound.loop = SoundLoop{.start = *comments->loopStart, .length = *comments->loopLength};
  }
  return sound;
}

} // namespace dwe::assets
