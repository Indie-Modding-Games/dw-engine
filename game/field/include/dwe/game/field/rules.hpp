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

// The highest shade of a fade: what one color channel holds.
inline constexpr std::int32_t MaxShade = 255;

// The map change (tamer.c: tamerTickChangeMap): the screen fades out while the player walks on without collision,
// the next map loads and the screen fades back in before the player can move (fade.c: fadeToBlack(20),
// fadeFromBlack(20)). Counted in frames of the original, as its fade steps once per frame.
struct TransitionRules {
    // How long one frame of the original lasts (20 per second).
    float frameSeconds = 0.05F;
    // The fade out's frame on which the new map loads (FADE_OUT_CURRENT reaching 20), and the fade in's frame on
    // which the player moves again (FADE_IN_CURRENT reaching 20); the fade in still draws that frame.
    std::int32_t fadeOutFrames = 20;
    std::int32_t fadeInFrames = 20;
    // fade.c: renderFade subtracts a grey "shade" from the screen: it rises by shadeStep per frame up to shadeMax
    // (renderFadeOut) and falls back from shadeMax by shadeStep per frame (renderFadeIn).
    std::int32_t shadeStep = 7;
    std::int32_t shadeMax = 160;
    // The shade from which the whole screen goes black: the fade's texel is white and the console multiplies it by
    // shade / 128.
    std::int32_t shadeFull = 128;
    // The fade out's frame on which the new map's name appears (addMapNameObject); it goes when the map loads.
    std::int32_t nameFrame = 10;
};

// The shade of the fade out on frame `frame` (1 = the first): it starts from what the last fade in left, because
// fade.c: fadeToBlack does not reset FADE_PROGRESS.
[[nodiscard]] std::int32_t fadeOutShade(const TransitionRules& rules, std::int32_t frame);

// The shade of the fade in on frame `frame` (1 = the first, drawn on the frame the new map loads).
[[nodiscard]] std::int32_t fadeInShade(const TransitionRules& rules, std::int32_t frame);

// How much of the screen a shade takes away: 0 (nothing) to 1 (everything, black).
[[nodiscard]] float shadeDarkness(const TransitionRules& rules, std::int32_t shade);

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
    TimeOfDayRules timeOfDay;
};

// Where a new field session starts (`field/warps` in the game pack, content/schemas/field_warps.schema.json). The
// warps the scripts make are in `field/script_warps` (script_warps.hpp).
struct FieldWarps {
    std::string startMap;
    std::int32_t startExit = 0;
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
