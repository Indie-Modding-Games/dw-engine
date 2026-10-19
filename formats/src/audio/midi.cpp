#include <dwe/formats/audio/midi.hpp>

#include <array>
#include <initializer_list>
#include <span>
#include <utility>

namespace dwe::formats::audio {

namespace {

constexpr std::uint8_t MetaStatus = 0xFF;
constexpr std::uint8_t TempoMeta = 0x51;
constexpr std::uint8_t MarkerMeta = 0x06;
constexpr std::uint8_t EndMeta = 0x2F;
constexpr std::uint8_t RpnMsb = 101;
constexpr std::uint8_t RpnLsb = 100;
constexpr std::uint8_t DataEntryMsb = 6;
constexpr std::uint8_t DataEntryLsb = 38;
constexpr std::uint8_t RpnNull = 127;

// The bytes of a MIDI track: events with their delta times.
class TrackWriter {
public:
    void event(std::uint32_t tick, std::initializer_list<std::uint8_t> bytes) {
        delta(tick);
        for (const auto value : bytes) {
            u8(value);
        }
    }

    void meta(std::uint32_t tick, std::uint8_t type, std::span<const std::uint8_t> data) {
        delta(tick);
        u8(MetaStatus);
        u8(type);
        quantity(static_cast<std::uint32_t>(data.size()));
        for (const auto value : data) {
            u8(value);
        }
    }

    void text(std::uint32_t tick, std::uint8_t type, std::string_view text) {
        std::vector<std::uint8_t> data;
        for (const char c : text) {
            data.push_back(static_cast<std::uint8_t>(c));
        }
        meta(tick, type, data);
    }

    std::vector<std::byte> take() { return std::move(bytes_); }

private:
    void u8(std::uint32_t value) { bytes_.push_back(static_cast<std::byte>(value & 0xFFU)); }

    void delta(std::uint32_t tick) {
        quantity(tick - lastTick_);
        lastTick_ = tick;
    }

    // A variable-length quantity: 7 bits per byte, most significant first, bit 7 set on all but the last.
    void quantity(std::uint32_t value) {
        std::array<std::uint8_t, 5> groups{};
        std::size_t count = 0;
        do {
            groups[count++] = static_cast<std::uint8_t>(value & 0x7FU);
            value >>= 7U;
        } while (value != 0);
        while (count > 1) {
            u8(groups[--count] | 0x80U);
        }
        u8(groups[0]);
    }

    std::vector<std::byte> bytes_;
    std::uint32_t lastTick_ = 0;
};

void appendBigEndian(std::vector<std::byte>& bytes, std::uint32_t value, std::size_t size) {
    for (std::size_t i = size; i > 0; --i) {
        bytes.push_back(static_cast<std::byte>((value >> (8U * (i - 1))) & 0xFFU));
    }
}

void appendTag(std::vector<std::byte>& bytes, std::string_view tag) {
    for (const char c : tag) {
        bytes.push_back(static_cast<std::byte>(c));
    }
}

std::array<std::uint8_t, 3> tempoBytes(std::uint32_t tempo) {
    return {static_cast<std::uint8_t>(tempo >> 16U), static_cast<std::uint8_t>(tempo >> 8U),
            static_cast<std::uint8_t>(tempo)};
}

void writeBendRange(TrackWriter& track, std::uint32_t tick, std::uint8_t channel, std::uint8_t semitones) {
    const auto control = static_cast<std::uint8_t>(0xB0U | channel);
    track.event(tick, {control, RpnMsb, 0});
    track.event(tick, {control, RpnLsb, 0});
    track.event(tick, {control, DataEntryMsb, semitones});
    track.event(tick, {control, DataEntryLsb, 0});
    track.event(tick, {control, RpnMsb, RpnNull});
    track.event(tick, {control, RpnLsb, RpnNull});
}

void writeEvent(TrackWriter& track, const SeqEvent& event, const BendRanges& bendRanges) {
    const auto channelStatus = [&event](std::uint8_t kind) { return static_cast<std::uint8_t>(kind | event.channel); };
    switch (event.type) {
    case SeqEventType::NoteOff:
        track.event(event.tick, {channelStatus(0x80), event.data1, event.data2});
        break;
    case SeqEventType::NoteOn:
        track.event(event.tick, {channelStatus(0x90), event.data1, event.data2});
        break;
    case SeqEventType::KeyPressure:
        track.event(event.tick, {channelStatus(0xA0), event.data1, event.data2});
        break;
    case SeqEventType::Control:
        track.event(event.tick, {channelStatus(0xB0), event.data1, event.data2});
        break;
    case SeqEventType::Program:
        track.event(event.tick, {channelStatus(0xC0), event.data1});
        if (const auto range = bendRanges[event.data1]) {
            writeBendRange(track, event.tick, event.channel, *range);
        }
        break;
    case SeqEventType::ChannelPressure:
        track.event(event.tick, {channelStatus(0xD0), event.data1});
        break;
    case SeqEventType::PitchBend:
        track.event(event.tick, {channelStatus(0xE0), event.data1, event.data2});
        break;
    case SeqEventType::Tempo:
        track.meta(event.tick, TempoMeta, tempoBytes(event.tempo));
        break;
    case SeqEventType::LoopStart:
        track.text(event.tick, MarkerMeta, MidiLoopStartMarker);
        break;
    case SeqEventType::LoopEnd:
        track.text(event.tick, MarkerMeta, MidiLoopEndMarker);
        break;
    case SeqEventType::End:
        track.meta(event.tick, EndMeta, {});
        break;
    }
}

} // namespace

BendRanges pitchBendRanges(const Vab& vab) {
    BendRanges ranges{};
    for (std::size_t program = 0; program < VabProgramSlots; ++program) {
        const auto tones = vab.tonesOf(program);
        if (!tones.empty()) {
            ranges[program] = tones.front().pbmax;
        }
    }
    return ranges;
}

std::vector<std::byte> convertSeqToMidi(const Seq& seq, const BendRanges& bendRanges) {
    TrackWriter track;
    track.meta(0, TempoMeta, tempoBytes(seq.header.tempo));
    for (const auto& event : seq.events) {
        writeEvent(track, event, bendRanges);
    }
    const auto trackBytes = track.take();

    std::vector<std::byte> file;
    appendTag(file, "MThd");
    appendBigEndian(file, 6, 4);
    appendBigEndian(file, 0, 2); // format 0: one track
    appendBigEndian(file, 1, 2);
    appendBigEndian(file, seq.header.resolution, 2);
    appendTag(file, "MTrk");
    appendBigEndian(file, static_cast<std::uint32_t>(trackBytes.size()), 4);
    file.insert(file.end(), trackBytes.begin(), trackBytes.end());
    return file;
}

} // namespace dwe::formats::audio
