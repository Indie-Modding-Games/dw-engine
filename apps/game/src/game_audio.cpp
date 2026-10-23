#include "game_audio.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::app {

namespace {

// In the order of ui::UiCue.
constexpr std::array<std::string_view, MenuCueCount> CueKeys = {"cursor", "confirm", "cancel"};

std::optional<MenuSound> readCue(const core::YamlNode& node, std::string_view key,
                                 std::vector<std::string>& problems) {
  assets::YamlReader reader;
  MenuSound sound;
  if (reader.expectKeys(node, key, {"bank", "sound"})) {
    sound = {.bank = reader.text(reader.field(node, "bank")),
             .sound = reader.integer<std::uint32_t>(reader.field(node, "sound"))};
  }
  if (reader.ok() && !assets::AssetId::parse(std::format("sound/{}", sound.bank))) {
    reader.fail(node, "the bank is no folder name");
  }
  if (!reader.ok()) {
    problems.push_back(reader.error(key).message);
    return std::nullopt;
  }
  return sound;
}

} // namespace

audio::Audio openGameAudio() {
  auto device = audio::openDeviceOutput();
  if (!device) {
    core::logWarning("{}", core::formatMessage(messages::AudioSilent, device.error().message));
    return audio::Audio{std::make_unique<audio::NullOutput>()};
  }
  return audio::Audio{std::move(*device)};
}

MenuSoundTable parseMenuSounds(std::string_view yaml) {
  MenuSoundTable table;
  auto tree = core::parseYaml(yaml);
  if (!tree || !tree->isMapping()) {
    table.problems.push_back(tree ? std::string{messages::MenuSoundsNotMapping}
                                  : tree.error().message);
    return table;
  }
  for (const auto& [key, value] : tree->entries()) {
    if (std::ranges::find(CueKeys, key) == CueKeys.end()) {
      table.problems.push_back(core::formatMessage(messages::MenuSoundUnknownCue, key));
    }
  }
  for (std::size_t cue = 0; cue < MenuCueCount; ++cue) {
    if (const core::YamlNode* node = tree->find(CueKeys[cue])) {
      table.cues[cue] = readCue(*node, CueKeys[cue], table.problems);
    } else {
      table.problems.push_back(core::formatMessage(messages::MenuSoundMissing, CueKeys[cue]));
    }
  }
  return table;
}

MenuSounds::MenuSounds(audio::Audio& audio, const assets::VirtualFileSystem& files)
    : audio_(&audio) {
  const auto id = assets::AssetId::parse(MenuSoundsId);
  auto bytes = files.read(*id);
  if (!bytes) {
    core::logWarning("{}", core::formatMessage(messages::MenuSoundsSilent, bytes.error().message));
    return;
  }
  const MenuSoundTable table = parseMenuSounds(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  for (const std::string& problem : table.problems) {
    core::logWarning("{}", core::formatMessage(messages::MenuSoundProblem, problem));
  }
  std::map<std::string, bool, std::less<>> failed;
  for (std::size_t cue = 0; cue < MenuCueCount; ++cue) {
    const std::optional<MenuSound>& sound = table.cues[cue];
    if (!sound || failed.contains(sound->bank)) {
      continue;
    }
    if (!banks_.contains(sound->bank)) {
      auto bank = audio::EffectBank::load(files, std::format("sound/{}", sound->bank));
      if (!bank) {
        core::logWarning("{}",
                         core::formatMessage(messages::MenuSoundProblem, bank.error().message));
        failed.emplace(sound->bank, true);
        continue;
      }
      banks_.emplace(sound->bank, std::move(*bank));
    }
    if (auto voices = banks_.at(sound->bank).requests(sound->sound); !voices) {
      core::logWarning("{}",
                       core::formatMessage(messages::MenuSoundProblem, voices.error().message));
      continue;
    }
    cues_[cue] = sound;
  }
}

void MenuSounds::play(ui::UiCue cue) {
  const std::optional<MenuSound>& sound = cues_[static_cast<std::size_t>(cue)];
  core::logDebug("[port:audio] tickMenuInput (main_menu.c:4664): cue={} bank={} sound={}",
                 CueKeys[static_cast<std::size_t>(cue)], sound ? sound->bank : "silent",
                 sound ? sound->sound : 0U);
  if (!sound) {
    return;
  }
  // The menu goes on either way.
  if (auto played = banks_.at(sound->bank).play(*audio_, sound->sound); !played) {
    core::logDebug("[port:audio] tickMenuInput (main_menu.c:4664): cue={} not played: {}",
                   CueKeys[static_cast<std::size_t>(cue)], played.error().message);
  }
}

void MenuSounds::play(std::span<const ui::UiCue> cues) {
  for (const ui::UiCue cue : cues) {
    play(cue);
  }
}

} // namespace dwe::app
