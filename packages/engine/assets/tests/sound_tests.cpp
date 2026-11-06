#include <dwe/assets/sound.hpp>

#include <doctest/doctest.h>
#include <opus.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "ogg.hpp"

using dwe::assets::decodeOpus;
using dwe::assets::readOggStream;
using dwe::assets::SoundLoop;

namespace {

using Bytes = std::vector<std::byte>;

constexpr std::uint8_t FirstPage = 0x02;
constexpr std::uint8_t LastPage = 0x04;
constexpr std::uint8_t Continued = 0x01;
constexpr std::uint32_t Serial = 0x1234'5678;
constexpr int PacketFrames = 960;

void put(Bytes& bytes, std::uint64_t value, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    bytes.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
  }
}

void putText(Bytes& bytes, std::string_view text) {
  for (const char c : text) {
    bytes.push_back(static_cast<std::byte>(c));
  }
}

// The Ogg CRC-32, bit by bit: independent of the table the reader uses.
std::uint32_t crcOf(const Bytes& page) {
  std::uint32_t crc = 0;
  for (const std::byte byte : page) {
    crc ^= std::to_integer<std::uint32_t>(byte) << 24U;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000'0000U) != 0 ? (crc << 1U) ^ 0x04C1'1DB7U : crc << 1U;
    }
  }
  return crc;
}

// One Ogg page with these segments (each at most 255 bytes) and a correct CRC.
Bytes page(std::uint8_t type, std::int64_t granule, std::uint32_t sequence,
           const std::vector<Bytes>& segments) {
  Bytes bytes;
  putText(bytes, "OggS");
  put(bytes, 0, 1);
  put(bytes, type, 1);
  put(bytes, static_cast<std::uint64_t>(granule), 8);
  put(bytes, Serial, 4);
  put(bytes, sequence, 4);
  put(bytes, 0, 4);
  put(bytes, segments.size(), 1);
  for (const Bytes& segment : segments) {
    put(bytes, segment.size(), 1);
  }
  for (const Bytes& segment : segments) {
    bytes.insert(bytes.end(), segment.begin(), segment.end());
  }
  const std::uint32_t crc = crcOf(bytes);
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[22 + i] = static_cast<std::byte>((crc >> (8U * i)) & 0xFFU);
  }
  return bytes;
}

// Splits a packet into the segments of its lacing: 255-byte pieces, then the rest (maybe empty).
std::vector<Bytes> lace(const Bytes& packet) {
  std::vector<Bytes> segments;
  std::size_t at = 0;
  while (packet.size() - at >= 255) {
    segments.emplace_back(packet.begin() + static_cast<std::ptrdiff_t>(at),
                          packet.begin() + static_cast<std::ptrdiff_t>(at + 255));
    at += 255;
  }
  segments.emplace_back(packet.begin() + static_cast<std::ptrdiff_t>(at), packet.end());
  return segments;
}

Bytes concat(const std::vector<Bytes>& parts) {
  Bytes all;
  for (const Bytes& part : parts) {
    all.insert(all.end(), part.begin(), part.end());
  }
  return all;
}

struct Encoded {
  std::vector<Bytes> packets;
  std::uint16_t preSkip = 0;
};

// Encodes PCM at 48 kHz in 20 ms packets, the last one padded with silence.
Encoded encode(const std::vector<std::int16_t>& pcm, int channels) {
  int error = OPUS_OK;
  OpusEncoder* encoder = opus_encoder_create(48000, channels, OPUS_APPLICATION_AUDIO, &error);
  REQUIRE(error == OPUS_OK);
  opus_int32 lookahead = 0;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): Opus's ctl interface is variadic.
  opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&lookahead));
  Encoded encoded{.packets = {}, .preSkip = static_cast<std::uint16_t>(lookahead)};
  std::vector<std::int16_t> padded(pcm);
  const std::size_t frameSamples = std::size_t{PacketFrames} * static_cast<std::size_t>(channels);
  padded.resize(
      ((padded.size() + (static_cast<std::size_t>(lookahead) * static_cast<std::size_t>(channels)) +
        frameSamples - 1) /
       frameSamples) *
      frameSamples);
  std::vector<unsigned char> out(4000);
  for (std::size_t at = 0; at < padded.size(); at += frameSamples) {
    const int size = opus_encode(encoder, &padded[at], PacketFrames, out.data(),
                                 static_cast<opus_int32>(out.size()));
    REQUIRE(size > 0);
    Bytes packet(static_cast<std::size_t>(size));
    for (std::size_t i = 0; i < packet.size(); ++i) {
      packet[i] = std::byte{out[i]};
    }
    encoded.packets.push_back(std::move(packet));
  }
  opus_encoder_destroy(encoder);
  return encoded;
}

Bytes opusHead(int channels, std::uint16_t preSkip) {
  Bytes head;
  putText(head, "OpusHead");
  put(head, 1, 1);
  put(head, static_cast<std::uint64_t>(channels), 1);
  put(head, preSkip, 2);
  put(head, 22050, 4);
  put(head, 0, 2);
  put(head, 0, 1);
  return head;
}

Bytes opusTags(const std::vector<std::string>& comments) {
  Bytes tags;
  putText(tags, "OpusTags");
  put(tags, 4, 4);
  putText(tags, "test");
  put(tags, comments.size(), 4);
  for (const std::string& comment : comments) {
    put(tags, comment.size(), 4);
    putText(tags, comment);
  }
  return tags;
}

// An Ogg Opus file of the PCM: the two header pages, then every audio packet on one last page.
Bytes opusFile(const std::vector<std::int16_t>& pcm, int channels,
               const std::vector<std::string>& comments, std::int64_t extraGranule = 0) {
  const Encoded encoded = encode(pcm, channels);
  std::vector<Bytes> audio;
  for (const Bytes& packet : encoded.packets) {
    const auto segments = lace(packet);
    audio.insert(audio.end(), segments.begin(), segments.end());
  }
  REQUIRE(audio.size() <= 255);
  const auto frames = static_cast<std::int64_t>(pcm.size() / static_cast<std::size_t>(channels));
  return concat({page(FirstPage, 0, 0, {opusHead(channels, encoded.preSkip)}),
                 page(0, 0, 1, lace(opusTags(comments))),
                 page(LastPage, encoded.preSkip + frames + extraGranule, 2, audio)});
}

std::vector<std::int16_t> sine(std::size_t frames, int channels) {
  std::vector<std::int16_t> pcm;
  for (std::size_t i = 0; i < frames; ++i) {
    for (int channel = 0; channel < channels; ++channel) {
      const double phase =
          2.0 * std::numbers::pi * 440.0 * (1.0 + channel) * static_cast<double>(i) / 48000.0;
      pcm.push_back(static_cast<std::int16_t>(8000.0 * std::sin(phase)));
    }
  }
  return pcm;
}

// The share of the signal's energy that the decoded copy gets wrong.
double errorShare(const std::vector<std::int16_t>& original,
                  const std::vector<std::int16_t>& decoded) {
  double signal = 0;
  double noise = 0;
  for (std::size_t i = 0; i < original.size(); ++i) {
    const double difference = static_cast<double>(original[i]) - static_cast<double>(decoded[i]);
    signal += static_cast<double>(original[i]) * original[i];
    noise += difference * difference;
  }
  return noise / signal;
}

std::string decodeError(const Bytes& file) {
  const auto sound = decodeOpus(file);
  REQUIRE_FALSE(sound.has_value());
  return sound.error().message;
}

} // namespace

TEST_CASE("an Ogg Opus file decodes to its length, trimmed by the pre-skip, with its loop") {
  const auto pcm = sine(4800, 1);
  const auto sound = decodeOpus(
      opusFile(pcm, 1, {"LOOPSTART=100", "looplength=2000", "DWE_UNITY_NOTE=48", "TITLE=x"}));
  REQUIRE(sound.has_value());
  CHECK(sound->sampleRate == 48000);
  CHECK(sound->channels == 1);
  CHECK(sound->sourceRate == 22050);
  REQUIRE(sound->pcm.size() == pcm.size());
  CHECK(errorShare(pcm, sound->pcm) < 0.05);
  CHECK(sound->loop == SoundLoop{.start = 100, .length = 2000});
  CHECK(sound->unityNote == 48);

  const auto stereoPcm = sine(3000, 2);
  const auto stereo = decodeOpus(opusFile(stereoPcm, 2, {}));
  REQUIRE(stereo.has_value());
  CHECK(stereo->channels == 2);
  REQUIRE(stereo->pcm.size() == stereoPcm.size());
  CHECK(errorShare(stereoPcm, stereo->pcm) < 0.05);
  CHECK_FALSE(stereo->loop.has_value());
  CHECK(stereo->unityNote == 60);
}

TEST_CASE("Ogg pages are checked and packets are joined across them") {
  Bytes big(600, std::byte{7});
  const auto segments = lace(big);
  REQUIRE(segments.size() == 3);
  const Bytes split =
      concat({page(FirstPage, 0, 0, {segments[0], segments[1]}),
              page(Continued | LastPage, 5, 1, {segments[2], Bytes{std::byte{1}}})});
  const auto stream = readOggStream(split);
  REQUIRE(stream.has_value());
  CHECK(stream->serial == Serial);
  REQUIRE(stream->packets.size() == 2);
  CHECK(stream->packets[0] == big);
  CHECK(stream->packets[1] == Bytes{std::byte{1}});
  CHECK(stream->lastGranule == 5);

  const auto errorOf = [](const Bytes& bytes) {
    const auto read = readOggStream(bytes);
    REQUIRE_FALSE(read.has_value());
    return read.error().message;
  };
  Bytes damaged = split;
  damaged[40] = std::byte{8};
  CHECK(errorOf(damaged) == "the Ogg page at 0 does not match its CRC");
  CHECK(errorOf(Bytes(split.begin(), split.end() - 1)).ends_with("is cut short"));
  CHECK(errorOf(page(FirstPage, 0, 0, {Bytes{std::byte{1}}})) == "the Ogg stream does not end");
  CHECK(errorOf(concat({page(FirstPage, 0, 0, {segments[0]}), page(LastPage, 0, 1, {Bytes{}})}))
            .ends_with("drops a packet halfway"));
  CHECK(errorOf(concat({page(FirstPage, 0, 0, {Bytes{}}), page(LastPage, 0, 2, {Bytes{}})}))
            .ends_with("is not the next page of the stream"));
  CHECK(errorOf(page(LastPage, 0, 0, {Bytes{}})) == "the Ogg page at 0 does not start the stream");
  CHECK(errorOf(
            concat({page(FirstPage | LastPage, 0, 0, {Bytes{}}), page(LastPage, 0, 1, {Bytes{}})}))
            .ends_with("follows the end of the stream"));
  CHECK(errorOf({}) == "the Ogg stream does not end");
}

TEST_CASE("Ogg Opus files that do not hold together are refused") {
  const auto pcm = sine(1000, 1);
  CHECK(decodeError(opusFile(pcm, 1, {"LOOPSTART=900", "LOOPLENGTH=101"})) ==
        "its loop does not fit in the sound");
  CHECK(decodeError(opusFile(pcm, 1, {"LOOPSTART=10"})) == "its loop does not fit in the sound");
  CHECK(decodeError(opusFile(pcm, 1, {"LOOPSTART=ten"})) ==
        "its comment LOOPSTART is not a number");
  CHECK(decodeError(opusFile(pcm, 1, {"DWE_UNITY_NOTE=200"})) ==
        "its unity note 200 is not a MIDI note");
  CHECK(decodeError(opusFile(pcm, 1, {}, 5000)) ==
        "it decodes to fewer frames than its last page says");

  const Bytes notHead =
      concat({page(FirstPage, 0, 0, {opusTags({})}), page(LastPage, 0, 1, {opusTags({})})});
  CHECK(decodeError(notHead) == "its first packet is not an OpusHead");
  Bytes surround = opusHead(1, 312);
  surround[18] = std::byte{1};
  CHECK(decodeError(
            concat({page(FirstPage, 0, 0, {surround}), page(LastPage, 312, 1, {opusTags({})})})) ==
        "only mono and stereo Opus (channel mapping family 0) are read");
  CHECK(decodeError(page(FirstPage | LastPage, 0, 0, {opusHead(1, 312)})) ==
        "the Ogg stream has no Opus headers");
  CHECK(decodeError(concat({page(FirstPage, 0, 0, {opusHead(1, 312)}),
                            page(LastPage, 0, 1, {opusTags({})})}))
            .starts_with("its length"));
}
