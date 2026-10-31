#pragma once

#include <dwe/assets/map_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/audio/effect_bank.hpp>
#include <dwe/game/story/story_ports.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::session {

// The original's sound slots on the game's audio
// (docs/architecture/audio.md#64-el-tramo-2-en-detalle): slot 0 plays sound/ss, slot 1 sound/sl,
// slots 3 and 4 the partner's banks (sound/vlall and sound/vball of its species), slots 5 to 7 the
// banks of the last battle's enemies (sound/vball of their species) and slot 8 the bank of the map,
// sound/esall/<sound_id of data/maps>. Which vlall or vball entry a species uses comes from the
// used_by of each bank_meta. Banks load the first time a slot needs them. A bank that does not load
// leaves its slot silent, with one warning, and a sound that does not play warns once.
class SessionSound final : public game::story::SoundPort {
public:
  // Both must outlive the session.
  SessionSound(audio::Audio& audio, const assets::VirtualFileSystem& files);

  // Slot 8 follows the map; the bank of the map before is let go.
  void enterMap(std::string_view mapId);
  // A scene's bank in slot 8 instead of the map's (sound_async.c: loadMapSounds2, as the evolution
  // and death scenes load theirs); a new map or a won battle gives slot 8 back to the map's bank.
  void useSceneBank(std::optional<std::int32_t> soundId);
  // A battle's bank in slot 8 (btl.c: loadBattleData, sound_async.c: loadSB).
  void startBattleBank();
  // The battle ended. A won battle loads the map's bank in slot 8 again, also over a scene's
  // (battle_main.c: BTL_handleVictorySequence, loadMapSounds2(getMapSoundId)); a fled or lost one
  // loads nothing, so the battle bank stays until a map or a scene loads another.
  void endBattleBank(bool won);
  // Slots 3 and 4 follow the partner's species (sound.c: loadPartnerSounds, which the partner's
  // start, its evolution, evl.c, and its rebirth, dooa.c: loadVLALL, call with its new type): the
  // vlall entry of its type (a species no entry names takes the fallback one) and its vball entry.
  // The function gives the partner's species id, empty for none; it must outlive the sound.
  void attachPartner(std::function<std::string()> species);
  // The enemies of a battle, in their order: enemy i takes slot 4 + i with the vball entry of its
  // species (btl.c: loadBattleData, readVBALLSection(i + 4, type)). The slots keep them after the
  // battle, as the original does, until the next battle loads others.
  void useEnemyBanks(std::span<const std::string> species);
  // One of slots 4 to 7 takes the vball entry of the original's species number `type`
  // (sound_async.c: readVBALLSection), as routine 51 loads the final boss's into slot 5. Slot 4 is
  // the partner's: it is left alone.
  void useSpeciesBank(std::int64_t slot, std::int32_t type);
  void playSound(std::int64_t slot, std::int64_t sound) override;
  std::optional<std::int64_t> playHeldSound(std::int64_t slot, std::int64_t sound) override;
  void stopHeldSound(std::int64_t held) override;

private:
  // Plays a sound and returns its voices, warning once when it cannot.
  std::optional<std::vector<audio::VoiceId>> playVoices(std::int64_t slot, std::int64_t sound);
  [[nodiscard]] std::optional<std::string> folderOf(std::int64_t slot);
  // The vlall (or vball) folder of a species, read the first time from the banks' used_by.
  std::optional<std::string> speciesFolder(std::string_view species, bool partnerBank);
  void readSpeciesBanks();
  const audio::EffectBank* bank(const std::string& folder);
  // Lets go of the banks no slot uses.
  void dropUnusedBanks();
  // Whether a sound has not warned yet; marks it as warned.
  bool firstWarning(std::int64_t slot, std::int64_t sound);

  audio::Audio* audio_;
  const assets::VirtualFileSystem* files_;
  std::optional<assets::MapTable> maps_;
  bool mapsRead_ = false;
  std::optional<std::string> mapBank_;
  std::optional<std::string> sceneBank_;
  bool inBattle_ = false;
  // A fled or lost battle's bank, still in slot 8.
  bool battleBankKept_ = false;
  std::function<std::string()> partnerSpecies_;
  // The vball folders in slots 5 to 7, empty for a slot nothing loaded.
  std::array<std::optional<std::string>, 3> enemyBanks_;
  // Species id (and the original's number) -> folder, from the used_by of sound/vball/* and
  // sound/vlall/*.
  bool speciesBanksRead_ = false;
  std::map<std::string, std::string, std::less<>> vballOf_;
  std::map<std::int32_t, std::string> vballOfType_;
  std::map<std::string, std::string, std::less<>> vlallOf_;
  std::optional<std::string> vlallFallback_;
  std::map<std::string, audio::EffectBank, std::less<>> banks_;
  // Kept across maps, so a bank that failed to load is not tried (or reported) again.
  std::set<std::string, std::less<>> failedBanks_;
  std::set<std::pair<std::int64_t, std::int64_t>> warnedSounds_;
  // The voices of the held sounds, by handle.
  std::map<std::int64_t, std::vector<audio::VoiceId>> held_;
  std::int64_t nextHeld_ = 0;
};

} // namespace dwe::session
