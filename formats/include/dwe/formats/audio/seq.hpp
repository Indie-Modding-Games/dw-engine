#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The sound library's SEQ: one stream of MIDI-like events for every channel, played by SsSeqOpen and SsSeqPlay
// (sound.c: seqOpen, seqPlay). The game keeps its SEQs in the music fonts of FAALL, after the VB.
namespace dwe::formats::audio {

// The 15-byte header, big-endian after the "pQES" magic.
struct SeqHeader {
    std::uint32_t version = 0;
    std::uint16_t resolution = 0; // ticks per quarter note
    std::uint32_t tempo = 0;      // microseconds per quarter note
    std::uint8_t rhythmNumerator = 0;
    std::uint8_t rhythmDenominator = 0;

    bool operator==(const SeqHeader&) const = default;
};

enum class SeqEventType : std::uint8_t {
    NoteOff,
    NoteOn, // velocity 0 releases the note, as the game's SEQs do
    KeyPressure,
    Control,
    Program,
    ChannelPressure,
    PitchBend, // data1 = low 7 bits, data2 = high 7 bits
    Tempo,
    LoopStart, // the NRPN pair CC99 = 20, CC6 = count; data1 = count (0 and 127 repeat forever)
    LoopEnd,   // CC99 = 30
    End,
};

struct SeqEvent {
    std::uint32_t tick = 0;
    SeqEventType type = SeqEventType::End;
    std::uint8_t channel = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    std::uint32_t tempo = 0; // microseconds per quarter note, for Tempo

    bool operator==(const SeqEvent&) const = default;
};

struct Seq {
    SeqHeader header;
    std::vector<SeqEvent> events; // the last one is End
    std::size_t size = 0;         // bytes from the magic to the end of the End event
};

// The controllers of the sound library's loop markers.
struct SeqLoopControl {
    static constexpr std::uint8_t NrpnMsb = 99;
    static constexpr std::uint8_t DataEntry = 6;
    static constexpr std::uint8_t LoopStart = 20;
    static constexpr std::uint8_t LoopEnd = 30;
};

// Reads a SEQ up to its end event (FF 2F 00). Events are a variable-length delta in ticks and a status with running
// status. The tempo meta event is FF 51 and three bytes, with no length byte; it and the end are the only meta events
// the sound library knows, and a meta event ends the running status. The loop markers become LoopStart and LoopEnd.
core::Result<Seq> readSeq(std::span<const std::byte> bytes);

// Splits the SEQ bank of a music font: a table of u32 offsets from the bank's start, as many as the first offset / 4
// (sound.c: seqOpen), each SEQ ending where the next starts and the last at the end of the bank.
core::Result<std::vector<std::span<const std::byte>>> splitSeqBank(std::span<const std::byte> bank);

} // namespace dwe::formats::audio
