#include <dwe/assets/sound_bank.hpp>
#include <dwe/assets/yaml_reader.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <utility>

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view Context = "the sound bank";
constexpr std::size_t AdsrText = 9; // "hhhh hhhh"

template <typename Item>
const Item* findNumbered(const std::vector<Item>& items, std::uint32_t Item::* number,
                         std::uint32_t wanted) {
  const auto found = std::ranges::find(items, wanted, number);
  return found == items.end() ? nullptr : &*found;
}

// Fails on an item whose number an earlier item of the list already has.
template <typename Item>
void checkUnique(YamlReader& reader, const YamlNode& node, const std::vector<Item>& items,
                 std::uint32_t Item::* number, const Item& item, std::string_view what) {
  if (reader.ok() && findNumbered(items, number, item.*number) != nullptr) {
    reader.fail(node, std::format("{} {} is there twice", what, item.*number));
  }
}

std::uint8_t midiValue(YamlReader& reader, const YamlNode& node) {
  const auto value = reader.integer<std::uint8_t>(node);
  if (value > 127) {
    reader.fail(node, "the value is past 127");
  }
  return value;
}

void readAdsr(YamlReader& reader, const YamlNode& node, SoundBankTone& tone) {
  const std::string text = reader.text(node);
  const auto parse = [&](std::size_t at, std::uint16_t& value) {
    const auto [end, error] = std::from_chars(text.data() + at, text.data() + at + 4, value, 16);
    return error == std::errc{} && end == text.data() + at + 4;
  };
  if (!reader.ok()) {
    return;
  }
  if (text.size() != AdsrText || text[4] != ' ' || !parse(0, tone.adsr1) || !parse(5, tone.adsr2)) {
    reader.fail(node, "adsr is two words of four hex digits");
  }
}

SoundBankTone readTone(YamlReader& reader, const YamlNode& node) {
  SoundBankTone tone{.tone = reader.integer<std::uint32_t>(reader.field(node, "tone")),
                     .root = midiValue(reader, reader.field(node, "center")),
                     .fineTune = midiValue(reader, reader.field(node, "shift")),
                     .volume = midiValue(reader, reader.field(node, "volume")),
                     .pan = midiValue(reader, reader.field(node, "pan")),
                     .sample = reader.integer<std::uint32_t>(reader.field(node, "sample"))};
  readAdsr(reader, reader.field(node, "adsr"), tone);
  return tone;
}

void checkSound(YamlReader& reader, const YamlNode& node, const SoundBank& bank,
                const SoundBankSound& sound) {
  const SoundBankProgram* program = bank.program(sound.program);
  if (program == nullptr) {
    reader.fail(node, std::format("program {} is not in the bank", sound.program));
    return;
  }
  for (const std::uint32_t number : sound.tones) {
    const SoundBankTone* tone = findNumbered(program->tones, &SoundBankTone::tone, number);
    if (tone == nullptr) {
      reader.fail(node, std::format("tone {} is not in program {}", number, sound.program));
    } else if (bank.sample(tone->sample) == nullptr) {
      reader.fail(node, std::format("sample {} is not in the bank", tone->sample));
    }
  }
}

} // namespace

const SoundBankSound* SoundBank::sound(std::uint32_t value) const {
  return findNumbered(sounds, &SoundBankSound::value, value);
}

const SoundBankProgram* SoundBank::program(std::uint32_t number) const {
  return findNumbered(programs, &SoundBankProgram::program, number);
}

const SoundBankSample* SoundBank::sample(std::uint32_t number) const {
  return findNumbered(samples, &SoundBankSample::sample, number);
}

core::Result<SoundBank> readSoundBank(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
  }
  YamlReader reader;
  const YamlNode& root = *tree;
  SoundBank bank{.volume =
                     midiValue(reader, reader.field(reader.field(root, "vab"), "master_volume")),
                 .programs = {},
                 .samples = {},
                 .sounds = {}};
  for (const YamlNode& node : reader.sequence(reader.field(root, "programs"), "programs")) {
    SoundBankProgram program{.program =
                                 reader.integer<std::uint32_t>(reader.field(node, "program")),
                             .volume = midiValue(reader, reader.field(node, "master_volume")),
                             .pan = midiValue(reader, reader.field(node, "pan")),
                             .tones = {}};
    for (const YamlNode& tone : reader.sequence(reader.field(node, "tones"), "tones")) {
      SoundBankTone read = readTone(reader, tone);
      checkUnique(reader, tone, program.tones, &SoundBankTone::tone, read, "tone");
      program.tones.push_back(read);
    }
    checkUnique(reader, node, bank.programs, &SoundBankProgram::program, program, "program");
    bank.programs.push_back(std::move(program));
  }
  for (const YamlNode& node : reader.sequence(reader.field(root, "samples"), "samples")) {
    SoundBankSample sample{.sample = reader.integer<std::uint32_t>(reader.field(node, "sample")),
                           .file = reader.text(reader.field(node, "file")),
                           .rate = reader.integer<std::uint32_t>(reader.field(node, "rate"))};
    if (reader.ok() && sample.rate == 0) {
      reader.fail(node, "a sample's rate is above 0");
    }
    checkUnique(reader, node, bank.samples, &SoundBankSample::sample, sample, "sample");
    bank.samples.push_back(std::move(sample));
  }
  const auto sounds = reader.sequence(reader.field(root, "sounds"), "sounds");
  for (const YamlNode& node : sounds) {
    SoundBankSound sound{.value = reader.integer<std::uint32_t>(reader.field(node, "value")),
                         .program = reader.integer<std::uint32_t>(reader.field(node, "program")),
                         .note = midiValue(reader, reader.field(node, "note")),
                         .tones = {}};
    for (const YamlNode& tone : reader.sequence(reader.field(node, "tones"), "tones")) {
      sound.tones.push_back(reader.integer<std::uint32_t>(tone));
    }
    checkUnique(reader, node, bank.sounds, &SoundBankSound::value, sound, "sound");
    bank.sounds.push_back(std::move(sound));
  }
  for (std::size_t i = 0; i < bank.sounds.size() && reader.ok(); ++i) {
    checkSound(reader, sounds[i], bank, bank.sounds[i]);
  }
  return reader.finish(std::move(bank), Context);
}

} // namespace dwe::assets
