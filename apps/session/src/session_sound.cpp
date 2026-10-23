#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/session_sound.hpp>

#include <format>
#include <utility>

namespace dwe::session {

namespace {

constexpr std::int64_t SystemSlot = 0;
constexpr std::int64_t SecondSystemSlot = 1;
constexpr std::int64_t MapSlot = 8;
constexpr std::string_view SystemBank = "sound/ss";
constexpr std::string_view SecondSystemBank = "sound/sl";

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
    mapBank_ = std::format("sound/esall/{:03}", map->soundId);
  }
  std::erase_if(banks_, [&](const auto& entry) {
    return entry.first != SystemBank && entry.first != SecondSystemBank && entry.first != mapBank_;
  });
}

void SessionSound::playSound(std::int64_t slot, std::int64_t sound) {
  const std::optional<std::string> folder = folderOf(slot);
  core::logDebug("[port:audio] playSound (sound.c:641): slot={} sound={} bank={}", slot, sound,
                 folder.value_or("none"));
  if (!folder) {
    if (!firstWarning(slot, sound)) {
      return;
    }
    core::logWarning("{}", core::formatMessage(messages::SoundNoSlot, slot, sound));
    return;
  }
  const audio::EffectBank* effects = bank(*folder);
  if (effects == nullptr) {
    return;
  }
  if (!std::in_range<std::uint32_t>(sound)) {
    if (!firstWarning(slot, sound)) {
      return;
    }
    core::logWarning("{}", core::formatMessage(messages::SoundNotPlayed, *folder, sound,
                                               messages::SoundOutOfRange));
    return;
  }
  if (auto played = effects->play(*audio_, static_cast<std::uint32_t>(sound));
      !played && firstWarning(slot, sound)) {
    core::logWarning("{}", core::formatMessage(messages::SoundNotPlayed, *folder, sound,
                                               played.error().message));
  }
}

std::optional<std::string> SessionSound::folderOf(std::int64_t slot) const {
  switch (slot) {
  case SystemSlot:
    return std::string{SystemBank};
  case SecondSystemSlot:
    return std::string{SecondSystemBank};
  case MapSlot:
    return mapBank_;
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
