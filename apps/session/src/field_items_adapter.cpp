#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <string>

#include "field_items_messages.hpp"

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view GroundSlotsKey = "ground_slots";
constexpr std::string_view ChestSlotsKey = "chest_slots";
constexpr std::string_view ReachCellsKey = "reach_cells";
constexpr std::string_view PickupSoundKey = "pickup_sound";
constexpr std::string_view ToiletSoundKey = "toilet_sound";
constexpr std::string_view SleepSoundKey = "sleep_sound";
constexpr std::string_view PraiseSoundKey = "praise_sound";
constexpr std::string_view ScoldSoundKey = "scold_sound";
constexpr std::string_view SlotKey = "slot";
constexpr std::string_view SoundKey = "sound";

std::int32_t positive(YamlReader& reader, const YamlNode& root, std::string_view key) {
  const YamlNode& node = reader.field(root, key);
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && value <= 0) {
    reader.fail(node, core::formatMessage(field_items_messages::NotPositive, key));
  }
  return value;
}

SoundCue soundCue(YamlReader& reader, const YamlNode& root, std::string_view key) {
  const YamlNode& node = reader.field(root, key);
  if (!reader.expectKeys(node, key, {SlotKey, SoundKey})) {
    return {};
  }
  return {.slot = reader.integer<std::int64_t>(reader.field(node, SlotKey)),
          .sound = reader.integer<std::int64_t>(reader.field(node, SoundKey))};
}

} // namespace

core::Result<FieldItemsRules> readFieldItemRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, field_items_messages::SectionText,
                         {GroundSlotsKey, ChestSlotsKey, ReachCellsKey, PickupSoundKey,
                          ToiletSoundKey, SleepSoundKey, PraiseSoundKey, ScoldSoundKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  const FieldItemsRules rules{.items = {.groundSlots = positive(reader, root, GroundSlotsKey),
                                        .chestSlots = positive(reader, root, ChestSlotsKey),
                                        .reachCells = positive(reader, root, ReachCellsKey)},
                              .pickupSound = soundCue(reader, root, PickupSoundKey),
                              .toiletSound = soundCue(reader, root, ToiletSoundKey),
                              .sleepSound = soundCue(reader, root, SleepSoundKey),
                              .praiseSound = soundCue(reader, root, PraiseSoundKey),
                              .scoldSound = soundCue(reader, root, ScoldSoundKey)};
  return reader.finish(rules, section.file);
}

FieldItemsAdapter::FieldItemsAdapter(InventoryAdapter& inventory, StoryAdapter& story)
    : inventory_{&inventory}, story_{&story} {}

ruleset::SectionChecks FieldItemsAdapter::sectionChecks() const {
  return {
      {std::string{FieldItemsSection}, [](const ruleset::Section& section) -> core::Result<void> {
         auto rules = readFieldItemRules(section);
         if (!rules) {
           return std::unexpected{rules.error()};
         }
         return {};
       }}};
}

core::Result<void> FieldItemsAdapter::readRules(const ruleset::Ruleset& rules) {
  const ruleset::Section* section = rules.section(FieldItemsSection);
  if (section == nullptr) {
    core::logDebug("[port:field_items] the ruleset has no field_items section: no ground items");
    items_.setRules({});
    rules_.reset();
    return {};
  }
  auto read = readFieldItemRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  items_.setRules(read->items);
  rules_ = *read;
  return {};
}

std::optional<assets::ItemHandle> FieldItemsAdapter::itemOf(std::int32_t originalItem) const {
  const assets::ItemEntry* entry = inventory_->items().findByOriginalIndex(originalItem);
  if (entry == nullptr) {
    core::logWarning("{}", core::formatMessage(field_items_messages::UnknownItem, originalItem));
    return std::nullopt;
  }
  return inventory_->items().handleOf(*entry);
}

bool FieldItemsAdapter::spawnItem(std::string_view mapId, std::int32_t originalItem,
                                  assets::CollisionCell cell, float height) {
  items_.enterMap(mapId);
  const std::optional<assets::ItemHandle> item = itemOf(originalItem);
  return item && items_.spawn(*item, cell, height);
}

bool FieldItemsAdapter::spawnChest(std::string_view mapId, std::int32_t originalItem,
                                   glm::vec3 position, std::int32_t rotation,
                                   std::int32_t trigger) {
  items_.enterMap(mapId);
  const std::optional<assets::ItemHandle> item = itemOf(originalItem);
  if (!item) {
    return false;
  }
  const bool taken = story_->state().trigger(trigger).value_or(false);
  return items_.spawnChest({.item = *item,
                            .position = position,
                            .rotation = rotation,
                            .trigger = trigger,
                            .taken = taken});
}

TakeOutcome FieldItemsAdapter::pickUp(std::size_t slot) {
  const auto ground = items_.ground();
  if (slot >= ground.size()) {
    return TakeOutcome::Empty;
  }
  const auto& onGround = ground[slot];
  if (!onGround) {
    return TakeOutcome::Empty;
  }
  game::inventory::Inventory& bag = inventory_->inventory();
  const assets::ItemHandle item = onGround->item;
  // tamerTickPickupItem: giveItem(type, 0) only asks whether the bag has room.
  if (!bag.hasRoomFor(item)) {
    core::logDebug("[port:field_items] tamerTickPickupItem (tamer.c:1450): bag full, slot {}",
                   slot);
    return TakeOutcome::BagFull;
  }
  bag.add(item, 1);
  items_.remove(slot);
  core::logDebug("[port:field_items] pickupItem (item.c:2639): '{}' into the bag",
                 inventory_->items().at(item).id);
  return TakeOutcome::Taken;
}

TakeOutcome FieldItemsAdapter::takeChest(std::size_t chest) {
  const auto chests = items_.chests();
  const auto* placed = chest < chests.size() ? &chests[chest] : nullptr;
  if (placed == nullptr || !placed->has_value() || (*placed)->taken) {
    core::logDebug("[port:field_items] tamerTickTakeChest (tamer.c:1520): chest {} empty", chest);
    return TakeOutcome::Empty;
  }
  const game::field::Chest& found = **placed;
  game::inventory::Inventory& bag = inventory_->inventory();
  const assets::ItemHandle item = found.item;
  if (!bag.hasRoomFor(item)) {
    core::logDebug("[port:field_items] tamerTickTakeChest (tamer.c:1557): bag full, chest {}",
                   chest);
    return TakeOutcome::BagFull;
  }
  const std::int32_t trigger = found.trigger;
  if (auto set = story_->state().setTrigger(trigger, true); !set) {
    core::logWarning("{}", core::formatMessage(field_items_messages::TriggerFailed, trigger,
                                               set.error().message));
  }
  items_.markTaken(chest);
  bag.add(item, 1);
  core::logDebug("[port:field_items] tamerTickTakeChest (tamer.c:1565): '{}' into the bag, "
                 "trigger {} set",
                 inventory_->items().at(item).id, trigger);
  return TakeOutcome::Taken;
}

} // namespace dwe::session
