#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <algorithm>
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
constexpr std::string_view FeedKey = "feed";
constexpr std::string_view FeedSoundKey = "sound";
constexpr std::string_view RangeBaseKey = "range_base";
constexpr std::string_view RangePerDistanceKey = "range_per_distance";
constexpr std::string_view TakeDistanceKey = "take_distance";
constexpr std::string_view DefaultKey = "default";
constexpr std::string_view SpeciesKey = "species";
constexpr std::string_view OfferFrameKey = "offer_frame";
constexpr std::string_view HandDownFrameKey = "hand_down_frame";
constexpr std::string_view ClipsKey = "clips";
constexpr std::string_view PlayerOfferKey = "player_offer";
constexpr std::string_view PlayerRefusedKey = "player_refused";
constexpr std::string_view PartnerIdleKey = "partner_idle";
constexpr std::string_view PartnerEatKey = "partner_eat";
constexpr std::string_view PartnerRefuseKey = "partner_refuse";
constexpr std::string_view ToiletWalkKey = "toilet_walk";
constexpr std::string_view CameraFramesKey = "camera_frames";
constexpr std::string_view PartnerClipKey = "partner_clip";
constexpr std::string_view PraiseScoldKey = "praise_scold";
constexpr std::string_view PraiseKey = "praise";
constexpr std::string_view ScoldKey = "scold";
constexpr std::string_view PlayerClipKey = "player_clip";
constexpr std::string_view TallPlayerClipKey = "tall_player_clip";
constexpr std::string_view BoostedPartnerClipKey = "boosted_partner_clip";
constexpr std::string_view TallFromLevelKey = "tall_from_level";
constexpr std::string_view ShortSpeciesKey = "short_species";
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

FeedClips feedClips(YamlReader& reader, const YamlNode& root) {
  const YamlNode& node = reader.field(root, ClipsKey);
  if (!reader.expectKeys(
          node, ClipsKey,
          {PlayerOfferKey, PlayerRefusedKey, PartnerIdleKey, PartnerEatKey, PartnerRefuseKey})) {
    return {};
  }
  const auto clip = [&](std::string_view key) {
    return reader.integer<std::uint32_t>(reader.field(node, key));
  };
  return {.playerOffer = clip(PlayerOfferKey),
          .playerRefused = clip(PlayerRefusedKey),
          .partnerIdle = clip(PartnerIdleKey),
          .partnerEat = clip(PartnerEatKey),
          .partnerRefuse = clip(PartnerRefuseKey)};
}

std::optional<FeedRules> feedRules(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = root.find(FeedKey);
  if (node == nullptr) {
    return std::nullopt;
  }
  if (!reader.expectKeys(*node, FeedKey,
                         {FeedSoundKey, RangeBaseKey, RangePerDistanceKey, TakeDistanceKey,
                          OfferFrameKey, HandDownFrameKey, ClipsKey})) {
    return std::nullopt;
  }
  const YamlNode& take = reader.field(*node, TakeDistanceKey);
  if (!reader.expectKeys(take, TakeDistanceKey, {DefaultKey, SpeciesKey})) {
    return std::nullopt;
  }
  FeedRules rules{.sound = soundCue(reader, *node, FeedSoundKey),
                  .rangeBase = positive(reader, *node, RangeBaseKey),
                  .rangePerDistance = positive(reader, *node, RangePerDistanceKey),
                  .defaultDistance = positive(reader, take, DefaultKey),
                  .distances = {},
                  .offerFrame = positive(reader, *node, OfferFrameKey),
                  .handDownFrame = positive(reader, *node, HandDownFrameKey),
                  .clips = feedClips(reader, *node)};
  const YamlNode& species = reader.field(take, SpeciesKey);
  if (reader.ok() && !species.isMapping()) {
    reader.fail(species, core::formatMessage(field_items_messages::NotMapping, SpeciesKey));
    return rules;
  }
  for (const core::YamlEntry& entry : species.entries()) {
    rules.distances.emplace(entry.key, positive(reader, species, entry.key));
  }
  return rules;
}

std::optional<ToiletWalkRules> toiletWalkRules(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = root.find(ToiletWalkKey);
  if (node == nullptr ||
      !reader.expectKeys(*node, ToiletWalkKey, {CameraFramesKey, PartnerClipKey})) {
    return std::nullopt;
  }
  return ToiletWalkRules{.cameraFrames = positive(reader, *node, CameraFramesKey),
                         .partnerClip =
                             reader.integer<std::uint32_t>(reader.field(*node, PartnerClipKey))};
}

std::optional<PraiseScoldRules> praiseScoldRules(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = root.find(PraiseScoldKey);
  if (node == nullptr ||
      !reader.expectKeys(*node, PraiseScoldKey,
                         {PraiseKey, ScoldKey, TallFromLevelKey, ShortSpeciesKey})) {
    return std::nullopt;
  }
  const YamlNode& praise = reader.field(*node, PraiseKey);
  const YamlNode& scold = reader.field(*node, ScoldKey);
  if (!reader.expectKeys(praise, PraiseKey, {PlayerClipKey, TallPlayerClipKey, PartnerClipKey}) ||
      !reader.expectKeys(scold, ScoldKey, {PlayerClipKey, PartnerClipKey, BoostedPartnerClipKey})) {
    return std::nullopt;
  }
  const auto clip = [&](const YamlNode& parent, std::string_view key) {
    return reader.integer<std::uint32_t>(reader.field(parent, key));
  };
  PraiseScoldRules rules{.praisePlayerClip = clip(praise, PlayerClipKey),
                         .praiseTallPlayerClip = clip(praise, TallPlayerClipKey),
                         .praisePartnerClip = clip(praise, PartnerClipKey),
                         .scoldPlayerClip = clip(scold, PlayerClipKey),
                         .scoldPartnerClip = clip(scold, PartnerClipKey),
                         .boostedScoldPartnerClip = clip(scold, BoostedPartnerClipKey),
                         .tallFromLevel = positive(reader, *node, TallFromLevelKey),
                         .shortSpecies = {}};
  for (const YamlNode& species :
       reader.sequence(reader.field(*node, ShortSpeciesKey), ShortSpeciesKey)) {
    rules.shortSpecies.push_back(reader.text(species));
  }
  return rules;
}

} // namespace

std::uint32_t PraiseScoldRules::praisePlayerClipFor(std::int32_t level,
                                                    std::string_view species) const {
  // tamer.c: tamerTickPraiseScold: the player reaches up to a partner of champion level or higher,
  // save the short ones.
  const bool tall = level >= tallFromLevel && !std::ranges::contains(shortSpecies, species);
  return tall ? praiseTallPlayerClip : praisePlayerClip;
}

float FeedRules::rangeOf(std::string_view species) const {
  const auto found = distances.find(species);
  const std::int32_t distance = found == distances.end() ? defaultDistance : found->second;
  return static_cast<float>(rangeBase + (rangePerDistance * distance));
}

core::Result<FieldItemsRules> readFieldItemRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, field_items_messages::SectionText,
                         {GroundSlotsKey, ChestSlotsKey, ReachCellsKey, PickupSoundKey,
                          ToiletSoundKey, SleepSoundKey, PraiseSoundKey, ScoldSoundKey},
                         {FeedKey, ToiletWalkKey, PraiseScoldKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  const FieldItemsRules rules{.items = {.groundSlots = positive(reader, root, GroundSlotsKey),
                                        .chestSlots = positive(reader, root, ChestSlotsKey),
                                        .reachCells = positive(reader, root, ReachCellsKey)},
                              .pickupSound = soundCue(reader, root, PickupSoundKey),
                              .toiletSound = soundCue(reader, root, ToiletSoundKey),
                              .sleepSound = soundCue(reader, root, SleepSoundKey),
                              .praiseSound = soundCue(reader, root, PraiseSoundKey),
                              .scoldSound = soundCue(reader, root, ScoldSoundKey),
                              .feed = feedRules(reader, root),
                              .toiletWalk = toiletWalkRules(reader, root),
                              .praiseScold = praiseScoldRules(reader, root)};
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

bool FieldItemsAdapter::dropItem(std::string_view mapId, std::string_view item,
                                 glm::vec3 position) {
  items_.enterMap(mapId);
  const std::optional<assets::ItemHandle> handle = inventory_->items().handleOf(item);
  if (!handle) {
    core::logWarning("{}", core::formatMessage(field_items_messages::UnknownDrop, item));
    return false;
  }
  core::logDebug("[port:field_items] spawnDroppedItems (item.c:2403): {} at ({}, {})", item,
                 position.x, position.z);
  return items_.drop(*handle, position);
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
