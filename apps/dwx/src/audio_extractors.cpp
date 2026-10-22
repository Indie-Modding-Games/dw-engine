#include "audio_extractors.hpp"

#include <dwe/core/yaml.hpp>
#include <dwe/formats/audio/adpcm.hpp>
#include <dwe/formats/audio/midi.hpp>
#include <dwe/formats/audio/seq.hpp>
#include <dwe/formats/audio/sf2.hpp>
#include <dwe/formats/audio/vab.hpp>
#include <dwe/formats/audio/vab_sf2.hpp>
#include <dwe/formats/audio/vhb.hpp>
#include <dwe/formats/audio/wav.hpp>
#include <dwe/formats/exe/tables.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "asset_names.hpp"

namespace dwe::dwx {

namespace {

namespace audio = formats::audio;
namespace exe = formats::exe;
using core::YamlNode;

constexpr double ChipRate = 44100.0;
constexpr int SemitoneSteps = 128; // the fine tune unit of a tone
constexpr std::uint8_t FirstPerTypeVabSlot =
    3; // playSound: vabs 3-7 play program 0 at the note of the value
constexpr std::uint8_t LastPerTypeVabSlot = 7;
constexpr int SoundValueBaseNote =
    60; // playSound: other vabs play program v / 16 at note 60 + v % 16
constexpr int SoundValueNotes = 16;
constexpr std::size_t LastPartnerType =
    65; // loadPartnerSounds: other types take the fallback entry
constexpr std::size_t FallbackVlallEntry = 15;
constexpr std::uint8_t BattleMusicFont = 0x21;
constexpr std::size_t FirstEnemyMusicType = 0x43; // btl.c: loadBattleData
constexpr std::size_t EndEnemyMusicType = 0x70;
constexpr std::string_view MusicFile = "faall";
// The block the sound library's tools put after a one-shot sample's end: flags 7 (a loop on itself)
// and every nibble 7. The voice is released before it gets there.
constexpr std::uint8_t TerminatorFlags = 0x07;
constexpr std::byte TerminatorNibbles{0x77};
// The music fonts align each SEQ to 4 bytes with the fill a debug heap leaves in memory it has not
// initialised.
constexpr std::size_t SeqAlignment = 4;
constexpr std::byte SeqAlignmentFill{0xCD};

// The map sound slices that code loads by a fixed id, besides the slice of each map (MAP_ENTRIES
// flags).
struct FixedMapSound {
  std::size_t id = 0;
  std::string_view loader;
};

constexpr std::array FixedMapSounds = {
    FixedMapSound{.id = 0x10, .loader = "std/std_setup.c: STD_tournamentMain"},
    FixedMapSound{.id = 0x12, .loader = "main/evl.c: getEvoSequenceState"},
    FixedMapSound{.id = 0x13, .loader = "main/partner.c: partnerTickDying"},
    FixedMapSound{.id = 0x13, .loader = "dooa/dooa.c: DOOA_tickDissolve"},
    FixedMapSound{.id = 0x14, .loader = "main/script_interp.c: scriptInstruction64to7E, op 0x34"},
    FixedMapSound{.id = 0x15, .loader = "main/script_interp.c: scriptInstruction64to7E, op 0x33"},
};

// The battle music tracks btl.c: loadBattleData plays by number.
struct FixedTrack {
  std::uint8_t track = 0;
  std::string_view when;
};

constexpr std::array FixedBattleTracks = {
    FixedTrack{.track = 0, .when = "predefined battles"},
    FixedTrack{.track = 1, .when = "enemies outside ENEMY_MUSIC_MAPPING"},
    FixedTrack{.track = 3, .when = "when the second player entity is type 0x73"},
};

// sound.c: seqPlay plays these tracks once and every other one forever.
bool playsOnce(std::size_t font, std::size_t track) {
  return (font == 0x1D && track != 1) ||
         ((font == 0x1E || font == 0x1F || font == 0x20) && track != 2);
}

YamlNode number(std::integral auto value) {
  return YamlNode::scalar(std::to_string(value));
}

YamlNode quoted(std::string text) {
  return YamlNode::scalar(std::move(text), YamlNode::ScalarStyle::Quoted);
}

YamlNode numberPair(std::integral auto first, std::integral auto second) {
  YamlNode node = YamlNode::sequence();
  node.append(number(first));
  node.append(number(second));
  return node;
}

// Where a bank is in its file and the names of its outputs.
struct BankPlace {
  const audio::SoundFile* file = nullptr;
  std::optional<std::size_t> entry; // in a packed file; the map sound id for ESALL
  std::optional<audio::SectorRange> sectors;

  [[nodiscard]] bool music() const { return file->name == MusicFile; }
  // The game's music font number: FAALL entry + 1 (sound.c: loadMusicFont).
  [[nodiscard]] std::size_t font() const { return entry.value_or(0) + 1; }

  [[nodiscard]] core::Result<assets::AssetId> asset(std::string_view leaf) const {
    return music() ? names::musicAsset(font(), leaf) : names::soundAsset(file->name, entry, leaf);
  }

  [[nodiscard]] std::string label() const {
    return entry ? std::format("entry {}", *entry) : std::string{"bank"};
  }
};

core::Result<void> addOutput(Extraction& extraction, core::Result<assets::AssetId> assetId,
                             std::string_view extension, std::vector<std::byte> bytes) {
  if (!assetId) {
    return std::unexpected{assetId.error()};
  }
  extraction.outputs.push_back(OutputFile{
      .id = std::move(*assetId), .extension = std::string{extension}, .bytes = std::move(bytes)});
  return {};
}

core::Result<void> addYaml(Extraction& extraction, core::Result<assets::AssetId> id,
                           const YamlNode& node) {
  auto text = core::emitYaml(node);
  if (!text) {
    return std::unexpected{text.error()};
  }
  const auto bytes = std::as_bytes(std::span{*text});
  return addOutput(extraction, std::move(id), "yaml",
                   std::vector<std::byte>(bytes.begin(), bytes.end()));
}

// Reports the bytes from start to the end of `bytes` that nothing explained, unless they are all
// zero (padding).
void reportLeftover(Extraction& extraction, std::span<const std::byte> bytes, std::size_t start,
                    std::string_view what) {
  if (start >= bytes.size()) {
    return;
  }
  const auto rest = bytes.subspan(start);
  const auto nonZero =
      std::ranges::count_if(rest, [](std::byte value) { return value != std::byte{0}; });
  if (nonZero > 0) {
    extraction.unknowns.push_back(std::format("{}: {} bytes after 0x{:x}, {} of them not zero",
                                              what, rest.size(), start, nonZero));
  }
}

bool isTerminatorBlock(std::span<const std::byte> rest) {
  return rest.size() == audio::AdpcmBlockSize && rest[0] == std::byte{0} &&
         std::to_integer<std::uint8_t>(rest[1]) == TerminatorFlags &&
         std::ranges::all_of(rest.subspan(2),
                             [](std::byte value) { return value == TerminatorNibbles; });
}

bool isSeqAlignment(std::span<const std::byte> rest) {
  return rest.size() < SeqAlignment &&
         std::ranges::all_of(
             rest,
             [](std::byte value) { return value == SeqAlignmentFill || value == std::byte{0}; });
}

// The samples of a bank, decoded, indexed by sample number; anomalies go to the unknowns.
std::vector<audio::DecodedSample> decodeSamples(Extraction& extraction, const audio::Vhb& vhb,
                                                std::span<const audio::VagLocation> vags,
                                                const std::string& where) {
  std::vector<audio::DecodedSample> samples(audio::VabVagSlots);
  for (const auto& vag : vags) {
    const auto bytes = vhb.vb.subspan(vag.offset, vag.size);
    auto sample = audio::decodeAdpcm(bytes);
    const std::string name = std::format("{}, sample {}", where, vag.number);
    if (!sample.terminated) {
      extraction.unknowns.push_back(std::format("{}: no block ends it", name));
    }
    if (sample.reservedBlocks > 0) {
      extraction.unknowns.push_back(std::format(
          "{}: {} blocks use a reserved shift, filter or header bit", name, sample.reservedBlocks));
    }
    const auto rest = bytes.subspan(std::min(bytes.size(), sample.blocks * audio::AdpcmBlockSize));
    if (sample.terminated && !rest.empty() && !isTerminatorBlock(rest)) {
      extraction.unknowns.push_back(
          std::format("{}: {} bytes after its end block", name, rest.size()));
    }
    samples[vag.number] = std::move(sample);
  }
  return samples;
}

// The tone that plays each sample first, in program and tone order.
std::map<std::size_t, audio::ToneAttributes> firstToneOfEachSample(const audio::Vab& vab) {
  std::map<std::size_t, audio::ToneAttributes> tones;
  for (const auto& block : vab.toneBlocks) {
    for (const auto& tone : vab.tonesOf(block.program)) {
      tones.try_emplace(static_cast<std::size_t>(tone.vag), tone);
    }
  }
  return tones;
}

// The rate at which a tone plays its sample at its own top key: the chip's 44100 Hz moved by the
// key's distance to the centre, in 128ths of a semitone (formats/audio: toneFineTune).
std::uint32_t playbackRate(const audio::ToneAttributes& tone) {
  const int steps = (tone.max * SemitoneSteps) - ((tone.center * SemitoneSteps) + tone.shift);
  return static_cast<std::uint32_t>(
      std::lround(ChipRate * std::exp2(steps / (12.0 * SemitoneSteps))));
}

YamlNode vabNode(const audio::Vab& vab) {
  YamlNode node = YamlNode::mapping();
  node.set("version", number(vab.header.version));
  node.set("id", number(vab.header.id));
  node.set("programs", number(vab.header.ps));
  node.set("tones", number(vab.header.ts));
  node.set("samples", number(vab.header.vs));
  node.set("master_volume", number(vab.header.mvol));
  node.set("pan", number(vab.header.pan));
  node.set("attr1", number(vab.header.attr1));
  node.set("attr2", number(vab.header.attr2));
  return node;
}

YamlNode toneNode(std::size_t index, const audio::ToneAttributes& tone) {
  YamlNode node = YamlNode::mapping();
  node.set("tone", number(index));
  node.set("keys", numberPair(tone.min, tone.max));
  node.set("center", number(tone.center));
  node.set("shift", number(tone.shift));
  node.set("volume", number(tone.vol));
  node.set("pan", number(tone.pan));
  node.set("priority", number(tone.prior));
  node.set("mode", number(tone.mode));
  node.set("sample", number(tone.vag));
  node.set("pitch_bend", numberPair(tone.pbmin, tone.pbmax));
  node.set("vibrato", numberPair(tone.vibW, tone.vibT));
  node.set("portamento", numberPair(tone.porW, tone.porT));
  node.set("adsr", quoted(std::format("{:04x} {:04x}", tone.adsr1, tone.adsr2)));
  return node;
}

YamlNode programsNode(const audio::Vab& vab) {
  YamlNode programs = YamlNode::sequence();
  for (const auto& block : vab.toneBlocks) {
    const auto& attributes = vab.programs[block.program];
    YamlNode node = YamlNode::mapping();
    node.set("program", number(block.program));
    node.set("master_volume", number(attributes.mvol));
    node.set("pan", number(attributes.mpan));
    node.set("priority", number(attributes.prior));
    node.set("mode", number(attributes.mode));
    node.set("attr", number(attributes.attr));
    YamlNode tones = YamlNode::sequence();
    const auto programTones = vab.tonesOf(block.program);
    for (std::size_t i = 0; i < programTones.size(); ++i) {
      tones.append(toneNode(i, programTones[i]));
    }
    node.set("tones", std::move(tones));
    programs.append(std::move(node));
  }
  return programs;
}

// The VAB fields with values the SoundFont does not carry; the YAML keeps them all.
YamlNode notInSoundFontNode(const audio::Vab& vab) {
  std::vector<std::string_view> fields;
  const auto anyTone = [&vab](auto predicate) {
    return std::ranges::any_of(vab.toneBlocks, [&](const auto& block) {
      return std::ranges::any_of(vab.tonesOf(block.program), predicate);
    });
  };
  const auto anyProgram = [&vab](auto predicate) {
    return std::ranges::any_of(
        vab.toneBlocks, [&](const auto& block) { return predicate(vab.programs[block.program]); });
  };
  if (vab.header.pan != 64) {
    fields.emplace_back("bank pan");
  }
  if (vab.header.attr1 != 0 || vab.header.attr2 != 0) {
    fields.emplace_back("bank attr1/attr2");
  }
  if (anyProgram([](const auto& program) { return program.prior != 0; })) {
    fields.emplace_back("program priority");
  }
  if (anyProgram([](const auto& program) { return program.attr != 0; })) {
    fields.emplace_back("program attr");
  }
  if (anyTone([](const auto& tone) { return tone.prior != 0; })) {
    fields.emplace_back("tone priority");
  }
  if (anyTone(
          [](const auto& tone) { return (tone.mode & ~audio::ToneAttributes::ReverbMode) != 0; })) {
    fields.emplace_back("tone mode bits other than reverb");
  }
  if (anyTone([](const auto& tone) { return tone.vibW != 0 || tone.vibT != 0; })) {
    fields.emplace_back("vibrato");
  }
  if (anyTone([](const auto& tone) { return tone.porW != 0 || tone.porT != 0; })) {
    fields.emplace_back("portamento");
  }
  if (anyTone([](const auto& tone) { return tone.pbmin != tone.pbmax; })) {
    fields.emplace_back("pitch bend down range different from up");
  }
  YamlNode node = YamlNode::sequence();
  for (const auto field : fields) {
    node.append(quoted(std::string{field}));
  }
  return node;
}

// The game values of the sounds of a bank and the tones each keys on (sound.c: playSound,
// startSound). startSound takes block `program` of the VH as the program's tones, which holds
// because the sound banks number their programs without gaps; it keys on every tone of the block
// whose top key is the note.
YamlNode soundsNode(const audio::Vab& vab, const audio::SoundFile& file) {
  const bool perType =
      file.firstVabSlot >= FirstPerTypeVabSlot && file.lastVabSlot <= LastPerTypeVabSlot;
  YamlNode sounds = YamlNode::sequence();
  for (const auto& block : vab.toneBlocks) {
    if (perType && block.program != 0) {
      continue;
    }
    const auto tones = vab.tonesOf(block.program);
    std::map<int, std::vector<std::size_t>> byNote;
    for (std::size_t i = 0; i < tones.size(); ++i) {
      byNote[tones[i].max].push_back(i);
    }
    for (const auto& [note, indices] : byNote) {
      const int offset = note - SoundValueBaseNote;
      if (!perType && (offset < 0 || offset >= SoundValueNotes)) {
        continue;
      }
      const auto value =
          perType ? note : (static_cast<int>(block.program) * SoundValueNotes) + offset;
      YamlNode sound = YamlNode::mapping();
      sound.set("value", number(value));
      sound.set("program", number(block.program));
      sound.set("note", number(note));
      YamlNode toneList = YamlNode::sequence();
      for (const auto index : indices) {
        toneList.append(number(index));
      }
      sound.set("tones", std::move(toneList));
      sounds.append(std::move(sound));
    }
  }
  return sounds;
}

YamlNode namedTypes(const std::vector<std::size_t>& types, const GameTables& tables) {
  YamlNode list = YamlNode::sequence();
  for (const auto type : types) {
    YamlNode node = YamlNode::mapping();
    node.set("type", number(type));
    node.set("id", quoted(names::digimonSlug(&tables, type)));
    list.append(std::move(node));
  }
  return list;
}

// Who loads a sound bank, from the executable's tables.
YamlNode usedByNode(const BankPlace& place, const GameTables& users) {
  YamlNode node = YamlNode::mapping();
  const std::string_view name = place.file->name;
  const std::size_t entry = place.entry.value_or(0);
  if (name == "vball" || name == "vlall") {
    const auto& ids =
        name == "vball" ? users.tables.digimonVballSoundIds : users.tables.digimonVlallSoundIds;
    std::vector<std::size_t> types;
    for (std::size_t type = 0; type < ids.size(); ++type) {
      const bool partnerRange = name == "vball" || (type >= 1 && type <= LastPartnerType);
      if (partnerRange && ids[type] == entry) {
        types.push_back(type);
      }
    }
    node.set("types", namedTypes(types, users));
    if (name == "vlall" && entry == FallbackVlallEntry) {
      node.set("fallback", quoted("every type outside 1-65 (sound.c: loadPartnerSounds)"));
    }
  } else if (name == "esall") {
    YamlNode maps = YamlNode::sequence();
    for (std::size_t map = 0; map < users.tables.mapEntries.size(); ++map) {
      const auto& mapEntry = users.tables.mapEntries[map];
      if (std::cmp_equal(mapEntry.flags & exe::MapEntryFlags::SoundIdMask, entry) &&
          !mapEntry.filename.empty()) {
        YamlNode mapNode = YamlNode::mapping();
        mapNode.set("map", number(map));
        if (map < users.ids.maps.size()) {
          mapNode.set("id", quoted(users.ids.maps[map]));
        }
        maps.append(std::move(mapNode));
      }
    }
    node.set("maps", std::move(maps));
    YamlNode loaders = YamlNode::sequence();
    for (const auto& fixed : FixedMapSounds) {
      if (fixed.id == entry) {
        loaders.append(quoted(std::string{fixed.loader}));
      }
    }
    node.set("loaded_by", std::move(loaders));
  }
  return node;
}

// What the original does with a single bank, by its file.
std::string_view singleBankUse(std::string_view name) {
  if (name == "ss") {
    return "system and menu sounds: playSound(0, v) and playSound2(0, v)";
  }
  if (name == "sl") {
    return "no literal call; only playSound with a variable bank or the animation opcode 0x4000 reach it";
  }
  return "replaces the map sounds in vab 8 for the VS mode and the arena overlays (sound.c: VSLoadSounds)";
}

YamlNode trackUsersNode(std::size_t font, std::size_t track, const GameTables& users) {
  YamlNode node = YamlNode::mapping();
  YamlNode jukebox = YamlNode::sequence();
  for (std::size_t i = 0; i < users.tables.jukeboxTracks.size(); ++i) {
    const auto& entry = users.tables.jukeboxTracks[i];
    if (entry.font == font && entry.track == track && i < users.ids.bgmTracks.size()) {
      jukebox.append(quoted(users.ids.bgmTracks[i]));
    }
  }
  node.set("jukebox", std::move(jukebox));
  if (font == BattleMusicFont) {
    std::vector<std::size_t> enemies;
    for (std::size_t type = FirstEnemyMusicType; type < EndEnemyMusicType; ++type) {
      const std::size_t index = type - FirstEnemyMusicType;
      if (index < users.tables.enemyMusicMapping.size() &&
          users.tables.enemyMusicMapping[index] == track) {
        enemies.push_back(type);
      }
    }
    node.set("battle_enemies", namedTypes(enemies, users));
    YamlNode fixed = YamlNode::sequence();
    for (const auto& battle : FixedBattleTracks) {
      if (battle.track == track) {
        fixed.append(quoted(std::format("btl.c: loadBattleData, {}", battle.when)));
      }
    }
    node.set("battle", std::move(fixed));
  }
  return node;
}

YamlNode seqNode(const audio::Seq& seq) {
  YamlNode node = YamlNode::mapping();
  node.set("resolution", number(seq.header.resolution));
  node.set("tempo", number(seq.header.tempo));
  node.set("rhythm", numberPair(seq.header.rhythmNumerator, seq.header.rhythmDenominator));
  node.set("events", number(seq.events.size()));
  node.set("ticks", number(seq.events.back().tick));
  const auto start =
      std::ranges::find(seq.events, audio::SeqEventType::LoopStart, &audio::SeqEvent::type);
  const auto end =
      std::ranges::find(seq.events, audio::SeqEventType::LoopEnd, &audio::SeqEvent::type);
  if (start != seq.events.end() && end != seq.events.end()) {
    YamlNode loop = YamlNode::mapping();
    loop.set("start_tick", number(start->tick));
    loop.set("end_tick", number(end->tick));
    loop.set("count", number(start->data1));
    node.set("loop", std::move(loop));
  }
  return node;
}

// Converts the SEQs of a music font to MIDI files and describes them.
core::Result<YamlNode> addTracks(Extraction& extraction, const BankPlace& place,
                                 const audio::Vhb& vhb, const GameTables* users) {
  YamlNode tracks = YamlNode::sequence();
  const auto seqs = audio::splitSeqBank(vhb.seqBank);
  if (!seqs) {
    extraction.unknowns.push_back(std::format("font {}: {}", place.font(), seqs.error().message));
    return tracks;
  }
  const auto ranges = audio::pitchBendRanges(vhb.vab);
  for (std::size_t track = 0; track < seqs->size(); ++track) {
    const auto seq = audio::readSeq((*seqs)[track]);
    if (!seq) {
      extraction.unknowns.push_back(
          std::format("font {} track {}: {}", place.font(), track, seq.error().message));
      continue;
    }
    if (!isSeqAlignment((*seqs)[track].subspan(seq->size))) {
      reportLeftover(extraction, (*seqs)[track], seq->size,
                     std::format("font {} track {} after its end event", place.font(), track));
    }
    for (const auto& event : seq->events) {
      if (event.type == audio::SeqEventType::Program && vhb.vab.programs[event.data1].tones == 0) {
        extraction.unknowns.push_back(
            std::format("font {} track {}: program change to the empty program {}", place.font(),
                        track, event.data1));
      }
    }
    const std::string file = std::format("track_{:02}", track);
    if (auto added =
            addOutput(extraction, place.asset(file), "mid", audio::convertSeqToMidi(*seq, ranges));
        !added) {
      return std::unexpected{added.error()};
    }
    YamlNode node = YamlNode::mapping();
    node.set("track", number(track));
    node.set("file", quoted(file + ".mid"));
    node.set("plays", YamlNode::scalar(playsOnce(place.font(), track) ? "once" : "forever"));
    node.set("seq", seqNode(*seq));
    if (users != nullptr) {
      node.set("used_by", trackUsersNode(place.font(), track, *users));
    }
    tracks.append(std::move(node));
  }
  return tracks;
}

// Writes a WAV of every sample of a sound bank at the rate of the first tone that plays it.
core::Result<YamlNode> addSampleWavs(Extraction& extraction, const BankPlace& place,
                                     std::span<const audio::VagLocation> vags,
                                     const std::vector<audio::DecodedSample>& samples,
                                     const audio::Vab& vab) {
  const auto firstTones = firstToneOfEachSample(vab);
  YamlNode list = YamlNode::sequence();
  for (const auto& vag : vags) {
    const auto& sample = samples[vag.number];
    const auto tone = firstTones.find(vag.number);
    const bool played = tone != firstTones.end();
    const std::uint32_t rate =
        played ? playbackRate(tone->second) : static_cast<std::uint32_t>(ChipRate);
    const std::string file = std::format("sample_{:03}", vag.number);
    auto wav =
        audio::writeWav(audio::WavSound{.pcm = sample.pcm,
                                        .sampleRate = rate,
                                        .loop = sample.loop,
                                        .unityNote = played ? tone->second.max : std::uint8_t{60}});
    if (auto added = addOutput(extraction, place.asset(file), "wav", std::move(wav)); !added) {
      return std::unexpected{added.error()};
    }
    YamlNode node = YamlNode::mapping();
    node.set("sample", number(vag.number));
    node.set("file", quoted(file + ".wav"));
    node.set("rate", number(rate));
    node.set("frames", number(sample.pcm.size()));
    if (sample.loop) {
      node.set("loop", numberPair(sample.loop->start, sample.loop->end));
    }
    if (!played) {
      node.set("played", YamlNode::scalar("false"));
    }
    if (vag.beyondCount) {
      node.set("beyond_count", YamlNode::scalar("true"));
    }
    list.append(std::move(node));
  }
  return list;
}

// Extracts one bank: its SoundFont, its samples or SEQs, and its YAML.
core::Result<void> extractBank(Extraction& extraction, std::span<const std::byte> bytes,
                               const BankPlace& place, const GameTables* users) {
  const auto vhb = audio::readVhb(bytes);
  if (!vhb) {
    extraction.unknowns.push_back(std::format("{}: {}", place.label(), vhb.error().message));
    return {};
  }
  reportLeftover(extraction, bytes, vhb->end, std::format("{} after its data", place.label()));
  const auto vags = audio::locateVags(vhb->vab, vhb->vb.size());
  if (!vags) {
    extraction.unknowns.push_back(std::format("{}: {}", place.label(), vags.error().message));
    return {};
  }
  const std::size_t vagsEnd = vags->empty() ? 0 : vags->back().offset + vags->back().size;
  reportLeftover(extraction, vhb->vb, vagsEnd,
                 std::format("{} VB after its samples", place.label()));
  const auto samples = decodeSamples(extraction, *vhb, *vags, place.label());

  std::string fontName{place.file->name};
  if (place.music()) {
    fontName = std::format("music font {:03}", place.font());
  } else if (place.entry) {
    fontName = std::format("{} entry {:03}", place.file->name, *place.entry);
  }
  const auto converted = audio::convertVabToSoundFont(vhb->vab, samples, fontName);
  if (!converted) {
    return std::unexpected{converted.error()};
  }
  auto soundFont = audio::writeSf2(converted->font);
  if (!soundFont) {
    return std::unexpected{soundFont.error()};
  }
  const std::string fontFile = place.music() ? "font" : "bank";
  if (auto added = addOutput(extraction, place.asset(fontFile), "sf2", std::move(*soundFont));
      !added) {
    return added;
  }

  YamlNode meta = YamlNode::mapping();
  meta.set("source", quoted(std::string{place.file->path}));
  if (place.entry) {
    meta.set("entry", number(*place.entry));
  }
  if (place.sectors) {
    meta.set("sectors", numberPair(place.sectors->first, place.sectors->count));
  }
  meta.set("loader", quoted(std::string{place.file->loader}));
  meta.set("vab_slots", numberPair(place.file->firstVabSlot, place.file->lastVabSlot));
  meta.set("soundfont", quoted(fontFile + ".sf2"));
  if (place.music()) {
    meta.set("font", number(place.font()));
  } else if (place.file->layout == audio::SoundFileLayout::Single) {
    meta.set("used_by", quoted(std::string{singleBankUse(place.file->name)}));
  } else if (users != nullptr) {
    meta.set("used_by", usedByNode(place, *users));
  }
  meta.set("vab", vabNode(vhb->vab));
  meta.set("programs", programsNode(vhb->vab));
  if (place.music()) {
    auto tracks = addTracks(extraction, place, *vhb, users);
    if (!tracks) {
      return std::unexpected{tracks.error()};
    }
    meta.set("tracks", std::move(*tracks));
  } else {
    meta.set("sounds", soundsNode(vhb->vab, *place.file));
    auto list = addSampleWavs(extraction, place, *vags, samples, vhb->vab);
    if (!list) {
      return std::unexpected{list.error()};
    }
    meta.set("samples", std::move(*list));
  }
  meta.set("not_in_soundfont", notInSoundFontNode(vhb->vab));
  meta.set("sloped_sustain_tones", number(converted->slopedSustains));
  return addYaml(extraction, place.asset(std::format("{}_meta", fontFile)), meta);
}

// The entries of a packed file. ESALL's come from MAP_SOUND_PARA, in 1 KB units (sound.c:
// loadMapSounds).
core::Result<std::vector<audio::SectorRange>> entriesOf(const audio::SoundFile& file,
                                                        const GameTables* users) {
  if (file.layout == audio::SoundFileLayout::FixedStride) {
    return audio::fixedStrideEntries(file);
  }
  if (users == nullptr) {
    return core::fail(
        "the executable's tables are not known, and they hold the slices of this file");
  }
  std::vector<audio::SectorRange> entries;
  entries.reserve(users->tables.mapSoundPara.size());
  for (const auto& para : users->tables.mapSoundPara) {
    entries.push_back(
        audio::SectorRange{.first = para.sectorId / 2U, .count = para.sectorCount / 2U});
  }
  return entries;
}

} // namespace

bool handlesSoundFile(std::string_view path) {
  return audio::findSoundFile(path) != nullptr;
}

core::Result<Extraction> extractSoundFile(const Disc& disc, const formats::disc::IsoFile& file) {
  const audio::SoundFile* soundFile = audio::findSoundFile(file.path);
  if (soundFile == nullptr) {
    return core::fail("not a sound file");
  }
  const auto bytes = formats::disc::readIsoFile(disc.source(), file);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  const GameTables* users = disc.tables();
  Extraction extraction;
  if (users == nullptr) {
    extraction.notes.emplace_back(
        "the executable's tables are not known: who uses each bank is not written");
  }
  if (soundFile->layout == audio::SoundFileLayout::Single) {
    if (auto done = extractBank(extraction, *bytes,
                                BankPlace{.file = soundFile, .entry = {}, .sectors = {}}, users);
        !done) {
      return std::unexpected{done.error()};
    }
    return extraction;
  }

  const auto entries = entriesOf(*soundFile, users);
  if (!entries) {
    return std::unexpected{entries.error()};
  }
  const auto parts = audio::splitSoundFile(*bytes, *entries);
  if (!parts) {
    return std::unexpected{parts.error()};
  }
  std::size_t covered = 0;
  for (std::size_t i = 0; i < parts->size(); ++i) {
    const auto& range = (*entries)[i];
    if (range.first * audio::SoundSectorSize != covered) {
      extraction.unknowns.push_back(std::format(
          "entry {} starts at sector {}, not where the one before ends", i, range.first));
    }
    covered = std::max(covered, std::size_t{range.first + range.count} * audio::SoundSectorSize);
    const BankPlace place{.file = soundFile, .entry = i, .sectors = range};
    if (auto done = extractBank(extraction, (*parts)[i], place, users); !done) {
      return std::unexpected{done.error()};
    }
  }
  reportLeftover(extraction, *bytes, covered, "after the last entry");
  return extraction;
}

} // namespace dwe::dwx
