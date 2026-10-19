#include <dwe/formats/audio/vab.hpp>
#include <dwe/formats/byte_reader.hpp>

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

#include "../bytes/byte_cursor.hpp"

namespace dwe::formats::audio {

namespace {

using bytes::ByteCursor;

constexpr std::string_view VabMagic = "pBAV"; // "VABp" stored as a little-endian u32
constexpr std::size_t HeaderSize = 0x20;
constexpr std::size_t ProgramSize = 16;
constexpr std::size_t ToneSize = 32;
constexpr std::size_t VagSizeEntry = 2;
constexpr std::size_t VagSizeUnit = 8;

VabHeader readHeader(ByteCursor& c) {
    c.skip(VabMagic.size());
    VabHeader header;
    header.version = c.u32();
    header.id = c.u32();
    header.fsize = c.u32();
    header.reserved0 = c.u16();
    header.ps = c.u16();
    header.ts = c.u16();
    header.vs = c.u16();
    header.mvol = c.u8();
    header.pan = c.u8();
    header.attr1 = c.u8();
    header.attr2 = c.u8();
    header.reserved1 = c.u32();
    return header;
}

ProgramAttributes readProgram(ByteCursor& c) {
    ProgramAttributes program;
    program.tones = c.u8();
    program.mvol = c.u8();
    program.prior = c.u8();
    program.mode = c.u8();
    program.mpan = c.u8();
    program.reserved0 = c.u8();
    program.attr = c.i16();
    program.reserved1 = c.u32();
    program.reserved2 = c.u32();
    return program;
}

ToneAttributes readTone(ByteCursor& c) {
    ToneAttributes tone;
    tone.prior = c.u8();
    tone.mode = c.u8();
    tone.vol = c.u8();
    tone.pan = c.u8();
    tone.center = c.u8();
    tone.shift = c.u8();
    tone.min = c.u8();
    tone.max = c.u8();
    tone.vibW = c.u8();
    tone.vibT = c.u8();
    tone.porW = c.u8();
    tone.porT = c.u8();
    tone.pbmin = c.u8();
    tone.pbmax = c.u8();
    tone.reserved1 = c.u8();
    tone.reserved2 = c.u8();
    tone.adsr1 = c.u16();
    tone.adsr2 = c.u16();
    tone.prog = c.i16();
    tone.vag = c.i16();
    std::ranges::generate(tone.reserved, [&c] { return c.i16(); });
    return tone;
}

core::Result<void> checkTones(const Vab& vab) {
    for (const auto& block : vab.toneBlocks) {
        const auto& program = vab.programs[block.program];
        if (program.tones > VabTonesPerProgram) {
            return core::fail(std::format("VAB program {} has {} tones; a program holds {}", block.program,
                                          program.tones, VabTonesPerProgram));
        }
        for (std::size_t i = 0; i < program.tones; ++i) {
            const auto& tone = block.tones[i];
            if (std::cmp_not_equal(tone.prog, block.program)) {
                return core::fail(
                    std::format("VAB program {} tone {} says it belongs to program {}", block.program, i, tone.prog));
            }
            if (tone.vag < 1 || std::cmp_greater(tone.vag, vab.header.vs) ||
                vab.vagSizes[static_cast<std::size_t>(tone.vag)] == 0) {
                return core::fail(std::format("VAB program {} tone {} plays sample {}, which is not one of the {}",
                                              block.program, i, tone.vag, vab.header.vs));
            }
        }
    }
    return {};
}

} // namespace

std::size_t Vab::size() const {
    return vabHeaderSize(toneBlocks.size());
}

std::span<const ToneAttributes> Vab::tonesOf(std::size_t program) const {
    const auto block = std::ranges::find(toneBlocks, program, &ToneBlock::program);
    if (block == toneBlocks.end()) {
        return {};
    }
    return std::span{block->tones}.first(std::min<std::size_t>(programs[program].tones, VabTonesPerProgram));
}

std::size_t vabHeaderSize(std::size_t programs) {
    return HeaderSize + (VabProgramSlots * ProgramSize) + (programs * VabTonesPerProgram * ToneSize) +
           (VabVagSlots * VagSizeEntry);
}

core::Result<Vab> readVab(std::span<const std::byte> bytes) {
    const ByteReader reader{bytes};
    if (reader.chars(0, VabMagic.size()) != VabMagic) {
        return core::fail("not a VAB header: it does not start with pBAV");
    }
    ByteCursor cursor{bytes};
    Vab vab;
    vab.header = readHeader(cursor);
    if (cursor.overrun()) {
        return core::fail(std::format("VAB header of {} bytes is shorter than 0x20", bytes.size()));
    }
    if (vab.header.vs >= VabVagSlots) {
        return core::fail(std::format("VAB has {} samples; the size table holds {}", vab.header.vs, VabVagSlots - 1));
    }
    const std::size_t size = vabHeaderSize(vab.header.ps);
    if (bytes.size() < size) {
        return core::fail(
            std::format("VAB header with {} programs needs {} bytes; there are {}", vab.header.ps, size, bytes.size()));
    }
    if (vab.header.fsize < size) {
        return core::fail(
            std::format("VAB fsize {} is smaller than its own header ({} bytes)", vab.header.fsize, size));
    }

    std::ranges::generate(vab.programs, [&cursor] { return readProgram(cursor); });
    std::vector<std::size_t> used;
    for (std::size_t i = 0; i < VabProgramSlots; ++i) {
        if (vab.programs[i].tones > 0) {
            used.push_back(i);
        }
    }
    if (used.size() != vab.header.ps) {
        return core::fail(
            std::format("VAB header says {} programs, but {} program slots have tones", vab.header.ps, used.size()));
    }
    for (const std::size_t program : used) {
        ToneBlock block{.program = program, .tones = {}};
        std::ranges::generate(block.tones, [&cursor] { return readTone(cursor); });
        vab.toneBlocks.push_back(block);
    }
    std::ranges::generate(vab.vagSizes, [&cursor] { return cursor.u16(); });
    if (cursor.overrun()) {
        return core::fail("VAB header ends inside its tables");
    }
    if (auto tones = checkTones(vab); !tones) {
        return std::unexpected{tones.error()};
    }
    return vab;
}

core::Result<std::vector<VagLocation>> locateVags(const Vab& vab, std::size_t vbSize) {
    std::vector<VagLocation> vags;
    std::size_t offset = 0;
    for (std::size_t number = 1; number < VabVagSlots; ++number) {
        const std::size_t size = std::size_t{vab.vagSizes[number]} * VagSizeUnit;
        const bool beyondCount = number > vab.header.vs;
        if (size == 0) {
            continue;
        }
        if (size > vbSize - std::min(offset, vbSize)) {
            if (beyondCount) {
                break;
            }
            return core::fail(std::format("VAB sample {} ({} bytes at {}) does not fit in a VB of {} bytes", number,
                                          size, offset, vbSize));
        }
        vags.push_back(VagLocation{.number = number, .offset = offset, .size = size, .beyondCount = beyondCount});
        offset += size;
    }
    return vags;
}

} // namespace dwe::formats::audio
