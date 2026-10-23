#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/field/field_items.hpp>
#include <dwe/session/session_system.hpp>

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::session {

class InventoryAdapter;
class StoryAdapter;

// The ruleset section of the items on the ground and the chests.
inline constexpr std::string_view FieldItemsSection = "field_items";

// A sound of the original's: the sound `sound` of slot `slot` (sound.c: playSound).
struct SoundCue {
  std::int64_t slot = 0;
  std::int64_t sound = 0;
};

// The clips of the feeding scene, by the original's animation ids (startAnimation).
struct FeedClips {
  std::uint32_t playerOffer = 0;
  std::uint32_t playerRefused = 0;
  std::uint32_t partnerIdle = 0;
  std::uint32_t partnerEat = 0;
  std::uint32_t partnerRefuse = 0;
};

// Giving the partner an item in the field (partner.c: partnerTickFeedItem, checkEatDistance,
// ITEM_TAKE_DISTANCE).
struct FeedRules {
  SoundCue sound;
  // The partner takes the item at rangeBase + rangePerDistance * its take distance or nearer.
  std::int32_t rangeBase = 0;
  std::int32_t rangePerDistance = 0;
  // The take distance of a species, by species id; defaultDistance for the others.
  std::int32_t defaultDistance = 0;
  std::map<std::string, std::int32_t, std::less<>> distances;
  // The player's offer clip frame past which the partner takes or refuses it, and the partner's
  // eat clip frame on which the player lowers the hand.
  std::int32_t offerFrame = 0;
  std::int32_t handDownFrame = 0;
  FeedClips clips;

  // The range for a species (checkEatDistance's targetDistance).
  [[nodiscard]] float rangeOf(std::string_view species) const;
};

// The partner's walk to the toilet (partner.c: tickPartnerToilet): the camera's move to the toilet
// and back, in the original's frames, and the clip at the toilet.
struct ToiletWalkRules {
  std::int32_t cameraFrames = 0;
  std::uint32_t partnerClip = 0;
};

// The field_items section: the ground items and the chests, and the field's sounds.
struct FieldItemsRules {
  game::field::FieldItemRules items;
  SoundCue pickupSound;
  SoundCue toiletSound;
  SoundCue sleepSound;
  SoundCue praiseSound;
  SoundCue scoldSound;
  // Without one, an item from the bag goes to the partner at once.
  std::optional<FeedRules> feed;
  // Without one, the partner goes at the toilet where it stands.
  std::optional<ToiletWalkRules> toiletWalk;
};

// Reads the field_items section; errors name the file and line.
core::Result<FieldItemsRules> readFieldItemRules(const ruleset::Section& section);

// What taking an item off the ground or out of a chest came to (tamer.c: tamerTickPickupItem,
// tamerTickTakeChest).
enum class TakeOutcome : std::uint8_t {
  Taken,
  // The bag had no room: the item stays where it was.
  BagFull,
  // The chest was taken before.
  Empty,
};

// Glues the field's ground items and chests to the bag and the story: what the player picks up goes
// to the bag, and a chest taken sets its story trigger. They are not saved: the map's scripts spawn
// them again when the player comes back (FieldItems::enterMap clears them on another map), and the
// chest's trigger says it was taken.
class FieldItemsAdapter final : public SessionSystem {
public:
  // The inventory and the story must outlive the adapter.
  FieldItemsAdapter(InventoryAdapter& inventory, StoryAdapter& story);

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // Reads the field_items section; without one, nothing lies on the ground.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;

  // The field's sounds; none without a field_items section.
  [[nodiscard]] const std::optional<FieldItemsRules>& rules() const { return rules_; }

  [[nodiscard]] game::field::FieldItems& items() { return items_; }
  [[nodiscard]] const game::field::FieldItems& items() const { return items_; }

  // A chest a script places (map_object.c: spawnChest): taken already if its trigger is set.
  // `mapId` is the map the player is on; false when there is no such item or no room.
  bool spawnChest(std::string_view mapId, std::int32_t originalItem, glm::vec3 position,
                  std::int32_t rotation, std::int32_t trigger);

  // Picks up a ground item into the bag (item.c: pickupItem).
  TakeOutcome pickUp(std::size_t slot);

  // Takes a chest's item into the bag and sets its trigger (tamer.c: tamerTickTakeChest).
  TakeOutcome takeChest(std::size_t chest);

  // An item a script lays on a cell of the map the player is on (item.c: spawnItem), by the
  // original's item number; false when there is no such item or no room on the ground.
  bool spawnItem(std::string_view mapId, std::int32_t originalItem, assets::CollisionCell cell,
                 float height);

  // An item a beaten enemy left where it stood on the map the player is on (item.c:
  // spawnDroppedItems), by the item's id; false when there is no such item or no room on the
  // ground.
  bool dropItem(std::string_view mapId, std::string_view item, glm::vec3 position);

private:
  [[nodiscard]] std::optional<assets::ItemHandle> itemOf(std::int32_t originalItem) const;

  InventoryAdapter* inventory_;
  StoryAdapter* story_;
  game::field::FieldItems items_{{}};
  std::optional<FieldItemsRules> rules_;
};

} // namespace dwe::session
