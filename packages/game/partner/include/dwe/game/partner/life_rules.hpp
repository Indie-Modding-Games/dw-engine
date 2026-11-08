#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::partner {

// The life section (rules/<id>/life.yaml, docs/reference/care.md "Vida y edad", "Muerte y
// renacer"). Hours are game hours.
inline constexpr std::string_view LifeSection = "life";

// The lifetime unhappiness costs: every so many hours, below a happiness, (happiness - below) /
// -divisor + extra hours.
struct UnhappyLifetime {
  std::int32_t everyHours = 0;
  std::int32_t happinessBelow = 0;
  std::int32_t happinessDivisor = 0;
  std::int32_t extraHours = 0;
};

// A baby the partner can be reborn as, with its starting stats (initializeReincarnatedPartner).
struct RebirthBaby {
  std::string digimon;
  StatBlock stats;
};

struct RebirthRules {
  std::string babyChoice;
  std::vector<RebirthBaby> babies;
  std::int32_t inheritPercentPerTamerLevel = 0;
  // dooa.c: DOOA_getSequenceState: a partner with no lives left gets a random egg, whatever the
  // egg box said. Off (the option choose_egg_after_lives_lost), the egg box's egg counts too.
  bool randomEggWithoutLives = true;
};

// A moment of a scene, in frames of the original, and the sound of slot 8 it plays then.
struct SceneCue {
  std::int32_t frame = 0;
  std::int32_t sound = 0;
};

// A life lost to a battle or a sickness (murd.c: MURD_tickScene): an iris closes on the partner,
// which stays alone on black with the lives box until aloneUntil; with lives left the screen fades
// out, the tamer wakes up on the wake map and the screen fades back in (shades of fade.c).
struct LostLifeScene {
  std::int32_t irisFrames = 0;
  std::int32_t aloneUntil = 0;
  std::int32_t fadeOutEnd = 0;
  std::int32_t mapBack = 0;
  std::int32_t fadeInEnd = 0;
  std::int32_t shadeMax = 0;
  std::int32_t shadeHold = 0;
};

// Where the tamer wakes up after a lost life or a death (dooa.c: DOOA_tick, murd.c: MURD_tick):
// storyMap once one of the story triggers is set or the story variable reaches atLeast, else
// earlyMap; maps by the original's index.
struct WakeMap {
  std::int32_t storyMap = 0;
  std::int32_t earlyMap = 0;
  std::int32_t exit = 0;
  std::vector<std::int32_t> storyTriggers;
  std::int32_t storyVariable = 0;
  std::int32_t storyVariableAtLeast = 0;
  // How many original pixels right of the screen's centre the death's camera starts on the story
  // map (dooa.c: DOOA_tickDissolve phase 0: 0xBC instead of 0xA0 when DEATH_MAP_TARGET is 0xCD).
  std::int32_t storyMapStartX = 0;

  // The death camera's start, in original pixels right of the screen's centre, on `map`.
  [[nodiscard]] std::int32_t startOffsetX(std::int32_t map) const {
    return map == storyMap ? storyMapStartX : 0;
  }
};

// The partner's clips in the death scenes, by the original's ids.
struct DeathSceneClips {
  std::uint32_t alive = 0;
  std::uint32_t lastLife = 0;
  std::uint32_t burst = 0;
  std::uint32_t vanish = 0;
  std::uint32_t reborn = 0;
  std::uint32_t finish = 0;
};

// The death (dooa.c: DOOA_tickDissolve) and the rebirth (DOOA_tickRebirth), in frames of the
// original from the start of each.
struct DeathScene {
  std::int32_t soundBank = 0;
  // The dissolve: the wake map loads, the partner stands alone, bursts, sinks, vanishes and a light
  // rises until endFrame. A partner with no lives left starts at mapFrame, already gone.
  std::int32_t mapFrame = 0;
  std::int32_t standFrame = 0;
  SceneCue burst;
  SceneCue sink;
  SceneCue vanish;
  SceneCue rise;
  // The wake map's first character (ENTITY_TABLE[2], the elder in his house) shows again on black
  // from hostFrame, and stays through the rebirth.
  std::int32_t hostFrame = 0;
  std::int32_t endFrame = 0;
  // The rebirth: the egg glows and hatches, the new baby is drawn at swapFrame while the scenery
  // fades back in until shownFrame, it plays its clip and finishes at finishFrame.
  SceneCue glow;
  SceneCue hatch;
  std::int32_t swapFrame = 0;
  std::int32_t shownFrame = 0;
  std::int32_t sparkleSound = 0;
  std::int32_t finishFrame = 0;
  DeathSceneClips clips;
};

// The scenes of a lost life, a death and a rebirth; without them the partner changes at once.
struct LifeScenes {
  LostLifeScene lostLife;
  WakeMap wakeMap;
  DeathScene death;
};

struct LifeRules {
  std::int32_t lifetimeHours = 0;
  std::int32_t lives = 0;
  UnhappyLifetime unhappyLifetime;
  RebirthRules rebirth;
  std::optional<LifeScenes> scenes;
};

// Reads the life section; errors name the file, line and column.
core::Result<LifeRules> readLifeRules(const ruleset::Section& section);

} // namespace dwe::game::partner
