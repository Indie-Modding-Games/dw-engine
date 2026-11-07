#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/session_sound.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace dwe::session {

namespace {

constexpr std::int64_t SystemSlot = 0;
constexpr std::int64_t SecondSystemSlot = 1;
constexpr std::int64_t PartnerVoiceSlot = 3;
constexpr std::int64_t PartnerSlot = 4;
constexpr std::int64_t FirstEnemySlot = 5;
constexpr std::int64_t LastEnemySlot = 7;
constexpr std::int64_t MapSlot = 8;
constexpr std::string_view SystemBank = "sound/ss";
constexpr std::string_view SecondSystemBank = "sound/sl";
// The battle bank (sound_async.c: loadSB), which a battle loads into slot 8.
constexpr std::string_view BattleBank = "sound/sb";

// The species banks: vlall holds the partners' voices (slot 3), vball every species' own sounds.
constexpr std::string_view VlallFolder = "sound/vlall/";
constexpr std::string_view VballFolder = "sound/vball/";
constexpr std::string_view BankMetaName = "/bank_meta";

// The folder of a bank of slot 8: sound/esall/<sound id>.
std::string bankFolderOf(std::int32_t soundId) {
  return std::format("sound/esall/{:03}", soundId);
}

} // namespace

SessionSound::SessionSound(audio::Audio& audio, const assets::VirtualFileSystem& files)
    : audio_(&audio), files_(&files) {}

void SessionSound::readMaps() {
  mapsRead_ = true;
  if (auto maps = assets::loadMapTable(*files_); maps) {
    maps_ = std::move(*maps);
  } else {
    core::logWarning("{}", core::formatMessage(messages::SoundNoMaps, maps.error().message));
  }
}

void SessionSound::enterMap(std::string_view mapId) {
  if (!mapsRead_) {
    readMaps();
  }
  const assets::MapEntry* map = maps_ ? maps_->find(mapId) : nullptr;
  if (map == nullptr) {
    // A map a pack added since the table was read (a map editor's new map, after reload_map).
    readMaps();
    map = maps_ ? maps_->find(mapId) : nullptr;
  }
  mapBank_.reset();
  // map.c: loadMap loads the new map's bank over whatever slot 8 held.
  battleBankKept_ = false;
  if (map != nullptr && map->soundId >= 0) {
    mapBank_ = bankFolderOf(map->soundId);
  }
  dropUnusedBanks();
}

void SessionSound::useSceneBank(std::optional<std::int32_t> soundId) {
  sceneBank_.reset();
  battleBankKept_ = false;
  if (soundId) {
    sceneBank_ = bankFolderOf(*soundId);
  }
  core::logDebug("[port:sound] loadMapSounds2 (sound_async.c:233): slot 8 bank={}",
                 sceneBank_.value_or(mapBank_.value_or(std::string{})));
  dropUnusedBanks();
}

void SessionSound::dropUnusedBanks() {
  std::set<std::string, std::less<>> used;
  for (std::int64_t slot = PartnerVoiceSlot; slot <= LastEnemySlot; ++slot) {
    if (std::optional<std::string> folder = folderOf(slot)) {
      used.insert(std::move(*folder));
    }
  }
  std::erase_if(banks_, [&](const auto& entry) {
    return entry.first != SystemBank && entry.first != SecondSystemBank &&
           entry.first != mapBank_ && entry.first != sceneBank_ &&
           !((inBattle_ || battleBankKept_) && entry.first == BattleBank) &&
           !used.contains(entry.first);
  });
}

void SessionSound::attachPartner(std::function<std::string()> species) {
  partnerSpecies_ = std::move(species);
}

void SessionSound::useEnemyBanks(std::span<const std::string> species) {
  for (std::size_t index = 0; index < enemyBanks_.size(); ++index) {
    // btl.c: loadBattleData loads only the slots of the enemies there are; the others keep theirs.
    if (index < species.size()) {
      enemyBanks_.at(index) = speciesFolder(species[index], false);
      core::logDebug("[port:sound] readVBALLSection (sound_async.c:217): slot {} species {} "
                     "bank={}",
                     FirstEnemySlot + static_cast<std::int64_t>(index), species[index],
                     enemyBanks_.at(index).value_or("none"));
    }
  }
  dropUnusedBanks();
}

void SessionSound::useSpeciesBank(std::int64_t slot, std::int32_t type) {
  if (slot < FirstEnemySlot || slot > LastEnemySlot) {
    return;
  }
  if (!speciesBanksRead_) {
    readSpeciesBanks();
  }
  const auto found = vballOfType_.find(type);
  std::optional<std::string>& bank =
      enemyBanks_.at(static_cast<std::size_t>(slot - FirstEnemySlot));
  bank.reset();
  if (found != vballOfType_.end()) {
    bank = found->second;
  }
  core::logDebug("[port:sound] readVBALLSection (sound_async.c:217): slot {} type {} bank={}", slot,
                 type, bank.value_or("none"));
  dropUnusedBanks();
}

void SessionSound::readSpeciesBanks() {
  speciesBanksRead_ = true;
  for (const assets::AssetId& id : files_->ids()) {
    const std::string_view text = id.str();
    const bool voices = text.starts_with(VlallFolder);
    if ((!voices && !text.starts_with(VballFolder)) || !text.ends_with(BankMetaName)) {
      continue;
    }
    const std::string folder{text.substr(0, text.size() - BankMetaName.size())};
    auto tree = files_->read(id).and_then([](const std::vector<std::byte>& bytes) {
      return core::parseYaml(
          std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    });
    if (!tree) {
      core::logWarning(
          "{}", core::formatMessage(messages::SoundBankFailed, folder, tree.error().message));
      continue;
    }
    const core::YamlNode* users = tree->find("used_by");
    if (users == nullptr || !users->isMapping()) {
      continue;
    }
    auto& folders = voices ? vlallOf_ : vballOf_;
    if (const core::YamlNode* types = users->find("types");
        types != nullptr && types->isSequence()) {
      for (const core::YamlNode& type : types->items()) {
        if (const core::YamlNode* species = type.isMapping() ? type.find("id") : nullptr;
            species != nullptr && species->isScalar()) {
          folders.emplace(species->text(), folder);
        }
        if (const core::YamlNode* number = type.isMapping() ? type.find("type") : nullptr;
            !voices && number != nullptr && number->isScalar()) {
          const std::string& digits = number->text();
          std::int32_t value = 0;
          if (const auto [end, error] =
                  std::from_chars(digits.data(), digits.data() + digits.size(), value);
              error == std::errc{} && end == digits.data() + digits.size()) {
            vballOfType_.emplace(value, folder);
          }
        }
      }
    }
    if (voices && users->find("fallback") != nullptr) {
      vlallFallback_ = folder;
    }
  }
}

std::optional<std::string> SessionSound::speciesFolder(std::string_view species, bool partnerBank) {
  if (species.empty()) {
    return std::nullopt;
  }
  if (!speciesBanksRead_) {
    readSpeciesBanks();
  }
  const auto& folders = partnerBank ? vlallOf_ : vballOf_;
  if (const auto found = folders.find(species); found != folders.end()) {
    return found->second;
  }
  // sound.c: loadPartnerSounds: a type outside 1 to 65 takes vlall entry 15.
  return partnerBank ? vlallFallback_ : std::nullopt;
}

void SessionSound::startBattleBank() {
  inBattle_ = true;
  battleBankKept_ = false;
  core::logDebug("[port:sound] loadSB (sound_async.c:208): slot 8 bank={}",
                 folderOf(MapSlot).value_or("none"));
  dropUnusedBanks();
}

void SessionSound::endBattleBank(bool won) {
  inBattle_ = false;
  battleBankKept_ = !won;
  if (won) {
    sceneBank_.reset();
  }
  core::logDebug("[port:sound] {}: slot 8 bank={}",
                 won ? "BTL_handleVictorySequence (battle_main.c:893): loadMapSounds2"
                     : "BTL_deinitializeCombat (battle_main.c:766): no victory, no reload",
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

std::optional<std::string> SessionSound::folderOf(std::int64_t slot) {
  switch (slot) {
  case SystemSlot:
    return std::string{SystemBank};
  case SecondSystemSlot:
    return std::string{SecondSystemBank};
  case PartnerVoiceSlot:
  case PartnerSlot:
    if (!partnerSpecies_) {
      return std::nullopt;
    }
    return speciesFolder(partnerSpecies_(), slot == PartnerVoiceSlot);
  case FirstEnemySlot:
  case FirstEnemySlot + 1:
  case LastEnemySlot:
    return enemyBanks_.at(static_cast<std::size_t>(slot - FirstEnemySlot));
  case MapSlot:
    if (inBattle_ || battleBankKept_) {
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
