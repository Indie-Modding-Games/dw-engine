#include <dwe/game/partner/life_rules.hpp>
#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/training_rules.hpp>

#include <string>
#include <string_view>
#include <utility>

#include "section_reading.hpp"

namespace dwe::game::partner {

namespace {

using assets::YamlReader;
using core::YamlNode;
using messages::formatMessage;
using namespace reading;

constexpr std::string_view LifetimeHoursKey = "lifetime_hours";
constexpr std::string_view LivesKey = "lives";
constexpr std::string_view UnhappyLifetimeKey = "unhappy_lifetime";
constexpr std::string_view EveryHoursKey = "every_hours";
constexpr std::string_view HappinessBelowKey = "happiness_below";
constexpr std::string_view HappinessDivisorKey = "happiness_divisor";
constexpr std::string_view ExtraHoursKey = "extra_hours";
constexpr std::string_view RebirthKey = "rebirth";
constexpr std::string_view BabyChoiceKey = "baby_choice";
constexpr std::string_view BabiesKey = "babies";
constexpr std::string_view InheritKey = "inherit_percent_per_tamer_level";
constexpr std::string_view DigimonKey = "digimon";

constexpr std::string_view ScenesKey = "scenes";
constexpr std::string_view LostLifeKey = "lost_life";
constexpr std::string_view IrisFramesKey = "iris_frames";
constexpr std::string_view AloneUntilKey = "alone_until";
constexpr std::string_view FadeOutEndKey = "fade_out_end";
constexpr std::string_view MapBackKey = "map_back";
constexpr std::string_view FadeInEndKey = "fade_in_end";
constexpr std::string_view ShadeMaxKey = "shade_max";
constexpr std::string_view ShadeHoldKey = "shade_hold";
constexpr std::string_view WakeMapKey = "wake_map";
constexpr std::string_view StoryMapKey = "story_map";
constexpr std::string_view EarlyMapKey = "early_map";
constexpr std::string_view ExitKey = "exit";
constexpr std::string_view StoryTriggersKey = "story_triggers";
constexpr std::string_view StoryVariableKey = "story_variable";
constexpr std::string_view StoryVariableAtLeastKey = "story_variable_at_least";
constexpr std::string_view StoryMapStartXKey = "story_map_start_x";
constexpr std::string_view DeathKey = "death";
constexpr std::string_view SoundBankKey = "sound_bank";
constexpr std::string_view MapFrameKey = "map_frame";
constexpr std::string_view StandFrameKey = "stand_frame";
constexpr std::string_view BurstKey = "burst";
constexpr std::string_view SinkKey = "sink";
constexpr std::string_view VanishKey = "vanish";
constexpr std::string_view RiseKey = "rise";
constexpr std::string_view EndFrameKey = "end_frame";
constexpr std::string_view HostFrameKey = "host_frame";
constexpr std::string_view GlowKey = "glow";
constexpr std::string_view HatchKey = "hatch";
constexpr std::string_view SwapFrameKey = "swap_frame";
constexpr std::string_view ShownFrameKey = "shown_frame";
constexpr std::string_view SparkleSoundKey = "sparkle_sound";
constexpr std::string_view FinishFrameKey = "finish_frame";
constexpr std::string_view ClipsKey = "clips";
constexpr std::string_view FrameKey = "frame";
constexpr std::string_view SoundKey = "sound";
constexpr std::string_view AliveClipKey = "alive";
constexpr std::string_view LastLifeClipKey = "last_life";
constexpr std::string_view RebornClipKey = "reborn";
constexpr std::string_view FinishClipKey = "finish";

UnhappyLifetime readUnhappyLifetime(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, UnhappyLifetimeKey,
             {EveryHoursKey, HappinessBelowKey, HappinessDivisorKey, ExtraHoursKey}, node)) {
    return {};
  }
  return {.everyHours = positive(reader, *node, EveryHoursKey),
          .happinessBelow = integer(reader, *node, HappinessBelowKey),
          .happinessDivisor = positive(reader, *node, HappinessDivisorKey),
          .extraHours = nonNegative(reader, *node, ExtraHoursKey)};
}

RebirthBaby readBaby(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::RebirthBabyText,
                         {DigimonKey, statName(Stat::Hp), statName(Stat::Mp),
                          statName(Stat::Offense), statName(Stat::Defense), statName(Stat::Speed),
                          statName(Stat::Brain)})) {
    return {};
  }
  RebirthBaby baby;
  const YamlNode& digimon = reader.field(node, DigimonKey);
  baby.digimon = reader.ok() ? reader.text(digimon) : std::string{};
  for (const Stat stat : AllStats) {
    baby.stats[stat] = positive(reader, node, statName(stat));
  }
  return baby;
}

RebirthRules readRebirth(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, RebirthKey, {BabyChoiceKey, BabiesKey, InheritKey}, node)) {
    return {};
  }
  RebirthRules rules;
  const YamlNode& choice = reader.field(*node, BabyChoiceKey);
  rules.babyChoice = reader.ok() ? reader.text(choice) : std::string{};
  const YamlNode& babies = reader.field(*node, BabiesKey);
  if (reader.ok()) {
    for (const YamlNode& baby : reader.sequence(babies, BabiesKey)) {
      rules.babies.push_back(readBaby(reader, baby));
    }
    if (reader.ok() && rules.babies.empty()) {
      reader.fail(babies, formatMessage(messages::NoneListed, BabiesKey));
    }
  }
  rules.inheritPercentPerTamerLevel = nonNegative(reader, *node, InheritKey);
  return rules;
}

LostLifeScene readLostLife(YamlReader& reader, const YamlNode& parent) {
  const YamlNode* node = nullptr;
  if (!group(reader, parent, LostLifeKey,
             {IrisFramesKey, AloneUntilKey, FadeOutEndKey, MapBackKey, FadeInEndKey, ShadeMaxKey,
              ShadeHoldKey},
             node)) {
    return {};
  }
  return {.irisFrames = positive(reader, *node, IrisFramesKey),
          .aloneUntil = positive(reader, *node, AloneUntilKey),
          .fadeOutEnd = positive(reader, *node, FadeOutEndKey),
          .mapBack = positive(reader, *node, MapBackKey),
          .fadeInEnd = positive(reader, *node, FadeInEndKey),
          .shadeMax = positive(reader, *node, ShadeMaxKey),
          .shadeHold = positive(reader, *node, ShadeHoldKey)};
}

WakeMap readWakeMap(YamlReader& reader, const YamlNode& parent) {
  const YamlNode* node = nullptr;
  if (!group(reader, parent, WakeMapKey,
             {StoryMapKey, EarlyMapKey, ExitKey, StoryTriggersKey, StoryVariableKey,
              StoryVariableAtLeastKey, StoryMapStartXKey},
             node)) {
    return {};
  }
  WakeMap wake{.storyMap = nonNegative(reader, *node, StoryMapKey),
               .earlyMap = nonNegative(reader, *node, EarlyMapKey),
               .exit = nonNegative(reader, *node, ExitKey),
               .storyTriggers = {},
               .storyVariable = nonNegative(reader, *node, StoryVariableKey),
               .storyVariableAtLeast = integer(reader, *node, StoryVariableAtLeastKey),
               .storyMapStartX = integer(reader, *node, StoryMapStartXKey)};
  const YamlNode& triggers = reader.field(*node, StoryTriggersKey);
  if (reader.ok()) {
    for (const YamlNode& trigger : reader.sequence(triggers, StoryTriggersKey)) {
      wake.storyTriggers.push_back(reader.integer<std::int32_t>(trigger));
    }
  }
  return wake;
}

SceneCue readCue(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode* node = nullptr;
  if (!group(reader, parent, key, {FrameKey, SoundKey}, node)) {
    return {};
  }
  return {.frame = positive(reader, *node, FrameKey),
          .sound = nonNegative(reader, *node, SoundKey)};
}

std::uint32_t clip(YamlReader& reader, const YamlNode& node, std::string_view key) {
  return static_cast<std::uint32_t>(nonNegative(reader, node, key));
}

DeathSceneClips readClips(YamlReader& reader, const YamlNode& parent) {
  const YamlNode* node = nullptr;
  if (!group(reader, parent, ClipsKey,
             {AliveClipKey, LastLifeClipKey, BurstKey, VanishKey, RebornClipKey, FinishClipKey},
             node)) {
    return {};
  }
  return {.alive = clip(reader, *node, AliveClipKey),
          .lastLife = clip(reader, *node, LastLifeClipKey),
          .burst = clip(reader, *node, BurstKey),
          .vanish = clip(reader, *node, VanishKey),
          .reborn = clip(reader, *node, RebornClipKey),
          .finish = clip(reader, *node, FinishClipKey)};
}

DeathScene readDeath(YamlReader& reader, const YamlNode& parent) {
  const YamlNode* node = nullptr;
  if (!group(reader, parent, DeathKey,
             {SoundBankKey, MapFrameKey, StandFrameKey, BurstKey, SinkKey, VanishKey, RiseKey,
              HostFrameKey, EndFrameKey, GlowKey, HatchKey, SwapFrameKey, ShownFrameKey,
              SparkleSoundKey, FinishFrameKey, ClipsKey},
             node)) {
    return {};
  }
  DeathScene death{.soundBank = nonNegative(reader, *node, SoundBankKey),
                   .mapFrame = positive(reader, *node, MapFrameKey),
                   .standFrame = positive(reader, *node, StandFrameKey),
                   .burst = readCue(reader, *node, BurstKey),
                   .sink = readCue(reader, *node, SinkKey),
                   .vanish = readCue(reader, *node, VanishKey),
                   .rise = readCue(reader, *node, RiseKey),
                   .hostFrame = positive(reader, *node, HostFrameKey),
                   .endFrame = positive(reader, *node, EndFrameKey),
                   .glow = readCue(reader, *node, GlowKey),
                   .hatch = readCue(reader, *node, HatchKey),
                   .swapFrame = positive(reader, *node, SwapFrameKey),
                   .shownFrame = positive(reader, *node, ShownFrameKey),
                   .sparkleSound = nonNegative(reader, *node, SparkleSoundKey),
                   .finishFrame = positive(reader, *node, FinishFrameKey),
                   .clips = readClips(reader, *node)};
  // The dissolve's and the rebirth's moments each come in order.
  const bool inOrder =
      death.mapFrame < death.standFrame && death.standFrame < death.burst.frame &&
      death.burst.frame < death.sink.frame && death.sink.frame < death.vanish.frame &&
      death.vanish.frame < death.rise.frame && death.rise.frame < death.endFrame &&
      death.glow.frame < death.hatch.frame && death.hatch.frame < death.swapFrame &&
      death.swapFrame < death.shownFrame && death.shownFrame < death.finishFrame;
  if (reader.ok() && !inOrder) {
    reader.fail(*node, messages::DeathSceneOutOfOrder);
  }
  return death;
}

std::optional<LifeScenes> readScenes(YamlReader& reader, const YamlNode* node) {
  if (node == nullptr ||
      !reader.expectKeys(*node, ScenesKey, {LostLifeKey, WakeMapKey, DeathKey})) {
    return std::nullopt;
  }
  LifeScenes scenes{.lostLife = readLostLife(reader, *node),
                    .wakeMap = readWakeMap(reader, *node),
                    .death = readDeath(reader, *node)};
  const LostLifeScene& lost = scenes.lostLife;
  const bool inOrder = lost.irisFrames < lost.aloneUntil && lost.aloneUntil < lost.fadeOutEnd &&
                       lost.fadeOutEnd <= lost.mapBack && lost.mapBack < lost.fadeInEnd;
  if (reader.ok() && !inOrder) {
    reader.fail(*node, messages::LostLifeSceneOutOfOrder);
  }
  return scenes;
}

} // namespace

core::Result<LifeRules> readLifeRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, messages::LifeSectionText,
                         {LifetimeHoursKey, LivesKey, UnhappyLifetimeKey, RebirthKey},
                         {ScenesKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  LifeRules rules{.lifetimeHours = positive(reader, root, LifetimeHoursKey),
                  .lives = positive(reader, root, LivesKey),
                  .unhappyLifetime = readUnhappyLifetime(reader, root),
                  .rebirth = readRebirth(reader, root),
                  .scenes = readScenes(reader, root.find(ScenesKey))};
  return reader.finish(std::move(rules), section.file);
}

} // namespace dwe::game::partner
