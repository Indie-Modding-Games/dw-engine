#include <dwe/formats/audio/seq.hpp>
#include <dwe/formats/byte_reader.hpp>

#include <format>
#include <limits>
#include <optional>
#include <string_view>

namespace dwe::formats::audio {

namespace {

constexpr std::string_view SeqMagic = "pQES";
constexpr std::size_t HeaderSize = 15;
constexpr std::size_t MaxDeltaBytes = 4;
constexpr std::uint8_t MetaStatus = 0xFF;
constexpr std::uint8_t TempoMeta = 0x51;
constexpr std::uint8_t EndMeta = 0x2F;
constexpr std::uint8_t SystemStatus = 0xF0;
constexpr std::size_t SeqOffsetSize = 4;

// Reads the SEQ's bytes in order; every read checks the end.
class SeqCursor {
public:
  explicit SeqCursor(std::span<const std::byte> bytes) : reader_(bytes) {}

  [[nodiscard]] std::size_t offset() const { return offset_; }

  [[nodiscard]] std::optional<std::uint8_t> peek() const { return reader_.u8(offset_); }

  std::optional<std::uint8_t> next() {
    const auto value = reader_.u8(offset_);
    if (value) {
      ++offset_;
    }
    return value;
  }

  // A data byte: present and below 0x80.
  std::optional<std::uint8_t> data() {
    const auto value = next();
    return value && *value < 0x80 ? value : std::nullopt;
  }

  std::optional<std::uint32_t> delta() {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < MaxDeltaBytes; ++i) {
      const auto byte = next();
      if (!byte) {
        return std::nullopt;
      }
      value = (value << 7U) | (*byte & 0x7FU);
      if ((*byte & 0x80U) == 0) {
        return value;
      }
    }
    return std::nullopt;
  }

private:
  ByteReader reader_;
  std::size_t offset_ = HeaderSize;
};

SeqEventType channelEventType(std::uint8_t status) {
  switch (status & 0xF0U) {
  case 0x80:
    return SeqEventType::NoteOff;
  case 0x90:
    return SeqEventType::NoteOn;
  case 0xA0:
    return SeqEventType::KeyPressure;
  case 0xB0:
    return SeqEventType::Control;
  case 0xC0:
    return SeqEventType::Program;
  case 0xD0:
    return SeqEventType::ChannelPressure;
  default:
    return SeqEventType::PitchBend;
  }
}

bool hasTwoDataBytes(SeqEventType type) {
  return type != SeqEventType::Program && type != SeqEventType::ChannelPressure;
}

bool isControl(const SeqEvent& event, std::uint8_t controller) {
  return event.type == SeqEventType::Control && event.data1 == controller;
}

// Turns the NRPN loop markers into LoopStart (CC99 = 20 and the CC6 right after it on the same
// channel) and LoopEnd (CC99 = 30).
std::vector<SeqEvent> foldLoopMarkers(const std::vector<SeqEvent>& events) {
  std::vector<SeqEvent> folded;
  for (std::size_t i = 0; i < events.size(); ++i) {
    const SeqEvent& event = events[i];
    if (isControl(event, SeqLoopControl::NrpnMsb) && event.data2 == SeqLoopControl::LoopStart &&
        i + 1 < events.size() && isControl(events[i + 1], SeqLoopControl::DataEntry) &&
        events[i + 1].channel == event.channel) {
      folded.push_back(SeqEvent{.tick = event.tick,
                                .type = SeqEventType::LoopStart,
                                .channel = event.channel,
                                .data1 = events[i + 1].data2,
                                .data2 = 0,
                                .tempo = 0});
      ++i;
    } else if (isControl(event, SeqLoopControl::NrpnMsb) &&
               event.data2 == SeqLoopControl::LoopEnd) {
      folded.push_back(SeqEvent{.tick = event.tick,
                                .type = SeqEventType::LoopEnd,
                                .channel = event.channel,
                                .data1 = 0,
                                .data2 = 0,
                                .tempo = 0});
    } else {
      folded.push_back(event);
    }
  }
  return folded;
}

} // namespace

core::Result<Seq> readSeq(std::span<const std::byte> bytes) {
  const ByteReader reader{bytes};
  if (reader.chars(0, SeqMagic.size()) != SeqMagic) {
    return core::fail("not a SEQ: it does not start with pQES");
  }
  if (bytes.size() < HeaderSize) {
    return core::fail(std::format("SEQ of {} bytes is shorter than its header", bytes.size()));
  }
  const auto byteAt = [&reader](std::size_t offset) {
    return std::uint32_t{reader.u8(offset).value_or(0)};
  };
  Seq seq;
  seq.header.version = (byteAt(4) << 24U) | (byteAt(5) << 16U) | (byteAt(6) << 8U) | byteAt(7);
  seq.header.resolution = static_cast<std::uint16_t>((byteAt(8) << 8U) | byteAt(9));
  seq.header.tempo = (byteAt(10) << 16U) | (byteAt(11) << 8U) | byteAt(12);
  seq.header.rhythmNumerator = static_cast<std::uint8_t>(byteAt(13));
  seq.header.rhythmDenominator = static_cast<std::uint8_t>(byteAt(14));
  if (seq.header.resolution == 0 || seq.header.resolution >= 0x8000) {
    return core::fail(std::format("SEQ resolution {} is not a number of ticks per quarter note",
                                  seq.header.resolution));
  }

  SeqCursor cursor{bytes};
  std::vector<SeqEvent> events;
  std::uint64_t tick = 0;
  std::uint8_t status = 0;
  while (true) {
    const std::size_t eventOffset = cursor.offset();
    const auto delta = cursor.delta();
    if (!delta) {
      return core::fail(std::format(
          "SEQ event at 0x{:x}: the delta time is cut or longer than 4 bytes", eventOffset));
    }
    tick += *delta;
    if (tick > std::numeric_limits<std::uint32_t>::max()) {
      return core::fail(
          std::format("SEQ event at 0x{:x} is past the last tick a SEQ can hold", eventOffset));
    }
    const auto first = cursor.peek();
    if (!first) {
      return core::fail("SEQ ends without its end event");
    }
    if ((*first & 0x80U) != 0) {
      status = *first;
      cursor.next();
    } else if (status == 0) {
      return core::fail(
          std::format("SEQ event at 0x{:x} has no status and none runs", eventOffset));
    }
    SeqEvent event{.tick = static_cast<std::uint32_t>(tick),
                   .type = SeqEventType::End,
                   .channel = 0,
                   .data1 = 0,
                   .data2 = 0,
                   .tempo = 0};
    if (status == MetaStatus) {
      status = 0;
      const auto meta = cursor.next();
      if (meta == TempoMeta) {
        const auto b0 = cursor.next();
        const auto b1 = cursor.next();
        const auto b2 = cursor.next();
        if (!b0 || !b1 || !b2) {
          return core::fail("SEQ ends inside a tempo event");
        }
        event.type = SeqEventType::Tempo;
        event.tempo = (std::uint32_t{*b0} << 16U) | (std::uint32_t{*b1} << 8U) | *b2;
        events.push_back(event);
        continue;
      }
      if (meta == EndMeta && cursor.next() == 0) {
        events.push_back(event);
        break;
      }
      return core::fail(std::format(
          "SEQ event at 0x{:x} is a meta event the sound library does not play", eventOffset));
    }
    if (status >= SystemStatus) {
      return core::fail(std::format(
          "SEQ event at 0x{:x} has status 0x{:02X}, which a SEQ cannot hold", eventOffset, status));
    }
    event.type = channelEventType(status);
    event.channel = status & 0x0FU;
    const auto data1 = cursor.data();
    const auto data2 = hasTwoDataBytes(event.type) ? cursor.data() : std::optional<std::uint8_t>{0};
    if (!data1 || !data2) {
      return core::fail(
          std::format("SEQ event at 0x{:x} is cut or has a data byte above 0x7F", eventOffset));
    }
    event.data1 = *data1;
    event.data2 = *data2;
    events.push_back(event);
  }
  seq.events = foldLoopMarkers(events);
  seq.size = cursor.offset();
  return seq;
}

core::Result<std::vector<std::span<const std::byte>>>
splitSeqBank(std::span<const std::byte> bank) {
  const ByteReader reader{bank};
  const auto first = reader.u32(0);
  if (!first || *first < SeqOffsetSize || *first % SeqOffsetSize != 0 || *first > bank.size()) {
    return core::fail(
        std::format("SEQ bank of {} bytes does not start with a table of offsets", bank.size()));
  }
  const std::size_t count = *first / SeqOffsetSize;
  std::vector<std::span<const std::byte>> seqs;
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint32_t start = reader.u32(i * SeqOffsetSize).value_or(0);
    const std::uint32_t end = i + 1 < count ? reader.u32((i + 1) * SeqOffsetSize).value_or(0)
                                            : static_cast<std::uint32_t>(bank.size());
    if (start < *first || end < start || end > bank.size()) {
      return core::fail(
          std::format("SEQ {} of the bank spans 0x{:x}-0x{:x}, outside the bank after its table", i,
                      start, end));
    }
    seqs.push_back(bank.subspan(start, end - start));
  }
  return seqs;
}

} // namespace dwe::formats::audio
