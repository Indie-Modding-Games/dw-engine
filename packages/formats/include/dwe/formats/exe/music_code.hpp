#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/exe/executable_layout.hpp>
#include <dwe/formats/exe/memory_image.hpp>

#include <cstdint>
#include <optional>
#include <vector>

// The music the executable picks with constants in its code, not in a data table: the fixed font
// and song of the story's music modes (script_interp.c: handleMusicOverride) and which song of the
// battle font a battle plays (btl.c: loadBattleData). Read from the user's executable where the
// layout locates them; data/music.yaml carries them (content/schemas/music.schema.json).
namespace dwe::formats::exe {

// A music mode of variable 245 that plays a fixed font and song, whatever the map asked for.
struct FieldMusicOverride {
  std::uint8_t mode = 0;
  std::uint8_t font = 0;
  std::uint8_t track = 0;
};

// loadBattleData: the battle font; bossTrack when the first enemy is of bossType, scriptedTrack in
// a predefined battle, ENEMY_MUSIC_MAPPING[type - firstEnemyType] for the types from firstEnemyType
// up to before endEnemyType, and defaultTrack for the rest.
struct BattleMusicCode {
  std::uint8_t font = 0;
  std::uint8_t bossType = 0;
  std::uint8_t bossTrack = 0;
  std::uint8_t scriptedTrack = 0;
  std::uint8_t defaultTrack = 0;
  std::uint8_t firstEnemyType = 0;
  std::uint8_t endEnemyType = 0;
};

struct MusicCode {
  std::vector<FieldMusicOverride> fieldOverrides; // empty when the layout does not locate them
  std::optional<BattleMusicCode> battle;          // nothing when the layout does not locate it
};

// Reads what the layout locates; fails when a located instruction or case is not what it should be.
core::Result<MusicCode> readMusicCode(const MemoryImage& image, const ExecutableLayout& layout);

} // namespace dwe::formats::exe
