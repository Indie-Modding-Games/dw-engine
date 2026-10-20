#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::game::field {

// The tunables of the field, read from `field/rules` in the game pack (content/schemas/field_rules.schema.json).
// Distances are in world units (100 = one collision cell), speeds per second and angles in radians, whatever the tick
// rate. The defaults are the original's values (docs/reference/campo.md).

// How a character moves.
struct MoverRules {
    // The .glb drawn for it.
    std::string model;
    // The collision size (DIGIMON_DATA[type].radius): the probe reaches `radius` ahead along the axis of movement and
    // `radius / 2` to each side (map_collision.c).
    float radius = 100.0F;
    float walkSpeed = 400.0F;
    float runSpeed = 1000.0F;
    // How fast the facing turns toward the way it moves; 0 turns at once, as the original.
    float turnSpeed = 0.0F;
};

// How the partner follows the player (partner.c: getPartnerTamerCloseness, partner tick).
struct PartnerRules {
    MoverRules mover;
    // Beyond this many partner radii from the player it runs; between walkDistance and this it walks; closer, it stops.
    float runDistance = 3.5F;
    float walkDistance = 2.5F;
    // How far from the player it appears when entering a map (tamer.c: loadMapEntities).
    float spawnDistance = 200.0F;
    // The most cells it remembers of the player's path when it cannot see the player.
    std::int32_t trailLength = 30;
    // Half the width of the corridor that must be clear for it to see the player (map.c: isFiveTileWidePathBlocked
    // tests five parallel lines of cells).
    float sightHalfWidth = 200.0F;
    // If it moves less than a tenth of what it should for this long, or is farther than teleportDistance, it is
    // placed behind the player.
    float stuckSeconds = 1.0F;
    float teleportDistance = 2000.0F;
};

// How the player's intents are read.
struct InputRules {
    // Stick vectors shorter than this are no movement.
    float deadZone = 0.25F;
    // Click-to-move: stopped by a wall this close to the clicked point counts as arrived.
    float arriveDistance = 25.0F;
};

// The map change (tamer.c: tamerTickChangeMap): the screen fades out while the player walks on without collision,
// the next map loads and the screen fades back in before the player can move (fade.c: fadeToBlack(20),
// fadeFromBlack(20), 20 frames each).
struct TransitionRules {
    float fadeOutSeconds = 1.0F;
    float fadeInSeconds = 1.0F;
};

// The game clock (clock.c: tickGameClock): a minute every 20 frames at normal speed, starting at 8:00 in a new game.
// It stops while a map change runs (tamer.c: stopGameTime, startGameTime).
struct ClockRules {
    std::int32_t startHour = 8;
    float minuteSeconds = 1.0F;
};

// The times of day (map.c: updateTimeOfDay): the hours each one starts at, which are also the only hours the palette
// fades at (map.c: renderMap), and what dusk and night do to the map's lights, per red, green and blue.
struct TimeOfDayRules {
    std::int32_t dayStart = 6;
    std::int32_t duskStart = 16;
    std::int32_t nightStart = 20;
    glm::vec3 duskLight{1.0F, 0.7F, 0.7F};
    glm::vec3 nightLight{0.5F};
};

struct FieldRules {
    MoverRules player;
    PartnerRules partner;
    InputRules input;
    TransitionRules transition;
    ClockRules clock;
    TimeOfDayRules timeOfDay;
};

// A warp the original does in an event script instead of the map's warp table: stepping on `trigger` in `map` takes
// the player to `targetExit` of `targetMap`. Each entry says in `evidence` which script does it.
struct ScriptedWarp {
    std::string map;
    std::int32_t trigger = 0;
    std::string targetMap;
    std::int32_t targetExit = 0;
    std::string evidence;
};

// Where a new field session starts, and the warps that come from scripts (`field/warps` in the game pack,
// content/schemas/field_warps.schema.json).
struct FieldWarps {
    std::string startMap;
    std::int32_t startExit = 0;
    std::vector<ScriptedWarp> scriptedWarps;
};

// Parses the text of `field/rules`. Unknown and missing keys are errors.
core::Result<FieldRules> parseFieldRules(std::string_view yaml);

// Parses the text of `field/warps`.
core::Result<FieldWarps> parseFieldWarps(std::string_view yaml);

// Reads `field/rules` from the virtual file system.
core::Result<FieldRules> loadFieldRules(const assets::VirtualFileSystem& vfs);

// Reads `field/warps` from the virtual file system.
core::Result<FieldWarps> loadFieldWarps(const assets::VirtualFileSystem& vfs);

} // namespace dwe::game::field
