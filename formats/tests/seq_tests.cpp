#include <dwe/formats/audio/seq.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "audio_fixtures.hpp"

using namespace dwe::formats::audio;
using dwe::formats::audio::test::Bytes;
using dwe::formats::audio::test::seqHeader;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

SeqEvent event(std::uint32_t tick, SeqEventType type, std::uint8_t channel, std::uint8_t data1, std::uint8_t data2) {
    return SeqEvent{.tick = tick, .type = type, .channel = channel, .data1 = data1, .data2 = data2, .tempo = 0};
}

} // namespace

TEST_CASE("SEQ: header, running status, tempo without a length byte and the end") {
    Bytes b = seqHeader(480, 500000);
    b.u8(0).u8(0xC1).u8(5);                 // program 5 on channel 1
    b.u8(0).u8(0x91).u8(60).u8(100);        // note on
    b.u8(0x83).u8(0x60).u8(60).u8(0);       // delta 480, running status: note on with velocity 0
    b.u8(0).u8(0xFF).u8(0x51).be24(400000); // tempo, no length byte
    b.u8(0x10).u8(0xE1).u8(0).u8(0x40);     // pitch bend centre
    b.u8(0).u8(0xFF).u8(0x2F).u8(0);
    b.u8(0xAA); // after the end: not read
    const auto seq = readSeq(b.bytes());
    REQUIRE_MESSAGE(seq.has_value(), errorOf(seq));
    CHECK(seq->header ==
          SeqHeader{.version = 1, .resolution = 480, .tempo = 500000, .rhythmNumerator = 1, .rhythmDenominator = 4});
    REQUIRE(seq->events.size() == 6);
    CHECK(seq->events[0] == event(0, SeqEventType::Program, 1, 5, 0));
    CHECK(seq->events[1] == event(0, SeqEventType::NoteOn, 1, 60, 100));
    CHECK(seq->events[2] == event(480, SeqEventType::NoteOn, 1, 60, 0));
    CHECK(seq->events[3].type == SeqEventType::Tempo);
    CHECK(seq->events[3].tempo == 400000);
    CHECK(seq->events[4] == event(496, SeqEventType::PitchBend, 1, 0, 0x40));
    CHECK(seq->events[5].type == SeqEventType::End);
    CHECK(seq->size == b.size() - 1);
}

TEST_CASE("SEQ: the NRPN loop markers become loop events") {
    Bytes b = seqHeader(480, 500000);
    b.u8(0).u8(0xB0).u8(99).u8(20); // loop start
    b.u8(0).u8(6).u8(127);          // running status: CC6 = count
    b.u8(0).u8(7).u8(100);          // CC7 stays a control
    b.u8(0x60).u8(99).u8(30);       // loop end
    b.u8(0).u8(0xB2).u8(99).u8(20); // CC99 = 20 without CC6 after it stays a control
    b.u8(0).u8(0xB2).u8(10).u8(64);
    b.u8(0).u8(0xFF).u8(0x2F).u8(0);
    const auto seq = readSeq(b.bytes());
    REQUIRE_MESSAGE(seq.has_value(), errorOf(seq));
    REQUIRE(seq->events.size() == 6);
    CHECK(seq->events[0] == event(0, SeqEventType::LoopStart, 0, 127, 0));
    CHECK(seq->events[1] == event(0, SeqEventType::Control, 0, 7, 100));
    CHECK(seq->events[2] == event(0x60, SeqEventType::LoopEnd, 0, 0, 0));
    CHECK(seq->events[3] == event(0x60, SeqEventType::Control, 2, 99, 20));
}

TEST_CASE("SEQ: malformed streams are errors") {
    const auto withEvents = [](std::initializer_list<std::uint8_t> events) {
        Bytes b = seqHeader(480, 500000);
        for (const auto value : events) {
            b.u8(value);
        }
        return readSeq(b.bytes());
    };
    CHECK(withEvents({0, 0x90, 60, 100, 0, 0xFF, 0x2F, 0}).has_value());
    CHECK_FALSE(withEvents({0, 0x90, 60, 100}).has_value());                         // no end
    CHECK_FALSE(withEvents({0, 60, 100}).has_value());                               // running status with none
    CHECK_FALSE(withEvents({0, 0xFF, 0x51, 1, 2, 3, 0, 60, 100}).has_value());       // a meta ends running status
    CHECK_FALSE(withEvents({0, 0xFF, 0x01, 2, 'h', 'i'}).has_value());               // other meta events
    CHECK_FALSE(withEvents({0, 0xF0, 1, 0xF7}).has_value());                         // system exclusive
    CHECK_FALSE(withEvents({0, 0x90, 0x80, 100}).has_value());                       // data byte above 0x7F
    CHECK_FALSE(withEvents({0x80, 0x80, 0x80, 0x80, 0, 0xFF, 0x2F, 0}).has_value()); // 5-byte delta
    CHECK_FALSE(withEvents({0, 0xFF, 0x51, 1}).has_value());                         // cut tempo
    CHECK_FALSE(readSeq(Bytes{}.text("pQES").u8(0).bytes()).has_value());
    CHECK_FALSE(readSeq(Bytes{}.text("MThd").fill(20).bytes()).has_value());
    CHECK_FALSE(readSeq(seqHeader(0, 500000).u8(0).u8(0xFF).u8(0x2F).u8(0).bytes()).has_value());
}

TEST_CASE("SEQ banks: offsets from the bank's start, as many as the first offset / 4") {
    Bytes b;
    b.u32(12).u32(16).u32(20).fill(4, 1).fill(4, 2).fill(6, 3);
    const auto seqs = splitSeqBank(b.bytes());
    REQUIRE_MESSAGE(seqs.has_value(), errorOf(seqs));
    REQUIRE(seqs->size() == 3);
    CHECK((*seqs)[0].size() == 4);
    CHECK((*seqs)[2].size() == 6);
    CHECK((*seqs)[1][0] == std::byte{2});

    CHECK_FALSE(splitSeqBank(Bytes{}.u32(0).bytes()).has_value());
    CHECK_FALSE(splitSeqBank(Bytes{}.u32(6).fill(4).bytes()).has_value());
    CHECK_FALSE(splitSeqBank(Bytes{}.u32(8).u32(4).bytes()).has_value());
    CHECK_FALSE(splitSeqBank(Bytes{}.u32(8).u32(20).bytes()).has_value());
    CHECK_FALSE(splitSeqBank(Bytes{}.u32(16).bytes()).has_value());
}
