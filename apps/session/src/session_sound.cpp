#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/session_sound.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace dwe::session {

namespace {

constexpr std::int64_t SystemSlot = 0;
constexpr std::int64_t SecondSystemSlot = 1;
constexpr std::int64_t MapSlot = 8;
constexpr std::string_view SystemBank = "sound/ss";
constexpr std::string_view SecondSystemBank = "sound/sl";
// The battle bank (sound_async.c: loadSB), which a battle loads into slot 8.
constexpr std::string_view BattleBank = "sound/sb";

// The folder of a bank of slot 8: sound/esall/<sound id>.
std::string bankFolderOf(std::int32_t soundId) {
  return std::format("sound/esall/{:03}", soundId);
}

} // namespace

SessionSound::SessionSound(audio::Audio& audio, const assets::VirtualFileSystem& files)
    : audio_(&audio), files_(&files) {}

void SessionSound::enterMap(std::string_view mapId) {
  if (!mapsRead_) {
    mapsRead_ = true;
    if (auto maps = assets::loadMapTable(*files_); maps) {
      maps_ = std::move(*maps);
    } else {
      core::logWarning("{}", core::formatMessage(messages::SoundNoMaps, maps.error().message));
    }
  }
  const assets::MapEntry* map = maps_ ? maps_->find(mapId) : nullptr;
  mapBank_.reset();
  if (map != nullptr && map->soundId >= 0) {
    mapBank_ = bankFolderOf(map->soundId);
  }
  dropUnusedBanks();
}

void SessionSound::useSceneBank(std::optional<std::int32_t> soundId) {
  sceneBank_.reset();
  if (soundId) {
    sceneBank_ = bankFolderOf(*soundId);
  }
  core::logDebug("[port:sound] loadMapSounds2 (sound_async.c:233): slot 8 bank={}",
                 sceneBank_.value_or(mapBank_.value_or(std::string{})));
  dropUnusedBanks();
}

void SessionSound::dropUnusedBanks() {
  std::erase_if(banks_, [&](const auto& entry) {
    return entry.first != SystemBank && entry.first != SecondSystemBank &&
           entry.first != mapBank_ && entry.first != sceneBank_ &&
           !(inBattle_ && entry.first == BattleBank);
  });
}

void SessionSound::useBattleBank(bool inBattle) {
  inBattle_ = inBattle;
  core::logDebug("[port:sound] {}: slot 8 bank={}",
                 inBattle ? "loadSB (sound_async.c:208)" : "loadMapSounds (sound.c:721)",
                 folderOf(MapSlot).value_or("none"));
  dropUnusedBanks();
}

std::optional<std::vector<audio::VoiceId>> SessionSound::playVoices(std::int64_t slot,
                                                                    std::int64_t sound) {
  const std::optional<std::string> folder = folderOf(slot);
  core::logDebug("[port:audio] playSound (sound.c:641): slot={} sound={} bank={}", slot, sound,
                 folder.value_or("none"));
  if (!folder) {
    if (!firstWarning(slot, sound)) {
      return std::nullopt;
    }
    core::logWarning("{}", core::formatMessage(messages::SoundNoSlot, slot, sound));
    return std::nullopt;
  }
  const audio::EffectBank* effects = bank(*folder);
  if (effects == nullptr) {
    return std::nullopt;
  }
  if (!std::in_range<std::uint32_t>(sound)) {
    if (!firstWarning(slot, sound)) {
      return std::nullopt;
    }
    core::logWarning("{}", core::formatMessage(messages::SoundNotPlayed, *folder, sound,
                                               messages::SoundOutOfRange));
    return std::nullopt;
  }
  auto played = effects->play(*audio_, static_cast<std::uint32_t>(sound));
  if (!played) {
    if (firstWarning(slot, sound)) {
      core::logWarning("{}", core::formatMessage(messages::SoundNotPlayed, *folder, sound,
                                                 played.error().message));
    }
    return std::nullopt;
  }
  return std::move(*played);
}

void SessionSound::playSound(std::int64_t slot, std::int64_t sound) {
  playVoices(slot, sound);
}

std::optional<std::int64_t> SessionSound::playHeldSound(std::int64_t slot, std::int64_t sound) {
  // Held sounds that ended on their own are let go first, so the map does not grow.
  std::erase_if(held_, [this](const auto& entry) {
    return std::ranges::none_of(entry.second,
                                [this](audio::VoiceId voice) { return audio_->isActive(voice); });
  });
  std::optional<std::vector<audio::VoiceId>> voices = playVoices(slot, sound);
  if (!voices) {
    return std::nullopt;
  }
  const std::int64_t handle = nextHeld_++;
  core::logDebug("[port:audio] playSound2 (sound.c): slot={} sound={} held={} voices={}", slot,
                 sound, handle, voices->size());
  held_.emplace(handle, std::move(*voices));
  return handle;
}

void SessionSound::stopHeldSound(std::int64_t held) {
  const auto found = held_.find(held);
  if (found == held_.end()) {
    return;
  }
  core::logDebug("[port:audio] thunkStopSoundMask (sound.c): held={}", held);
  for (const audio::VoiceId voice : found->second) {
    audio_->stop(voice);
  }
  held_.erase(found);
}

std::optional<std::string> SessionSound::folderOf(std::int64_t slot) const {
  switch (slot) {
  case SystemSlot:
    return std::string{SystemBank};
  case SecondSystemSlot:
    return std::string{SecondSystemBank};
  case MapSlot:
    if (inBattle_) {
      return std::string{BattleBank};
    }
    return sceneBank_ ? sceneBank_ : mapBank_;
  default:
    return std::nullopt;
  }
}

const audio::EffectBank* SessionSound::bank(const std::string& folder) {
  if (const auto found = banks_.find(folder); found != banks_.end()) {
    return &found->second;
  }
  if (failedBanks_.contains(folder)) {
    return nullptr;
  }
  auto loaded = audio::EffectBank::load(*files_, folder);
  if (!loaded) {
    core::logWarning(
        "{}", core::formatMessage(messages::SoundBankFailed, folder, loaded.error().message));
    failedBanks_.insert(folder);
    return nullptr;
  }
  return &banks_.emplace(folder, std::move(*loaded)).first->second;
}

bool SessionSound::firstWarning(std::int64_t slot, std::int64_t sound) {
  return warnedSounds_.emplace(slot, sound).second;
}

} // namespace dwe::session
