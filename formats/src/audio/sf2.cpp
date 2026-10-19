#include <dwe/formats/audio/sf2.hpp>

#include <algorithm>
#include <format>
#include <limits>
#include <string_view>

#include "riff_writer.hpp"

namespace dwe::formats::audio {

namespace {

constexpr std::size_t NameSize = 20;
constexpr std::size_t LoopGuardPoints = 8;
constexpr std::size_t SamplePadding = 46;
constexpr std::uint32_t MonoSample = 1;
constexpr std::size_t MaxIndex = std::numeric_limits<std::uint16_t>::max();

// Where a sample landed in the smpl chunk, in points.
struct SamplePlace {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t loopStart = 0;
    std::uint32_t loopEnd = 0;
};

bool isPlacedByWriter(Sf2Generator generator) {
    return generator == Sf2Generator::KeyRange || generator == Sf2Generator::SampleId ||
           generator == Sf2Generator::Instrument;
}

core::Result<void> checkFont(const SoundFont& font) {
    for (const auto& sample : font.samples) {
        if (sample.loop && (sample.loop->start >= sample.loop->end || sample.loop->end > sample.pcm.size())) {
            return core::fail(std::format("SF2 sample {}: loop {}-{} is outside its {} points", sample.name,
                                          sample.loop->start, sample.loop->end, sample.pcm.size()));
        }
    }
    std::size_t zones = 0;
    std::size_t generators = 0;
    for (const auto& instrument : font.instruments) {
        for (const auto& zone : instrument.zones) {
            if (zone.sample >= font.samples.size()) {
                return core::fail(std::format("SF2 instrument {} plays sample {}; there are {}", instrument.name,
                                              zone.sample, font.samples.size()));
            }
            if (zone.keyLow > zone.keyHigh || zone.keyHigh > 127) {
                return core::fail(std::format("SF2 instrument {} has the key range {}-{}", instrument.name, zone.keyLow,
                                              zone.keyHigh));
            }
            if (std::ranges::any_of(zone.generators,
                                    [](const auto& value) { return isPlacedByWriter(value.generator); })) {
                return core::fail(
                    std::format("SF2 instrument {} gives a generator the writer places itself", instrument.name));
            }
            ++zones;
            generators += zone.generators.size() + 2;
        }
    }
    for (const auto& preset : font.presets) {
        if (preset.instrument >= font.instruments.size()) {
            return core::fail(std::format("SF2 preset {} plays instrument {}; there are {}", preset.name,
                                          preset.instrument, font.instruments.size()));
        }
    }
    if (zones >= MaxIndex || generators >= MaxIndex || font.presets.size() >= MaxIndex ||
        font.instruments.size() >= MaxIndex) {
        return core::fail("SF2 tables outgrow their 16-bit indices");
    }
    return {};
}

void writeInfoText(RiffWriter& riff, std::string_view id, std::string_view text) {
    const std::size_t at = riff.beginChunk(id);
    // A zero-terminated string of even length.
    riff.name(text, text.size() % 2 == 0 ? text.size() + 2 : text.size() + 1);
    riff.endChunk(at);
}

// Writes the samples with their guard points and padding, and returns where each one landed.
core::Result<std::vector<SamplePlace>> writeSampleData(RiffWriter& riff, const SoundFont& font) {
    std::vector<SamplePlace> places;
    std::vector<std::int16_t> points;
    for (const auto& sample : font.samples) {
        SamplePlace place;
        place.start = static_cast<std::uint32_t>(points.size());
        points.insert(points.end(), sample.pcm.begin(), sample.pcm.end());
        if (sample.loop) {
            const SampleLoop loop = *sample.loop;
            const std::size_t length = loop.end - loop.start;
            for (std::size_t i = 0; i < LoopGuardPoints; ++i) {
                points.push_back(sample.pcm[loop.start + (i % length)]);
            }
            place.loopStart = place.start + static_cast<std::uint32_t>(loop.start);
            place.loopEnd = place.start + static_cast<std::uint32_t>(loop.end);
        }
        place.end = static_cast<std::uint32_t>(points.size());
        if (!sample.loop) {
            place.loopStart = place.start;
            place.loopEnd = place.end;
        }
        points.insert(points.end(), SamplePadding, 0);
        if (points.size() > std::numeric_limits<std::uint32_t>::max() / 2) {
            return core::fail("SF2 sample data outgrows a RIFF chunk");
        }
        places.push_back(place);
    }
    const std::size_t listAt = riff.beginList("LIST", "sdta");
    riff.pcmChunk("smpl", points);
    riff.endChunk(listAt);
    return places;
}

void writeBag(RiffWriter& riff, std::size_t generator) {
    riff.u16(static_cast<std::uint32_t>(generator));
    riff.u16(0); // no modulators
}

void writeGenerator(RiffWriter& riff, Sf2Generator generator, std::uint32_t amount) {
    riff.u16(static_cast<std::uint32_t>(generator));
    riff.u16(amount);
}

void writeEmptyModulators(RiffWriter& riff, std::string_view id) {
    const std::size_t at = riff.beginChunk(id);
    for (std::size_t i = 0; i < 10; ++i) {
        riff.u8(0);
    }
    riff.endChunk(at);
}

void writePresets(RiffWriter& riff, const SoundFont& font) {
    std::size_t at = riff.beginChunk("phdr");
    for (std::size_t i = 0; i <= font.presets.size(); ++i) {
        const bool terminal = i == font.presets.size();
        riff.name(terminal ? "EOP" : font.presets[i].name, NameSize);
        riff.u16(terminal ? 0 : font.presets[i].preset);
        riff.u16(terminal ? 0 : font.presets[i].bank);
        riff.u16(static_cast<std::uint32_t>(i));
        riff.u32(0); // library
        riff.u32(0); // genre
        riff.u32(0); // morphology
    }
    riff.endChunk(at);

    at = riff.beginChunk("pbag");
    for (std::size_t i = 0; i <= font.presets.size(); ++i) {
        writeBag(riff, i);
    }
    riff.endChunk(at);
    writeEmptyModulators(riff, "pmod");

    at = riff.beginChunk("pgen");
    for (const auto& preset : font.presets) {
        writeGenerator(riff, Sf2Generator::Instrument, static_cast<std::uint32_t>(preset.instrument));
    }
    writeGenerator(riff, Sf2Generator{0}, 0);
    riff.endChunk(at);
}

void writeInstruments(RiffWriter& riff, const SoundFont& font) {
    std::size_t at = riff.beginChunk("inst");
    std::size_t bag = 0;
    for (const auto& instrument : font.instruments) {
        riff.name(instrument.name, NameSize);
        riff.u16(static_cast<std::uint32_t>(bag));
        bag += instrument.zones.size();
    }
    riff.name("EOI", NameSize);
    riff.u16(static_cast<std::uint32_t>(bag));
    riff.endChunk(at);

    at = riff.beginChunk("ibag");
    std::size_t generator = 0;
    for (const auto& instrument : font.instruments) {
        for (const auto& zone : instrument.zones) {
            writeBag(riff, generator);
            generator += zone.generators.size() + 2;
        }
    }
    writeBag(riff, generator);
    riff.endChunk(at);
    writeEmptyModulators(riff, "imod");

    at = riff.beginChunk("igen");
    for (const auto& instrument : font.instruments) {
        for (const auto& zone : instrument.zones) {
            writeGenerator(riff, Sf2Generator::KeyRange, zone.keyLow | (std::uint32_t{zone.keyHigh} << 8U));
            for (const auto& value : zone.generators) {
                writeGenerator(riff, value.generator, static_cast<std::uint16_t>(value.amount));
            }
            writeGenerator(riff, Sf2Generator::SampleId, static_cast<std::uint32_t>(zone.sample));
        }
    }
    writeGenerator(riff, Sf2Generator{0}, 0);
    riff.endChunk(at);
}

void writeSampleHeaders(RiffWriter& riff, const SoundFont& font, const std::vector<SamplePlace>& places) {
    const std::size_t at = riff.beginChunk("shdr");
    for (std::size_t i = 0; i < font.samples.size(); ++i) {
        const auto& sample = font.samples[i];
        const auto& place = places[i];
        riff.name(sample.name, NameSize);
        riff.u32(place.start);
        riff.u32(place.end);
        riff.u32(place.loopStart);
        riff.u32(place.loopEnd);
        riff.u32(sample.sampleRate);
        riff.u8(sample.originalPitch);
        riff.u8(static_cast<std::uint8_t>(sample.pitchCorrection));
        riff.u16(0); // no linked sample
        riff.u16(MonoSample);
    }
    riff.name("EOS", NameSize);
    for (std::size_t i = 0; i < 26; ++i) {
        riff.u8(0);
    }
    riff.endChunk(at);
}

} // namespace

core::Result<std::vector<std::byte>> writeSf2(const SoundFont& font) {
    if (auto checked = checkFont(font); !checked) {
        return std::unexpected{checked.error()};
    }
    RiffWriter riff;
    const std::size_t riffAt = riff.beginList("RIFF", "sfbk");

    const std::size_t infoAt = riff.beginList("LIST", "INFO");
    const std::size_t versionAt = riff.beginChunk("ifil");
    riff.u16(2);
    riff.u16(1);
    riff.endChunk(versionAt);
    writeInfoText(riff, "isng", "EMU8000");
    writeInfoText(riff, "INAM", font.name);
    riff.endChunk(infoAt);

    auto places = writeSampleData(riff, font);
    if (!places) {
        return std::unexpected{places.error()};
    }

    const std::size_t presetDataAt = riff.beginList("LIST", "pdta");
    writePresets(riff, font);
    writeInstruments(riff, font);
    writeSampleHeaders(riff, font, *places);
    riff.endChunk(presetDataAt);

    riff.endChunk(riffAt);
    return riff.take();
}

} // namespace dwe::formats::audio
