#pragma once

#include <dwe/assets/fishing_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/fishing/fishing.hpp>
#include <dwe/game/fishing/rules.hpp>
#include <dwe/game/inventory/stacking.hpp>
#include <dwe/session/session_system.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::session {

class InventoryAdapter;
class StoryAdapter;

// Whether the pause menu offers fishing (main/fish.c: getFishingAvailability).
enum class FishingAvailability : std::uint8_t {
  // Not a fishing map, or fishing is off on it.
  None,
  // A fishing map, but no rod or no food to bait it with: the entry shows, greyed.
  Unready,
  Ready,
};

// The bait menu's rows (fish.c: FISH_openBaitMenu): one per bag slot holding food (hasFoodItem's
// sorting value 2), in the bag's order and with that slot's count, so a second stack of an item is
// a second row. Measured: a bag of Sirloin 3, a non-food item, Meat 5, Sirloin 2 and Giant Meat 4
// lists Sirloin 3, Meat 5, Sirloin 2, Giant Meat 4.
[[nodiscard]] std::vector<game::inventory::Stack>
baitRows(std::span<const std::optional<game::inventory::Stack>> slots,
         const assets::ItemTable& items);

// What the player got from one tick's events, for the screen's messages.
struct FishingCatch {
  enum class Kind : std::uint8_t {
    Fish,
    // Something that got stuck on the hook.
    Snag,
  };
  Kind kind = Kind::Fish;
  assets::ItemHandle item;
  // Fish only: its kind and size in centimetres, and whether it beat the kind's record.
  std::int32_t fishKind = 0;
  std::int32_t size = 0;
  bool newRecord = false;
  // False when the bag had no room and the player let it go.
  bool kept = false;
};

// The fishing of the fishing maps (main/fish.c, the FISH overlay): the `fishing` rules,
// data/fishing and the rod triggers. It owns the water of the map the player is on; the game's
// screen ticks it and hands the events back to apply(). Without the table or the rules no map has
// fishing.
class FishingAdapter final : public SessionSystem {
public:
  // The inventory and the story must outlive the adapter.
  FishingAdapter(InventoryAdapter& inventory, StoryAdapter& story,
                 std::optional<assets::FishingTable> table);

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;

  // The player entered a map (checkFishingMap): a fishing map gets its water, any other none.
  void enterMap(std::string_view mapId);
  // The water of the current map, or nullptr off a fishing map.
  [[nodiscard]] game::fishing::Fishing* fishing() { return fishing_ ? &*fishing_ : nullptr; }
  [[nodiscard]] const game::fishing::Fishing* fishing() const {
    return fishing_ ? &*fishing_ : nullptr;
  }
  // The data of the current map's fishing (its image and scale), or nullptr off a fishing map.
  [[nodiscard]] const assets::FishingMapEntry* map() const { return fishing_ ? map_ : nullptr; }

  [[nodiscard]] FishingAvailability availability() const;
  // main/fish.c: deinitializeFishing: the partner's natural death (partnerTickDying) takes the
  // water of the map away, fish, sea serpent, ripples and marks, until a map loads again
  // (enterMap). Measured on mayo06: FISHING_DATA_PTR is NULL from the first dying frame and the
  // serpent's lines leave the lake.
  void leaveWater();
  // main/fish.c: setFishingDisabled and setFishingEnabled: the partner's evolution and death scenes
  // stop the fishing of the map while they run and give it back at their end; nothing off a
  // fishing map.
  void setFishingDisabled();
  void setFishingEnabled();
  // The best rod the player has (getBestFishingRod).
  [[nodiscard]] std::optional<game::fishing::Rod> bestRod() const;
  // The food in the bag, which baits the hook: the bait menu's rows (baitRows).
  [[nodiscard]] std::vector<game::inventory::Stack> baits() const;
  // The item number Fishing::chooseBait takes for a bag item.
  [[nodiscard]] std::int32_t itemNumber(assets::ItemHandle item) const;
  // The bag item of an item number (Fishing::baitItem), or nothing.
  [[nodiscard]] std::optional<assets::ItemHandle> itemOfNumber(std::int32_t number) const;

  // Applies a tick's events: an eaten bait leaves the bag, a catch goes in it (FISH_tickRod), a
  // landed fish counts for the partner and the records, and the sea serpent stops fishing on the
  // map (tamerTickFishing). The catches come back for the screen's messages.
  std::vector<FishingCatch> apply(GameSession& session, const game::fishing::FishingOutput& out);

  // The longest fish of a kind landed this run (FISH_RECORD_SIZE, which is not saved).
  [[nodiscard]] std::int32_t record(std::int32_t fishKind) const;
  // The sound slot of the fishing's sounds (FishingEventKind::Sound).
  [[nodiscard]] std::int32_t soundSlot() const { return rules_ ? rules_->sounds.slot : 0; }

private:
  [[nodiscard]] std::optional<std::int32_t> numberOf(std::string_view id) const;
  [[nodiscard]] std::optional<game::fishing::FishingTables> resolveTables() const;
  [[nodiscard]] std::optional<game::fishing::FishingPlace>
  resolvePlace(const assets::FishingMapEntry& map) const;
  std::optional<FishingCatch> give(const game::fishing::FishingEvent& event);

  InventoryAdapter* inventory_;
  StoryAdapter* story_;
  std::optional<assets::FishingTable> table_;
  std::optional<game::fishing::FishingRules> rules_;
  std::optional<game::fishing::Fishing> fishing_;
  const assets::FishingMapEntry* map_ = nullptr;
  std::array<std::int32_t, 8> records_{};
};

} // namespace dwe::session
