#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/session/fishing_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <algorithm>
#include <string>
#include <utility>

#include "fishing_messages.hpp"

namespace dwe::session {

namespace {

using game::fishing::FishingEvent;
using game::fishing::FishingEventKind;
using game::fishing::Rod;

// main/fish.c: getBestFishingRod.
constexpr std::int32_t AmazingRodTrigger = 46;
constexpr std::int32_t OldRodTrigger = 45;
// hasFoodItem: the food's sorting value.
constexpr std::int32_t FoodSorting = 2;
constexpr char ShoreByY = 'y';

} // namespace

FishingAdapter::FishingAdapter(InventoryAdapter& inventory, StoryAdapter& story,
                               std::optional<assets::FishingTable> table)
    : inventory_{&inventory}, story_{&story}, table_{std::move(table)} {}

ruleset::SectionChecks FishingAdapter::sectionChecks() const {
  return {{std::string{game::fishing::FishingSectionName},
           [](const ruleset::Section& section) -> core::Result<void> {
             auto rules = game::fishing::parseFishingRules(section);
             if (!rules) {
               return std::unexpected{rules.error()};
             }
             return {};
           }}};
}

core::Result<void> FishingAdapter::readRules(const ruleset::Ruleset& rules) {
  const ruleset::Section* section = rules.section(game::fishing::FishingSectionName);
  if (section == nullptr || !table_) {
    core::logDebug("{}", fishing_messages::NoFishing);
    rules_.reset();
    fishing_.reset();
    return {};
  }
  auto read = game::fishing::parseFishingRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = *read;
  // The water of the map comes back with the new rules on the next map.
  fishing_.reset();
  return {};
}

std::optional<assets::ItemHandle> FishingAdapter::itemOfNumber(std::int32_t number) const {
  const assets::ItemTable& items = inventory_->items();
  const auto entries = items.entries();
  const auto found = std::ranges::find(entries, number, &assets::ItemEntry::index);
  if (found == entries.end()) {
    return std::nullopt;
  }
  return items.handleOf(*found);
}

std::optional<std::int32_t> FishingAdapter::numberOf(std::string_view id) const {
  if (id.empty()) {
    return std::nullopt;
  }
  const assets::ItemEntry* entry = inventory_->items().find(id);
  if (entry == nullptr) {
    core::logWarning("{}", core::formatMessage(fishing_messages::UnknownItem, id));
    return std::nullopt;
  }
  return entry->index;
}

std::optional<game::fishing::FishingTables> FishingAdapter::resolveTables() const {
  if (!table_ || table_->kinds.empty()) {
    core::logWarning("{}", fishing_messages::NoKinds);
    return std::nullopt;
  }
  game::fishing::FishingTables tables{.kinds = {},
                                      .seaSerpent = {},
                                      .firstBait = table_->firstBait,
                                      .biteChances = table_->biteChances,
                                      .junk = {}};
  for (const assets::FishingKindEntry& kind : table_->kinds) {
    // The kinds give their item by its original number (FISH_tickRod: kind + 0x3E).
    const assets::ItemEntry* item = inventory_->items().findByOriginalIndex(kind.item);
    if (item == nullptr) {
      core::logWarning("{}", core::formatMessage(fishing_messages::UnknownItemNumber, kind.item));
    }
    tables.kinds.push_back({.item = item != nullptr ? item->index : kind.item,
                            .size = {.min = kind.sizeMin, .max = kind.sizeMax},
                            .moveDelay = {.min = kind.moveDelayMin, .max = kind.moveDelayMax},
                            .attractedBy = numberOf(kind.bait)});
  }
  tables.seaSerpent = tables.kinds.back();
  tables.kinds.pop_back();
  for (const assets::FishingJunk& junk : table_->junk) {
    if (const std::optional<std::int32_t> item = numberOf(junk.item)) {
      tables.junk.push_back({.item = *item, .upTo = junk.threshold});
    }
  }
  return tables;
}

std::optional<game::fishing::FishingPlace>
FishingAdapter::resolvePlace(const assets::FishingMapEntry& map) const {
  if (!table_ || !map.shoreline || *map.shoreline >= table_->shorelines.size()) {
    core::logWarning("{}", core::formatMessage(fishing_messages::NoShoreline, map.map));
    return std::nullopt;
  }
  const assets::FishingShoreline& line = table_->shorelines[*map.shoreline];
  game::fishing::FishingPlace place{.castRange = map.castRange,
                                    .splash = map.splash,
                                    .seaSerpentTimes = map.seaSerpentTimes,
                                    .shoreline = {.axis = line.axis == ShoreByY
                                                              ? game::fishing::Shoreline::Axis::Y
                                                              : game::fishing::Shoreline::Axis::X,
                                                  .points = line.points},
                                    .spots = {},
                                    .seaSerpentPath = map.seaSerpentPath};
  for (const assets::FishingSpotEntry& spot : map.spots) {
    place.spots.push_back({.weight = spot.weight, .points = spot.points});
  }
  return place;
}

void FishingAdapter::enterMap(std::string_view mapId) {
  fishing_.reset();
  if (!rules_ || !table_) {
    return;
  }
  const assets::FishingMapEntry* map = table_->findMap(mapId);
  if (map == nullptr) {
    return;
  }
  std::optional<game::fishing::FishingTables> tables = resolveTables();
  std::optional<game::fishing::FishingPlace> place = resolvePlace(*map);
  if (!tables || !place) {
    return;
  }
  fishing_.emplace(*rules_, std::move(*tables), std::move(*place));
  map_ = map;
  core::logDebug("[port:fishing] checkFishingMap (main/fish.c:116): water on '{}'", mapId);
}

std::optional<Rod> FishingAdapter::bestRod() const {
  const auto held = [this](std::int32_t trigger) {
    const core::Result<bool> set = story_->state().trigger(trigger);
    if (!set) {
      core::logWarning("{}", core::formatMessage(fishing_messages::RodTriggerFailed, trigger,
                                                 set.error().message));
      return false;
    }
    return *set;
  };
  if (held(AmazingRodTrigger)) {
    return Rod::Amazing;
  }
  if (held(OldRodTrigger)) {
    return Rod::Old;
  }
  return std::nullopt;
}

std::vector<assets::ItemHandle> FishingAdapter::baits() const {
  std::vector<assets::ItemHandle> food;
  for (const assets::ItemEntry& entry : inventory_->items().entries()) {
    if (entry.sortingValue != FoodSorting) {
      continue;
    }
    const assets::ItemHandle item = inventory_->items().handleOf(entry);
    if (inventory_->inventory().countOf(item) > 0) {
      food.push_back(item);
    }
  }
  return food;
}

std::int32_t FishingAdapter::itemNumber(assets::ItemHandle item) const {
  return inventory_->items().at(item).index;
}

FishingAvailability FishingAdapter::availability() const {
  if (!fishing_ || fishing_->disabled()) {
    return FishingAvailability::None;
  }
  if (!bestRod() || baits().empty()) {
    return FishingAvailability::Unready;
  }
  return FishingAvailability::Ready;
}

std::optional<FishingCatch> FishingAdapter::give(const FishingEvent& event) {
  const std::optional<assets::ItemHandle> item = itemOfNumber(event.value);
  if (!item) {
    core::logWarning("{}", core::formatMessage(fishing_messages::UnknownItemNumber, event.value));
    return std::nullopt;
  }
  game::inventory::Inventory& bag = inventory_->inventory();
  FishingCatch caught{.kind = event.kind == FishingEventKind::FishCaught ? FishingCatch::Kind::Fish
                                                                         : FishingCatch::Kind::Snag,
                      .item = *item,
                      .fishKind = event.fishKind,
                      .size = event.size,
                      .newRecord = false,
                      .kept = bag.hasRoomFor(*item)};
  if (caught.kept) {
    bag.add(*item, 1);
  }
  return caught;
}

std::vector<FishingCatch> FishingAdapter::apply(GameSession& session,
                                                const game::fishing::FishingOutput& out) {
  std::vector<FishingCatch> catches;
  for (const FishingEvent& event : out.events) {
    switch (event.kind) {
    case FishingEventKind::BaitEaten: {
      // FISH_tickHookedFish: removeItem of the bait's slot.
      const std::optional<assets::ItemHandle> bait = itemOfNumber(event.value);
      if (!bait || inventory_->inventory().countOf(*bait) == 0) {
        core::logWarning("{}", core::formatMessage(fishing_messages::BaitNotHeld, event.value));
        break;
      }
      inventory_->inventory().remove(*bait, 1);
      core::logDebug("[port:fishing] FISH_tickHookedFish (fish.c:3216): bait {} eaten",
                     event.value);
      break;
    }
    case FishingEventKind::FishCaught: {
      std::optional<FishingCatch> caught = give(event);
      if (!caught) {
        break;
      }
      // FISH_tickRod (fish.c:4293): the kind's record, then the partner's count.
      if (event.fishKind >= 0 && std::cmp_less(event.fishKind, records_.size())) {
        auto& record = records_[static_cast<std::size_t>(event.fishKind)];
        caught->newRecord = event.size > record;
        record = std::max(record, event.size);
      }
      if (session.playing()) {
        session.partner().countFishCaught();
      }
      core::logDebug("[port:fishing] FISH_tickRod (fish.c:4297): fish {} of {} cm, kept {}",
                     event.value, event.size, caught->kept);
      catches.push_back(*caught);
      break;
    }
    case FishingEventKind::JunkCaught:
      if (std::optional<FishingCatch> caught = give(event)) {
        core::logDebug("[port:fishing] FISH_tickRod (fish.c:4379): item {} stuck, kept {}",
                       event.value, caught->kept);
        catches.push_back(*caught);
      }
      break;
    case FishingEventKind::SeaSerpentLanded:
      // tamerTickFishing: fishing stops on the map and the scene runs (the screen's part).
      if (fishing_) {
        fishing_->setDisabled(true);
      }
      core::logDebug("[port:fishing] tamerTickFishing (main/fish.c:90): the sea serpent landed");
      break;
    default:
      break;
    }
  }
  return catches;
}

std::int32_t FishingAdapter::record(std::int32_t fishKind) const {
  if (fishKind < 0 || std::cmp_greater_equal(fishKind, records_.size())) {
    return 0;
  }
  return records_[static_cast<std::size_t>(fishKind)];
}

} // namespace dwe::session
