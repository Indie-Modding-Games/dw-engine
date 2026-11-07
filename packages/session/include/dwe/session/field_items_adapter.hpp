#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/field/field_items.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/session/session_system.hpp>

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::session {

class InventoryAdapter;
class StoryAdapter;

// The ruleset section of the items on the ground and the chests.
inline constexpr std::string_view FieldItemsSection = "field_items";
// The ruleset section of the rare items (an engine improvement, off by default: the option
// rare_items; docs/modulos/game/evolution.md, «Objetos raros de evolución»).
inline constexpr std::string_view RareItemsSection = "rare_items";

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

// Praising and scolding the partner in the field (partner.c: partnerTickPraiseScold; tamer.c:
// tamerTickPraiseScold): the clips, by the original's animation ids, the player's gesture and the
// partner's answer to it.
struct PraiseScoldRules {
  std::uint32_t praisePlayerClip = 0;
  // The player's praise clip for a tall partner: one of level tallFromLevel (by the original's
  // level numbers) or higher that is not one of shortSpecies.
  std::uint32_t praiseTallPlayerClip = 0;
  std::uint32_t praisePartnerClip = 0;
  std::uint32_t scoldPlayerClip = 0;
  std::uint32_t scoldPartnerClip = 0;
  // The partner's clip for a scold right after a refusal or a pile on the ground.
  std::uint32_t boostedScoldPartnerClip = 0;
  std::int32_t tallFromLevel = 0;
  std::vector<std::string> shortSpecies;

  // The player's praise clip for a partner of this level (by the original's numbers) and species.
  [[nodiscard]] std::uint32_t praisePlayerClipFor(std::int32_t level,
                                                  std::string_view species) const;
};

// The field_items section: the ground items and the chests, and the field's sounds.
struct FieldItemsRules {
  game::field::FieldItemRules items;
  SoundCue pickupSound;
  // The player's clip while an item on the ground is offered, by the original's animation id
  // (tamer.c: tamerTickPickupItem); without one the player stands as it was.
  std::optional<std::uint32_t> pickupPlayerClip;
  SoundCue toiletSound;
  SoundCue sleepSound;
  SoundCue praiseSound;
  SoundCue scoldSound;
  // Without one, an item from the bag goes to the partner at once.
  std::optional<FeedRules> feed;
  // Without one, the partner goes at the toilet where it stands.
  std::optional<ToiletWalkRules> toiletWalk;
  // Without one, praise and scold apply once the partner is beside the player, with no clips.
  std::optional<PraiseScoldRules> praiseScold;
};

// Reads the field_items section; errors name the file and line.
core::Result<FieldItemsRules> readFieldItemRules(const ruleset::Section& section);

// Where the rare items may lie on a map: the cells its head script lays its random items on.
struct RareItemMap {
  std::vector<assets::CollisionCell> spots;
  // The story trigger after which the head lays no items any more; none: it always does.
  std::optional<std::int32_t> unlessTrigger;
};

// The rare items: on each spot of a map, as the map loads, `chancePercent` in 100 that one item of
// the pool lies there instead of what the head script rolls for it.
struct RareItemRules {
  std::int32_t chancePercent = 0;
  // Item ids of data/items.
  std::vector<std::string> pool;
  std::map<std::string, RareItemMap, std::less<>> maps;
};

// Reads the rare_items section; errors name the file and line.
core::Result<RareItemRules> readRareItemRules(const ruleset::Section& section);

// A rare item the roll laid on a spot.
struct RareItemDrop {
  assets::ItemHandle item;
  assets::CollisionCell cell;
};

// Rolls the spots of a map: each one, in order, has `chancePercent` in 100 (Random::chance) of one
// of the `eligible` items, picked evenly; a picked item leaves the pool, so a load lays each item
// once at most. Nothing eligible draws nothing.
[[nodiscard]] std::vector<RareItemDrop> rollRareItems(std::span<const assets::CollisionCell> spots,
                                                      std::int32_t chancePercent,
                                                      std::vector<assets::ItemHandle> eligible,
                                                      ruleset::Random& random);

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

  // The rare items (the option rare_items, off by default). They need a ruleset with a rare_items
  // section: without one, turning them on lays nothing.
  void setRareItems(bool enabled) { rareItemsOn_ = enabled; }
  [[nodiscard]] bool rareItems() const { return rareItemsOn_; }
  [[nodiscard]] const std::optional<RareItemRules>& rareItemRules() const { return rareRules_; }

  // A map load (FieldItems::loadMap), before its head script runs. On a new load with the rare
  // items on, the map's spots roll (rollRareItems) and the hits lie at `height`, the height the
  // player stands at, as the script's items do. An item lies only while the bag holds none of it
  // and none lies on the ground: once used it can come again. With them off, nothing is drawn.
  void loadMap(std::string_view mapId, std::uint32_t load, float height, ruleset::Random& random);

  // A chest a script places (map_object.c: spawnChest): taken already if its trigger is set.
  // `mapId` is the map the player is on; false when there is no such item or no room.
  bool spawnChest(std::string_view mapId, std::int32_t originalItem, glm::vec3 position,
                  std::int32_t rotation, std::int32_t trigger);

  // Picks up a ground item into the bag (item.c: pickupItem).
  TakeOutcome pickUp(std::size_t slot);

  // Takes a chest's item into the bag and sets its trigger (tamer.c: tamerTickTakeChest).
  TakeOutcome takeChest(std::size_t chest);

  // An item a script lays on a cell of the map the player is on (item.c: spawnItem), by the
  // original's item number; false when there is no such item or no room on the ground. A spot
  // where a rare item lies keeps it: the script's item does not come (true).
  bool spawnItem(std::string_view mapId, std::int32_t originalItem, assets::CollisionCell cell,
                 float height);

  // An item a beaten enemy left where it stood on the map the player is on (item.c:
  // spawnDroppedItems), by the item's id, on a free cell of the map's `collision`
  // (game::field::dropCell); false when there is no such item or no room on the ground.
  bool dropItem(std::string_view mapId, std::string_view item, glm::vec3 position,
                const assets::MapCollision& collision);

private:
  [[nodiscard]] std::optional<assets::ItemHandle> itemOf(std::int32_t originalItem) const;

  InventoryAdapter* inventory_;
  StoryAdapter* story_;
  // Whether an item may lie as a rare item: the bag holds none and none lies on the ground.
  [[nodiscard]] bool rareItemEligible(assets::ItemHandle item) const;

  game::field::FieldItems items_{{}};
  std::optional<FieldItemsRules> rules_;
  std::optional<RareItemRules> rareRules_;
  bool rareItemsOn_ = false;
  // The spots of this load where a rare item lies.
  std::vector<assets::CollisionCell> rareCells_;
};

} // namespace dwe::session
