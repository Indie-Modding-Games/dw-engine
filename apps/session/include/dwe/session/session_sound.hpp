#pragma once

#include <dwe/assets/map_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/audio/effect_bank.hpp>
#include <dwe/game/story/story_ports.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace dwe::session {

// The original's sound slots on the game's audio
// (docs/architecture/audio.md#64-el-tramo-2-en-detalle): slot 0 plays sound/ss, slot 1 sound/sl and
// slot 8 the bank of the map, sound/esall/<sound_id of data/maps>. Banks load the first time a slot
// needs them. A bank that does not load leaves its slot silent, with one warning, and a sound that
// does not play warns once.
class SessionSound final : public game::story::SoundPort {
public:
  // Both must outlive the session.
  SessionSound(audio::Audio& audio, const assets::VirtualFileSystem& files);

  // Slot 8 follows the map; the bank of the map before is let go.
  void enterMap(std::string_view mapId);
  void playSound(std::int64_t slot, std::int64_t sound) override;

private:
  [[nodiscard]] std::optional<std::string> folderOf(std::int64_t slot) const;
  const audio::EffectBank* bank(const std::string& folder);
  // Whether a sound has not warned yet; marks it as warned.
  bool firstWarning(std::int64_t slot, std::int64_t sound);

  audio::Audio* audio_;
  const assets::VirtualFileSystem* files_;
  std::optional<assets::MapTable> maps_;
  bool mapsRead_ = false;
  std::optional<std::string> mapBank_;
  std::map<std::string, audio::EffectBank, std::less<>> banks_;
  // Kept across maps, so a bank that failed to load is not tried (or reported) again.
  std::set<std::string, std::less<>> failedBanks_;
  std::set<std::pair<std::int64_t, std::int64_t>> warnedSounds_;
};

} // namespace dwe::session
