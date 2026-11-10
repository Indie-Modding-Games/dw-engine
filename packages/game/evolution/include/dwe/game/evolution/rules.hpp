#pragma once

#include <dwe/assets/entry_class.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::evolution {

// The ruleset section of evolution: rules/<id>/evolution.yaml
// (content/schemas/ruleset_evolution.schema.json).
inline constexpr std::string_view EvolutionSection = "evolution";

// The keys of the strategies the section names.
inline constexpr std::string_view SelectionKey = "selection";
inline constexpr std::string_view ScoringKey = "scoring";

// The quirks of the section (docs/reference/evolution.md, "Rarezas"); true reproduces the original.
inline constexpr std::string_view EvolutionItemSticks = "evolution_item_sticks";
inline constexpr std::string_view SelectionAverageCarriesOver = "selection_average_carries_over";
inline constexpr std::string_view UnlistedBabyRestartsTimer = "unlisted_baby_restarts_timer";
inline constexpr std::string_view RookieStatTiePicksLast = "rookie_stat_tie_picks_last";
inline constexpr std::string_view TimerSpecialOverridesNormal = "timer_special_overrides_normal";

// The rules below name forms and items by their table ids; Evolution::create checks that each one
// exists.

// From how many hours of the counter each level checks its evolution.
struct CheckHours {
  std::int32_t baby = 0;
  std::int32_t inTraining = 0;
  std::int32_t rookie = 0;
  std::int32_t champion = 0;
};

// How a candidate is scored.
struct RequirementRules {
  std::int32_t passPoints = 0;
  std::int32_t weightMargin = 0;
  std::int32_t hpMpDivisor = 1;
  // The learned moves that count (evolution.c: getNumMasteredMoves): the original's below
  // mastered_moves_below, and those mastered_move_ids names.
  assets::EntryClass masteredMoves;
};

// Where a rookie with no candidate goes.
struct FallbackRules {
  std::int32_t rookieHours = 0;
  std::string rookieTarget;
};

// What a special rule compares.
enum class SpecialField : std::uint8_t {
  Level,
  EvolutionTimer,
  Discipline,
  Happiness,
  Tiredness,
  Battles,
  Defense,
  Map,
  DisciplineBefore,
  HappinessBefore,
};

enum class Comparison : std::uint8_t {
  Equals,
  AtLeast,
};

struct SpecialRequirement {
  SpecialField field = SpecialField::Level;
  Comparison comparison = Comparison::Equals;
  std::int32_t value = 0;
  // The map the partner must be on, by its id in data/maps, for SpecialField::Map (always Equals).
  // NOLINTNEXTLINE(readability-redundant-member-init): the default lets designated initializers leave it out.
  std::string mapId = {};
};

// A special evolution: when everything it asks holds, the partner turns into the target.
struct SpecialRule {
  std::string target;
  // The check's shared roll must be below this.
  std::optional<std::int32_t> rollBelow;
  // The partner must be one of these forms; empty for any.
  std::vector<std::string> from;
  // Only right after a scold.
  bool scolded = false;
  std::vector<SpecialRequirement> requirements;
};

// The partner of this level with the counter at exactly these hours also runs the praise and scold
// rules.
struct TimerSpecialRule {
  std::int32_t level = 0;
  std::int32_t hours = 0;
};

struct SpecialRules {
  std::int32_t rollLimit = 1;
  std::vector<SpecialRule> wakeUp;
  std::vector<SpecialRule> praiseScold;
  TimerSpecialRule praiseScoldOnTimer;
};

struct ItemRules {
  // Item id -> form, whatever the level.
  std::map<std::string, std::string, std::less<>> fixed;
  // An item of the item table works when its form is this many levels above the partner.
  std::int32_t levelStep = 0;
};

struct StatGainRules {
  std::int32_t gainDivisor = 1;
  std::int32_t scaleDivisor = 1;
  std::vector<std::string> scaledTargets;
  std::int32_t scaledBelowLevel = 0;
  std::int32_t ultimateLifetimeHours = 0;
  std::int32_t championTamerLevelChance = 0;
  std::int32_t ultimateTamerLevelChance = 0;
};

struct MoveRules {
  // The first move of each specialty's block, by specialty: the original's numbers.
  std::vector<std::int32_t> specialtyBlocks;
  std::int32_t blockSize = 0;
  // More moves of a specialty's block, by id, by specialty (specialty_block_ids): those a pack
  // adds.
  std::map<std::int32_t, std::vector<std::string>> specialtyBlockIds;
  std::int32_t basicSlotBelowLevel = 0;
  // The moves the fourth equipped slot takes, the last of the list among them: the original's from
  // finisher_first to before finisher_end, and those finisher_ids names.
  assets::EntryClass finishers;

  // The moves of a specialty's block: the original's from its first to its first + blockSize - 1,
  // and its ids; nothing for a specialty without a block.
  [[nodiscard]] std::optional<assets::EntryClass> specialtyBlock(std::int32_t specialty) const;
};

struct RebirthRules {
  std::vector<std::string> babies;
};

// The camera of the evolution scene (evl.c: EVL_updateEvoCamera, EVL_calculateCameraVectors): it
// circles the partner at max(radius, height) * distancePerSize + distance, raised by rise plus
// twice the height, in the original's angle units per frame.
struct EvolutionSceneCamera {
  std::int32_t startAngle = 0;
  std::int32_t angleStep = 0;
  std::int32_t distancePerSize = 0;
  std::int32_t distance = 0;
  std::int32_t rise = 0;
  // The projection distance it zooms to, and how far the picture drops (pixels) for a partner of
  // dropHeight or taller.
  std::int32_t focalLength = 0;
  std::int32_t maxDrop = 0;
  std::int32_t dropHeight = 0;
};

// A span of the scene's frames, from `from` to `to`.
struct FrameSpan {
  std::int32_t from = 0;
  std::int32_t to = 0;
};

// The partner's clips in the scene, by the original's ids.
struct EvolutionSceneClips {
  std::uint32_t idle = 0;
  std::uint32_t ready = 0;
  std::uint32_t burst = 0;
  std::uint32_t finish = 0;
};

// The evolution scene on the field (main/evl.c: tickEvoSequenceLoading, evl.c:
// EVL_tickEvoSequence), in frames of the original: when the scenery hides, the camera aims and
// moves, the partner bursts, grows, roars, takes its new form and the scene ends.
struct EvolutionSceneRules {
  // The sound bank of the scene in slot 8 (loadMapSounds2), until the map's comes back.
  std::int32_t soundBank = 0;
  std::int32_t waitFrames = 0;
  std::int32_t dimFrames = 0;
  std::int32_t cameraAimEnd = 0;
  std::int32_t cameraMoveEnd = 0;
  std::int32_t burstFrame = 0;
  FrameSpan orbitEase;
  FrameSpan grow;
  std::int32_t roarFrame = 0;
  std::int32_t swapFrame = 0;
  std::int32_t endFrame = 0;
  std::vector<std::int32_t> burstSounds;
  std::int32_t roarSound = 0;
  EvolutionSceneCamera camera;
  EvolutionSceneClips clips;
};

// The evolution section, as read. The strategy names are checked against their registries.
struct EvolutionRules {
  std::string selection;
  std::string scoring;
  CheckHours checkHours;
  // Baby id -> in-training id.
  std::map<std::string, std::string, std::less<>> babyTargets;
  RequirementRules requirements;
  FallbackRules fallback;
  SpecialRules specials;
  ItemRules items;
  StatGainRules statGains;
  MoveRules moves;
  RebirthRules rebirth;
  // Nothing: the partner takes its new form at once, with no scene.
  std::optional<EvolutionSceneRules> scene;
  ruleset::Quirks quirks;
};

// Reads the evolution section; errors name the file, line and column.
core::Result<EvolutionRules> readEvolutionRules(const ruleset::Section& section);

// The check of the evolution section for RulesetStore: it reads the section and checks the strategy
// names.
core::Result<void> checkEvolutionSection(const ruleset::Section& section);

// The evolution rules of a ruleset: its evolution section, with the wake-up specials of its
// all_species section, if any, after the section's own (docs/modulos/game/evoluciones-extra.md).
core::Result<EvolutionRules> readEvolutionRules(const ruleset::Ruleset& ruleset);

// The check of the all_species section for RulesetStore: its species patches and its wake-up
// specials.
core::Result<void> checkAllSpeciesSection(const ruleset::Section& section);

// How many of the learned moves (their ids in the move table) the requirements count.
[[nodiscard]] std::int32_t countMasteredMoves(const RequirementRules& rules,
                                              std::span<const std::string> learned,
                                              const assets::MoveTable& moves);

// Checks the move classes of the rules against the session's move table: every number of a block or
// of the finishers is a move of the original, and every id a move.
[[nodiscard]] core::Result<void> checkMoveClasses(const EvolutionRules& rules,
                                                  const assets::MoveTable& moves);

} // namespace dwe::game::evolution
