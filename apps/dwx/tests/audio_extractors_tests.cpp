#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>
#include <dwe/formats/exe/tables_yaml.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "audio_fixtures.hpp"
#include "base_pack.hpp"
#include "disc_fixtures.hpp"
#include "game_tables.hpp"
#include "riff_reader.hpp"
#include "temp_folder.hpp"

using namespace dwe::dwx;
using dwe::formats::audio::test::adpcmBlock;
using dwe::formats::audio::test::adpcmSample;
using dwe::formats::audio::test::buildBankVhb;
using dwe::formats::audio::test::buildFontVhb;
using dwe::formats::audio::test::buildVab;
using dwe::formats::audio::test::Bytes;
using dwe::formats::audio::test::nibblesOf;
using dwe::formats::audio::test::ProgramSpec;
using dwe::formats::audio::test::seqHeader;
using dwe::formats::audio::test::ToneSpec;

namespace {

constexpr std::size_t SectorSize = 2048;
constexpr std::size_t FontEntrySectors = 0x27;
constexpr std::size_t FontEntries = 33;
constexpr std::size_t MapBankSectors = 2;

// Two samples: a one-shot one followed by the terminator block the original puts after its end, and a looped one.
std::vector<std::byte> twoSamples() {
    const std::array<std::uint8_t, 2> oneShot{0, 1};
    const std::array<std::uint8_t, 3> looped{4, 2, 3};
    auto vb = adpcmSample(oneShot);
    const auto terminator = adpcmBlock(0, 0, 7, nibblesOf(7));
    vb.insert(vb.end(), terminator.begin(), terminator.end());
    const auto loop = adpcmSample(looped);
    vb.insert(vb.end(), loop.begin(), loop.end());
    return vb;
}

// A sound bank whose program 0 plays sample 1 at note 60 (sound 0) and sample 2 at note 61 (sound 1).
std::vector<std::byte> soundBank() {
    const auto vh = buildVab({ProgramSpec{.slot = 0,
                                          .tones = {ToneSpec{.vag = 1}, ToneSpec{.min = 61, .max = 61, .vag = 2}},
                                          .mvol = 127,
                                          .mpan = 64}},
                             {48, 48}, 2);
    return buildBankVhb(vh, twoSamples());
}

std::vector<std::byte> musicFont() {
    const auto vh = buildVab({ProgramSpec{.slot = 0, .tones = {ToneSpec{}}, .mvol = 127, .mpan = 64}}, {48, 48}, 2);
    Bytes seq = seqHeader(480, 500000);
    seq.u8(0).u8(0xC0).u8(0).u8(0).u8(0x90).u8(60).u8(100).u8(0x83).u8(0x60).u8(60).u8(0);
    seq.u8(0).u8(0xFF).u8(0x2F).u8(0).u8(0xCD).u8(0xCD); // the fill that aligns the next SEQ
    Bytes bank;
    bank.u32(4).append(seq.bytes());
    return buildFontVhb(vh, twoSamples(), bank.bytes());
}

std::vector<std::byte> padded(std::vector<std::byte> bytes, std::size_t sectors) {
    bytes.resize(sectors * SectorSize, std::byte{0});
    return bytes;
}

// A disc with a sound bank, every music font and a map sound file of two slices; map 0 uses slice 1.
struct SoundDisc {
    explicit SoundDisc(std::vector<std::byte> bank = soundBank()) {
        tables.tables.bgmTrackNames = {"Town Theme"};
        tables.tables.jukeboxTracks = {{.font = 1, .track = 0}};
        tables.tables.mapSoundPara = {{.sectorId = 0, .sectorCount = 4}, {.sectorId = 4, .sectorCount = 4}};
        dwe::formats::exe::MapEntry map;
        map.filename = "FOREST";
        map.flags = 1;
        tables.tables.mapEntries = {map};
        tables.ids = dwe::formats::exe::makeTableIds(tables.tables);

        fixtures::IsoBuilder builder;
        builder.addFile("SOUND/VHB/SS.VHB", std::move(bank));
        std::vector<std::byte> fonts;
        for (std::size_t i = 0; i < FontEntries; ++i) {
            const auto font = padded(musicFont(), FontEntrySectors);
            fonts.insert(fonts.end(), font.begin(), font.end());
        }
        builder.addFile("SOUND/VHB/FAALL.VHB", std::move(fonts));
        auto slices = padded(soundBank(), MapBankSectors);
        const auto second = padded(soundBank(), MapBankSectors);
        slices.insert(slices.end(), second.begin(), second.end());
        builder.addFile("SOUND/VHB/ESALL.VHB", std::move(slices));
        image = builder.build();
        source =
            std::make_unique<dwe::formats::disc::MemorySectorSource>(image, dwe::formats::disc::SectorLayout::Cooked);
        volume = *dwe::formats::disc::readIsoVolume(*source);
    }

    std::vector<std::byte> image;
    std::unique_ptr<dwe::formats::disc::MemorySectorSource> source;
    dwe::formats::disc::IsoVolume volume{.volumeId = {}, .volumeSectors = 0, .files = {}};
    DiscInfo info{.volumeId = "FIXTURE", .executablePath = "", .executableSha1 = "", .version = ""};
    GameTables tables;
};

const FileReport& reportOf(const ExtractionRun& run, std::string_view path) {
    const auto found = std::ranges::find(run.files, path, [](const FileReport& report) { return report.file.path; });
    REQUIRE(found != run.files.end());
    return *found;
}

const OutputFile& outputOf(const ExtractionRun& run, std::string_view path) {
    const auto found = std::ranges::find(run.outputs, path, outputPath);
    REQUIRE(found != run.outputs.end());
    return *found;
}

dwe::core::YamlNode yamlOf(const OutputFile& output) {
    auto yaml =
        dwe::core::parseYaml(std::string(reinterpret_cast<const char*>(output.bytes.data()), output.bytes.size()));
    REQUIRE(yaml.has_value());
    return std::move(*yaml);
}

bool mentions(const std::vector<std::string>& texts, std::string_view text) {
    return std::ranges::any_of(texts, [text](const std::string& each) { return each.contains(text); });
}

} // namespace

TEST_CASE("a sound bank gives a SoundFont, a WAV per sample and what the game plays from it") {
    const SoundDisc fixture;
    const Disc disc{*fixture.source, fixture.volume, fixture.info, &fixture.tables};
    const auto run = runExtractors(disc, extractors());
    REQUIRE(run.has_value());

    const auto& bank = reportOf(*run, "SOUND/VHB/SS.VHB");
    CHECK(bank.extractor == "sound_bank");
    CHECK(bank.status == FileStatus::Extracted);
    CHECK(bank.outputs == std::vector<std::string>{"sound/ss/bank.sf2", "sound/ss/sample_001.wav",
                                                   "sound/ss/sample_002.wav", "sound/ss/bank_meta.yaml"});

    const auto font = dwe::formats::audio::test::parseRiff(outputOf(*run, "sound/ss/bank.sf2").bytes);
    CHECK(font.type == "sfbk");
    // The second sample plays a semitone above its centre at its top key.
    const auto& wav = outputOf(*run, "sound/ss/sample_002.wav").bytes;
    CHECK(dwe::formats::audio::test::readU32(wav, 24) == 46722);
    CHECK(dwe::formats::audio::test::readU32(outputOf(*run, "sound/ss/sample_001.wav").bytes, 24) == 44100);

    const auto meta = yamlOf(outputOf(*run, "sound/ss/bank_meta.yaml"));
    const auto& sounds = meta.find("sounds")->items();
    REQUIRE(sounds.size() == 2);
    CHECK(sounds[1].find("value")->text() == "1");
    CHECK(sounds[1].find("note")->text() == "61");
    const auto& samples = meta.find("samples")->items();
    REQUIRE(samples.size() == 2);
    CHECK(samples[1].find("loop") != nullptr);
    CHECK(samples[0].find("loop") == nullptr);
}

TEST_CASE("a music font gives its SoundFont and a MIDI file per SEQ") {
    const SoundDisc fixture;
    const Disc disc{*fixture.source, fixture.volume, fixture.info, &fixture.tables};
    const auto run = runExtractors(disc, extractors());
    REQUIRE(run.has_value());

    const auto& fonts = reportOf(*run, "SOUND/VHB/FAALL.VHB");
    CHECK(fonts.status == FileStatus::Extracted);
    CHECK(fonts.outputs.size() == FontEntries * 3);
    const auto& midi = outputOf(*run, "music/001/track_00.mid").bytes;
    REQUIRE(midi.size() > 4);
    CHECK(dwe::formats::audio::test::readTag(midi, 0) == "MThd");
    CHECK(std::ranges::find(run->outputs, "music/033/font.sf2", outputPath) != run->outputs.end());

    const auto meta = yamlOf(outputOf(*run, "music/001/font_meta.yaml"));
    const auto& tracks = meta.find("tracks")->items();
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].find("plays")->text() == "forever");
    CHECK(tracks[0].find("used_by")->find("jukebox")->items().size() == 1);
}

TEST_CASE("map sound slices come from the executable's table") {
    const SoundDisc fixture;
    const Disc disc{*fixture.source, fixture.volume, fixture.info, &fixture.tables};
    const auto run = runExtractors(disc, extractors());
    REQUIRE(run.has_value());

    const auto& slices = reportOf(*run, "SOUND/VHB/ESALL.VHB");
    CHECK(slices.status == FileStatus::Extracted);
    CHECK(std::ranges::find(slices.outputs, "sound/esall/001/bank.sf2") != slices.outputs.end());
    const auto meta = yamlOf(outputOf(*run, "sound/esall/001/bank_meta.yaml"));
    const auto& maps = meta.find("used_by")->find("maps")->items();
    REQUIRE(maps.size() == 1);
    CHECK(maps[0].find("map")->text() == "0");

    const dwe::assets::testing::TempFolder temp;
    REQUIRE(prepareOutputFolder(temp.path() / "base", false).has_value());
    REQUIRE(writeBasePack(temp.path() / "base", *run, fixture.info).has_value());
    CHECK(checkBasePack(temp.path() / "base", *run).has_value());
}

TEST_CASE("without the executable's tables, banks lose their users and map slices are not found") {
    const SoundDisc fixture;
    const Disc disc{*fixture.source, fixture.volume, fixture.info};
    const auto run = runExtractors(disc, extractors());
    REQUIRE(run.has_value());

    const auto& bank = reportOf(*run, "SOUND/VHB/SS.VHB");
    CHECK(bank.status == FileStatus::Extracted);
    CHECK(mentions(bank.notes, "tables are not known"));
    const auto& slices = reportOf(*run, "SOUND/VHB/ESALL.VHB");
    CHECK(slices.status == FileStatus::Partial);
    CHECK(slices.outputs.empty());
    CHECK(mentions(slices.unknowns, "tables are not known"));
}

TEST_CASE("bytes the readers do not explain make a bank partial") {
    SUBCASE("data after the bank") {
        auto bank = soundBank();
        bank.push_back(std::byte{1});
        const SoundDisc fixture{bank};
        const Disc disc{*fixture.source, fixture.volume, fixture.info, &fixture.tables};
        const auto run = runExtractors(disc, extractors());
        REQUIRE(run.has_value());
        const auto& report = reportOf(*run, "SOUND/VHB/SS.VHB");
        CHECK(report.status == FileStatus::Partial);
        CHECK(mentions(report.unknowns, "after its data"));
    }
    SUBCASE("a sample with no end block") {
        auto bank = soundBank();
        bank[bank.size() - 16 + 1] = std::byte{2}; // the looped sample's last block repeats without ending
        const SoundDisc fixture{bank};
        const Disc disc{*fixture.source, fixture.volume, fixture.info, &fixture.tables};
        const auto run = runExtractors(disc, extractors());
        REQUIRE(run.has_value());
        const auto& report = reportOf(*run, "SOUND/VHB/SS.VHB");
        CHECK(report.status == FileStatus::Partial);
        CHECK(mentions(report.unknowns, "no block ends it"));
    }
}
