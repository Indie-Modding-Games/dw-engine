#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/field/field_items.hpp>
#include <dwe/session/session_system.hpp>

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
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

// The field_items section: the ground items and the chests, and the sounds of the field's cells.
struct FieldItemsRules {
  game::field::FieldItemRules items;
  SoundCue pickupSound;
  SoundCue toiletSound;
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
// them again when the player comes back (FieldItems::enterMap clears them on another map), and the chest's trigger says it was taken.
class FieldItemsAdapter final : public SessionSystem {
public:
  // The inventory and the story must outlive the adapter.
  FieldItemsAdapter(InventoryAdapter& inventory, StoryAdapter& story);

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // Reads the field_items section; without one, nothing lies on the ground.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;

  // The sounds of picking up an item and of the toilet; none without a field_items section.
  [[nodiscard]] std::optional<SoundCue> pickupSound() const { return pickupSound_; }
  [[nodiscard]] std::optional<SoundCue> toiletSound() const { return toiletSound_; }

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

private:
  [[nodiscard]] std::optional<assets::ItemHandle> itemOf(std::int32_t originalItem) const;

  InventoryAdapter* inventory_;
  StoryAdapter* story_;
  game::field::FieldItems items_{{}};
  std::optional<SoundCue> pickupSound_;
  std::optional<SoundCue> toiletSound_;
};

} // namespace dwe::session
