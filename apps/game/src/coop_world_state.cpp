#include "coop_world_state.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>
#include <variant>

#include "coop_world.hpp"

namespace dwe::app {

namespace {

namespace coop = game::coop;

bool isFact(const coop::WorldEdit& edit) {
  return edit.kind == coop::WorldEditKind::Trigger || edit.kind == coop::WorldEditKind::Variable;
}

bool isMapEdit(const coop::WorldEdit& edit) {
  return edit.kind == coop::WorldEditKind::MapObject || edit.kind == coop::WorldEditKind::MapBlock;
}

bool sharedBy(const coop::WorldScope& shared, const coop::WorldEdit& edit) {
  return !shared || shared(edit);
}

std::int32_t wireCoordinate(float value) {
  const float bounded =
      std::clamp(std::isfinite(value) ? value : 0.0F, -static_cast<float>(coop::MaxWorldCoordinate),
                 static_cast<float>(coop::MaxWorldCoordinate));
  return static_cast<std::int32_t>(std::lround(bounded));
}

// The ground's items as the wire names them (ids still 0).
std::vector<coop::GroundEntry> groundEntries(const CoopWorldView& view) {
  std::vector<coop::GroundEntry> entries;
  if (view.items == nullptr || view.itemTable == nullptr) {
    return entries;
  }
  const auto ground = view.items->ground();
  for (std::size_t slot = 0; slot < ground.size() && entries.size() < coop::MaxGroundEntries;
       ++slot) {
    const auto& item = ground[slot];
    if (!item) {
      continue;
    }
    const std::string& id = view.itemTable->at(item->item).id;
    if (id.empty() || id.size() > coop::MaxGroundItemId) {
      continue;
    }
    entries.push_back({.id = 0,
                       .slot = static_cast<std::uint8_t>(slot),
                       .item = id,
                       .column = static_cast<std::int16_t>(item->cell.column),
                       .row = static_cast<std::int16_t>(item->cell.row),
                       .x = wireCoordinate(item->position.x),
                       .y = wireCoordinate(item->position.y),
                       .z = wireCoordinate(item->position.z)});
  }
  return entries;
}

bool samePlace(const coop::GroundEntry& a, const coop::GroundEntry& b) {
  return a.slot == b.slot && a.item == b.item && a.column == b.column && a.row == b.row &&
         a.x == b.x && a.y == b.y && a.z == b.z;
}

std::vector<CoopOutgoing> toEveryGuest(const coop::WorldAuthority& authority,
                                       const coop::Message& message) {
  std::vector<CoopOutgoing> out;
  for (const coop::PlayerRecord& record : authority.players()) {
    if (record.id != coop::HostPlayerId) {
      out.push_back({.to = record.id, .message = message});
    }
  }
  return out;
}

void append(std::vector<CoopOutgoing>& to, std::vector<CoopOutgoing> more) {
  to.insert(to.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
}

std::int16_t wireTile(std::int32_t value) {
  return static_cast<std::int16_t>(std::clamp<std::int32_t>(
      value, std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::max()));
}

} // namespace

coop::PoopPiles pileList(const game::partner::PoopPiles& piles, std::string_view map) {
  coop::PoopPiles list{.mapId = std::string{map}, .restart = false, .piles = {}};
  for (const game::partner::PoopPile& pile : piles.onMap(map)) {
    if (list.piles.size() == coop::MaxMapPiles) {
      break;
    }
    list.piles.push_back(
        {.x = wireTile(pile.tile.x), .y = wireTile(pile.tile.y), .size = pile.size});
  }
  return list;
}

void applyPileList(game::partner::PoopPiles& piles, const coop::PoopPiles& list) {
  if (list.restart) {
    piles.restore({}, piles.cursor());
  }
  if (list.mapId.empty()) {
    return;
  }
  std::vector<game::partner::PoopPile> host;
  for (const coop::PileEntry& entry : list.piles) {
    host.push_back({.map = list.mapId, .tile = {.x = entry.x, .y = entry.y}, .size = entry.size});
  }
  piles.replaceMap(list.mapId, host);
}

std::optional<coop::PoopPiles> CoopPileTracker::update(const game::partner::PoopPiles& piles,
                                                       std::string_view map) {
  if (map.empty()) {
    return std::nullopt;
  }
  coop::PoopPiles list = pileList(piles, map);
  const auto sent = sent_.find(map);
  const bool same = sent != sent_.end() ? sent->second == list.piles : list.piles.empty();
  if (same) {
    return std::nullopt;
  }
  sent_[std::string{map}] = list.piles;
  return list;
}

std::vector<coop::PoopPiles> CoopPileTracker::sync(const game::partner::PoopPiles& piles,
                                                   std::string_view hostMap) {
  std::vector<coop::PoopPiles> lists;
  coop::PoopPiles first = pileList(piles, hostMap);
  first.restart = true;
  lists.push_back(std::move(first));
  std::vector<std::string_view> maps;
  for (const game::partner::PoopPile& pile : piles.slots()) {
    if (pile.size != 0 && pile.map != hostMap && !pile.map.empty() &&
        pile.map.size() <= coop::MaxPileMapText && !std::ranges::contains(maps, pile.map)) {
      maps.emplace_back(pile.map);
    }
  }
  for (const std::string_view map : maps) {
    lists.push_back(pileList(piles, map));
  }
  return lists;
}

void CoopStoryBaseline::reset(const game::story::StoryState& state) {
  triggers_.assign(static_cast<std::size_t>(state.triggerCount()), false);
  for (std::int32_t i = 0; i < state.triggerCount(); ++i) {
    triggers_[static_cast<std::size_t>(i)] = state.trigger(i).value_or(false);
  }
  variables_.assign(static_cast<std::size_t>(state.variableCount()), 0);
  for (std::int32_t i = 0; i < state.variableCount(); ++i) {
    variables_[static_cast<std::size_t>(i)] = state.variable(i).value_or(0);
  }
  ready_ = true;
}

std::vector<coop::WorldEdit> CoopStoryBaseline::changes(const game::story::StoryState& state,
                                                        const coop::WorldScope& shared,
                                                        std::vector<CoopFactChange>* changed) {
  std::vector<coop::WorldEdit> edits;
  if (!ready_ || std::cmp_not_equal(triggers_.size(), state.triggerCount()) ||
      std::cmp_not_equal(variables_.size(), state.variableCount())) {
    // A new capacity (rules read again): what it holds now is the agreed state.
    reset(state);
    return edits;
  }
  for (std::int32_t i = 0; i < state.triggerCount(); ++i) {
    const bool now = state.trigger(i).value_or(false);
    if (now == triggers_[static_cast<std::size_t>(i)]) {
      continue;
    }
    triggers_[static_cast<std::size_t>(i)] = now;
    const coop::WorldEdit edit{
        .kind = coop::WorldEditKind::Trigger, .key = i, .value = now ? 1 : 0};
    if (sharedBy(shared, edit)) {
      edits.push_back(edit);
      if (changed != nullptr) {
        changed->push_back({.edit = edit, .previous = now ? 0 : 1});
      }
    }
  }
  for (std::int32_t i = 0; i < state.variableCount(); ++i) {
    const std::int32_t now = state.variable(i).value_or(0);
    const std::int32_t was = variables_[static_cast<std::size_t>(i)];
    if (now == was) {
      continue;
    }
    variables_[static_cast<std::size_t>(i)] = now;
    const coop::WorldEdit edit{.kind = coop::WorldEditKind::Variable, .key = i, .value = now};
    if (sharedBy(shared, edit)) {
      edits.push_back(edit);
      if (changed != nullptr) {
        changed->push_back({.edit = edit, .previous = was});
      }
    }
  }
  return edits;
}

bool CoopStoryBaseline::differs(const game::story::StoryState& state, const coop::WorldEdit& edit) {
  const auto now = current(state, edit);
  return now && now->value != edit.value;
}

std::optional<coop::WorldEdit> CoopStoryBaseline::current(const game::story::StoryState& state,
                                                          const coop::WorldEdit& edit) {
  if (edit.kind == coop::WorldEditKind::Trigger) {
    if (const auto set = state.trigger(edit.key)) {
      return coop::WorldEdit{.kind = edit.kind, .key = edit.key, .value = *set ? 1 : 0};
    }
  } else if (edit.kind == coop::WorldEditKind::Variable) {
    if (const auto value = state.variable(edit.key)) {
      return coop::WorldEdit{.kind = edit.kind, .key = edit.key, .value = *value};
    }
  }
  return std::nullopt;
}

bool CoopStoryBaseline::apply(game::story::StoryState& state, const coop::WorldEdit& edit) {
  if (!differs(state, edit)) {
    return false;
  }
  const auto key = static_cast<std::size_t>(edit.key);
  if (edit.kind == coop::WorldEditKind::Trigger) {
    if (!state.setTrigger(edit.key, edit.value != 0)) {
      return false;
    }
    if (key < triggers_.size()) {
      triggers_[key] = edit.value != 0;
    }
  } else {
    if (!state.setVariable(edit.key, edit.value)) {
      return false;
    }
    if (key < variables_.size()) {
      variables_[key] = edit.value;
    }
  }
  return true;
}

bool CoopStoryBaseline::apply(game::story::StoryState& state, const coop::WorldEdit& edit,
                              std::vector<CoopFactChange>& changed) {
  const auto before = current(state, edit);
  if (!before || !apply(state, edit)) {
    return false;
  }
  changed.push_back({.edit = edit, .previous = before->value});
  return true;
}

std::vector<coop::WorldSync> storySync(std::uint32_t revision,
                                       const game::story::StoryState& state) {
  std::vector<bool> triggers(static_cast<std::size_t>(state.triggerCount()));
  for (std::int32_t i = 0; i < state.triggerCount(); ++i) {
    triggers[static_cast<std::size_t>(i)] = state.trigger(i).value_or(false);
  }
  std::vector<std::int32_t> variables(static_cast<std::size_t>(state.variableCount()));
  for (std::int32_t i = 0; i < state.variableCount(); ++i) {
    variables[static_cast<std::size_t>(i)] = state.variable(i).value_or(0);
  }
  return coop::worldSyncParts(revision, triggers, variables);
}

void applyStorySync(game::story::StoryState& state, CoopStoryBaseline& baseline,
                    const coop::WorldSync& sync, const coop::WorldScope& shared,
                    std::vector<CoopFactChange>& changed) {
  const auto applyShared = [&](const coop::WorldEdit& edit) {
    if (sharedBy(shared, edit)) {
      (void)baseline.apply(state, edit, changed);
    }
  };
  for (std::size_t i = 0; i < sync.triggers.size(); ++i) {
    applyShared({.kind = coop::WorldEditKind::Trigger,
                 .key = sync.firstTrigger + static_cast<std::int32_t>(i),
                 .value = sync.triggers[i] ? 1 : 0});
  }
  for (std::size_t i = 0; i < sync.variables.size(); ++i) {
    applyShared({.kind = coop::WorldEditKind::Variable,
                 .key = sync.firstVariable + static_cast<std::int32_t>(i),
                 .value = sync.variables[i]});
  }
}

std::size_t followChestTriggers(game::field::FieldItems& items,
                                const game::story::StoryState& state) {
  std::size_t changed = 0;
  const auto chests = items.chests();
  for (std::size_t slot = 0; slot < chests.size(); ++slot) {
    if (chests[slot] && !chests[slot]->taken &&
        state.trigger(chests[slot]->trigger).value_or(false) && items.takenElsewhere(slot)) {
      core::logDebug("coop: chest {} taken by another player (trigger {})", slot,
                     chests[slot]->trigger);
      ++changed;
    }
  }
  return changed;
}

std::optional<coop::GroundItems> CoopGroundTracker::update(const CoopWorldView& view) {
  if (view.mapId.empty() || !view.settled) {
    return std::nullopt;
  }
  std::vector<coop::GroundEntry> entries = groundEntries(view);
  const bool sameLoad = last_ && last_->mapId == view.mapId && last_->mapSerial == view.mapSerial;
  for (coop::GroundEntry& entry : entries) {
    const coop::GroundEntry* before = nullptr;
    if (sameLoad) {
      const auto found = std::ranges::find(last_->items, entry.slot, &coop::GroundEntry::slot);
      if (found != last_->items.end() && samePlace(*found, entry)) {
        before = &*found;
      }
    }
    entry.id = before != nullptr ? before->id : nextId_++;
  }
  if (sameLoad && entries == last_->items) {
    return std::nullopt;
  }
  last_ = coop::GroundItems{.mapId = view.mapId, .mapSerial = view.mapSerial, .items = entries};
  return last_;
}

std::optional<std::uint32_t> CoopGroundTracker::idAt(std::size_t slot) const {
  if (!last_) {
    return std::nullopt;
  }
  const auto found = std::ranges::find(last_->items, slot, [](const coop::GroundEntry& entry) {
    return static_cast<std::size_t>(entry.slot);
  });
  return found != last_->items.end() ? std::optional{found->id} : std::nullopt;
}

std::optional<std::uint32_t> CoopGroundMirror::idAt(const CoopWorldView& view,
                                                    std::size_t slot) const {
  if (!shownSerial_ || *shownSerial_ != view.mapSerial || slot >= shown_.size() ||
      shown_[slot] == 0) {
    return std::nullopt;
  }
  return shown_[slot];
}

void CoopGroundMirror::show(CoopWorldView& view) {
  if (view.items == nullptr || view.itemTable == nullptr || !host_ || !view.settled ||
      host_->mapId != view.mapId) {
    // Not on the host's map (or between maps): the ground is this game's own.
    shown_.clear();
    shownSerial_.reset();
    return;
  }
  std::vector<std::optional<game::field::GroundItem>> ground;
  std::vector<std::uint32_t> shown;
  for (const coop::GroundEntry& entry : host_->items) {
    const auto handle = view.itemTable->handleOf(entry.item);
    if (!handle) {
      continue;
    }
    if (ground.size() <= entry.slot) {
      ground.resize(entry.slot + 1U);
      shown.resize(entry.slot + 1U, 0);
    }
    ground[entry.slot] = game::field::GroundItem{.item = *handle,
                                                 .cell = {.column = entry.column, .row = entry.row},
                                                 .position = {static_cast<float>(entry.x),
                                                              static_cast<float>(entry.y),
                                                              static_cast<float>(entry.z)}};
    shown[entry.slot] = entry.id;
  }
  const auto now = view.items->ground();
  if (ground.size() < now.size()) {
    ground.resize(now.size());
  }
  const bool same = std::ranges::equal(
      now, ground,
      [](const std::optional<game::field::GroundItem>& a,
         const std::optional<game::field::GroundItem>& b) {
        return a.has_value() == b.has_value() &&
               (!a || (a->item == b->item && a->cell == b->cell && a->position == b->position));
      });
  if (!same) {
    view.items->replaceGround(ground);
  }
  shown_ = std::move(shown);
  shownSerial_ = view.mapSerial;
}

bool CoopHostWorld::grant(CoopWorldView& view, const coop::ItemClaim& claim, CoopWorldShare& share,
                          bool& chestsMayChange) {
  if (claim.kind == coop::ClaimKind::Chest) {
    // Taken: its trigger set, here and then on every game (the claimer's item notice gives it).
    const coop::WorldEdit taken{.kind = coop::WorldEditKind::Trigger, .key = claim.key, .value = 1};
    if (!sharedBy(view.shared, taken) || !CoopStoryBaseline::differs(*view.story, taken) ||
        !facts_.apply(*view.story, taken, share.facts)) {
      return false;
    }
    chestsMayChange = true;
    for (const coop::WorldChange& change : ledger_.record(view.mapId, std::span{&taken, 1})) {
      append(share.out, toEveryGuest(authority_, change));
    }
    return true;
  }
  // Off the host's ground, for the next claim too: one taker per item.
  const auto slot = static_cast<std::size_t>(claim.key);
  const auto id = ground_.idAt(slot);
  if (view.items == nullptr || !view.settled || claim.mapId != view.mapId || !id ||
      *id != claim.id) {
    return false;
  }
  view.items->remove(slot);
  if (auto ground = ground_.update(view)) {
    append(share.out, toEveryGuest(authority_, *ground));
  }
  return true;
}

CoopWorldShare CoopHostWorld::share(CoopWorldView& view) {
  CoopWorldShare share;
  if (view.story == nullptr) {
    return share;
  }
  game::story::StoryState& story = *view.story;
  ledger_.setScope(view.shared);
  if (!facts_.ready()) {
    facts_.reset(story);
  }
  // A new load, a script's warp reset included, overtakes what the guest did on the last load.
  const bool newMap = view.settled && syncSerial_ != view.mapSerial;
  if (newMap) {
    syncSerial_ = view.mapSerial;
    synced_.clear();
    ledger_.newMapLoad();
  }
  // The host's own interactions: its game is the world, so they only need telling.
  std::vector<coop::WorldEdit> own = facts_.changes(story, view.shared, &share.ownFacts);
  if (view.settled) {
    own.insert(own.end(), view.mapEdits.begin(), view.mapEdits.end());
  }
  for (const coop::WorldChange& change : ledger_.record(view.mapId, own)) {
    append(share.out, toEveryGuest(authority_, change));
  }

  // The host's own pick-ups and partner piles land before any claim is judged.
  if (auto ground = ground_.update(view)) {
    append(share.out, toEveryGuest(authority_, *ground));
  }
  if (view.piles != nullptr) {
    if (auto piles = piles_.update(*view.piles, view.mapId)) {
      append(share.out, toEveryGuest(authority_, *piles));
    }
  }

  // The guests' asks, in the order they came.
  bool chestsMayChange = false;
  for (auto& [from, ask] : std::exchange(asks_, {})) {
    const auto changes = [&](const coop::WorldEdit& edit) {
      return !isFact(edit) || CoopStoryBaseline::differs(story, edit);
    };
    coop::WorldLedger::Taken taken =
        ledger_.accept(from, ask, view.settled ? std::string_view{view.mapId} : std::string_view{},
                       authority_.find(from) != nullptr, changes);
    if (taken.verdict != coop::AskVerdict::Accepted) {
      core::logDebug("coop: player {}'s world ask {} not taken: {}", from, ask.ask,
                     coop::askVerdictName(taken.verdict));
    }
    if (taken.change) {
      for (const coop::WorldEdit& edit : taken.change->edits) {
        if (isFact(edit)) {
          chestsMayChange |= facts_.apply(story, edit, share.facts);
        } else if (isMapEdit(edit)) {
          share.mapEdits.push_back(edit);
        }
      }
      core::logInfo("coop: player {} changed the shared world ({} edits, revision {})", from,
                    taken.change->edits.size(), taken.change->revision);
      append(share.out, toEveryGuest(authority_, *taken.change));
    }
    if (!taken.overtaken.empty()) {
      // The guest's game holds values the world replaced since: it gets the world's again.
      std::vector<coop::WorldEdit> world;
      for (const coop::WorldEdit& edit : taken.overtaken) {
        if (auto now = CoopStoryBaseline::current(story, edit)) {
          world.push_back(*now);
        }
      }
      core::logInfo("coop: player {}'s world ask {}: {} edits overtaken by newer changes", from,
                    ask.ask, taken.overtaken.size());
      for (const coop::WorldChange& change : ledger_.record(view.mapId, world)) {
        append(share.out, toEveryGuest(authority_, change));
      }
    }
    if (!taken.refused.empty()) {
      // A player's own facts never reach the world (the guest's game keeps them to itself).
      core::logDebug("coop: player {}'s world ask {}: {} edits of its own left out", from, ask.ask,
                     taken.refused.size());
    }
  }

  // The guests' claims, in the order they came: the first to claim an item still there gets it.
  for (auto& [from, claim] : std::exchange(claims_, {})) {
    if (authority_.find(from) == nullptr) {
      continue;
    }
    auto& last = lastClaim_[from];
    if (claim.claim <= last) {
      continue;
    }
    last = claim.claim;
    const bool granted = grant(view, claim, share, chestsMayChange);
    core::logInfo("coop: player {}'s claim {} on {} {} {}", from, claim.claim,
                  claim.kind == coop::ClaimKind::Chest ? "chest" : "ground slot", claim.key,
                  granted ? "granted" : "refused: taken first");
    share.out.push_back(
        {.to = from, .message = coop::ClaimAnswer{.claim = claim.claim, .granted = granted}});
  }
  if (chestsMayChange && view.items != nullptr) {
    (void)followChestTriggers(*view.items, story);
  }
  takePileAsks(view, share);

  // The whole story to a guest that just joined, and to everyone on a new map.
  for (const coop::PlayerRecord& record : authority_.players()) {
    if (record.id == coop::HostPlayerId || synced_.contains(record.id)) {
      continue;
    }
    synced_.insert(record.id);
    for (coop::WorldSync& part : storySync(ledger_.revision(), story)) {
      share.out.push_back({.to = record.id, .message = std::move(part)});
    }
    if (ground_.last() && !newMap) {
      // Joined on this map: what lies here now (a new map sends it below anyway).
      share.out.push_back({.to = record.id, .message = *ground_.last()});
    }
    if (view.piles != nullptr) {
      for (coop::PoopPiles& list : CoopPileTracker::sync(*view.piles, view.mapId)) {
        share.out.push_back({.to = record.id, .message = std::move(list)});
      }
    }
  }
  if (auto ground = ground_.update(view)) {
    append(share.out, toEveryGuest(authority_, *ground));
  }
  return share;
}

void CoopHostWorld::takePileAsks(CoopWorldView& view, CoopWorldShare& share) {
  for (auto& [from, ask] : std::exchange(pileAsks_, {})) {
    if (authority_.find(from) == nullptr) {
      continue;
    }
    auto& last = lastPileAsk_[from];
    if (ask.ask <= last) {
      continue;
    }
    last = ask.ask;
    const game::partner::Tile tile{.x = ask.x, .y = ask.y};
    if (ask.action == coop::PileAction::Leave) {
      if (view.piles != nullptr && view.random != nullptr) {
        const game::partner::Tile placed =
            view.piles->placeAt(ask.mapId, tile, ask.size, *view.random);
        core::logInfo("coop: player {}'s partner left a pile on {} at ({}, {})", from, ask.mapId,
                      placed.x, placed.y);
      }
    } else {
      const std::optional<std::int32_t> size =
          view.piles != nullptr ? view.piles->remove(ask.mapId, tile) : std::nullopt;
      core::logInfo("coop: player {}'s partner eats the pile on {} at ({}, {}): {}", from,
                    ask.mapId, tile.x, tile.y, size ? "granted" : "refused: gone");
      share.out.push_back({.to = from,
                           .message = coop::PileAnswer{.ask = ask.ask,
                                                       .granted = size.has_value(),
                                                       .size = size.value_or(0)}});
    }
    if (view.piles != nullptr) {
      if (auto piles = piles_.update(*view.piles, ask.mapId)) {
        append(share.out, toEveryGuest(authority_, *piles));
      }
    }
  }
}

void CoopGuestWorld::sharePiles(CoopWorldView& view, CoopWorldShare& share) {
  if (view.pileShare != nullptr) {
    FieldPileShare& mine = *view.pileShare;
    for (const game::partner::PoopPile& pile : std::exchange(mine.left, {})) {
      share.out.push_back({.to = coop::HostPlayerId,
                           .message = coop::PileAsk{.ask = ++pileAsks_,
                                                    .action = coop::PileAction::Leave,
                                                    .mapId = pile.map,
                                                    .x = wireTile(pile.tile.x),
                                                    .y = wireTile(pile.tile.y),
                                                    .size = pile.size}});
    }
    if (mine.eat.state == FieldPileShare::Eat::State::Asked) {
      mine.eat.number = ++pileAsks_;
      mine.eat.state = FieldPileShare::Eat::State::Sent;
      share.out.push_back({.to = coop::HostPlayerId,
                           .message = coop::PileAsk{.ask = mine.eat.number,
                                                    .action = coop::PileAction::Eat,
                                                    .mapId = mine.eat.mapId,
                                                    .x = wireTile(mine.eat.tile.x),
                                                    .y = wireTile(mine.eat.tile.y),
                                                    .size = 0}});
    }
  }
  for (const coop::PoopPiles& list : std::exchange(pilesIn_, {})) {
    if (view.piles != nullptr) {
      applyPileList(*view.piles, list);
    }
  }
  for (const coop::PileAnswer& answer : std::exchange(pileAnswers_, {})) {
    if (view.pileShare == nullptr) {
      continue;
    }
    FieldPileShare::Eat& eat = view.pileShare->eat;
    if (eat.state == FieldPileShare::Eat::State::Sent && eat.number == answer.ask) {
      eat.state = answer.granted ? FieldPileShare::Eat::State::Granted
                                 : FieldPileShare::Eat::State::Refused;
      eat.size = answer.size;
    }
  }
}

CoopWorldShare CoopGuestWorld::share(CoopWorldView& view) {
  CoopWorldShare share;
  if (view.story == nullptr) {
    return share;
  }
  game::story::StoryState& story = *view.story;
  if (!facts_.ready()) {
    facts_.reset(story);
  }
  // What this game changed: its own news, asked of the host.
  std::vector<coop::WorldEdit> mine = facts_.changes(story, view.shared, &share.ownFacts);
  if (view.settled) {
    mine.insert(mine.end(), view.mapEdits.begin(), view.mapEdits.end());
  }
  if (!view.mapId.empty()) {
    for (coop::WorldAsk& ask : replica_.ask(view.mapId, mine)) {
      share.out.push_back({.to = coop::HostPlayerId, .message = std::move(ask)});
    }
  }
  if (view.claim != nullptr && view.claim->state == FieldItemClaim::State::Asked) {
    claim(view, share);
  }

  // The host's changes, each once and in order.
  bool chestsMayChange = false;
  for (const coop::Message& message : std::exchange(worldIn_, {})) {
    if (const auto* change = std::get_if<coop::WorldChange>(&message)) {
      if (!replica_.take(*change)) {
        continue;
      }
      for (const coop::WorldEdit& edit : change->edits) {
        if (isFact(edit)) {
          if (sharedBy(view.shared, edit)) {
            chestsMayChange |= facts_.apply(story, edit, share.facts);
          }
        } else if (isMapEdit(edit) && change->origin != self_ && change->mapId == view.mapId) {
          if (view.settled) {
            share.mapEdits.push_back(edit);
          } else {
            // This game is loading that map again: the edits wait for its load to end.
            heldMap_ = view.mapId;
            heldEdits_.push_back(edit);
          }
        }
      }
    } else if (const auto* sync = std::get_if<coop::WorldSync>(&message)) {
      if (replica_.take(*sync)) {
        applyStorySync(story, facts_, *sync, view.shared, share.facts);
        chestsMayChange = true;
        if (sync->part + 1 == sync->parts) {
          core::logInfo("coop: the shared world is at the host's revision {}", sync->revision);
        }
      }
    }
  }
  if (view.settled && !heldEdits_.empty()) {
    if (heldMap_ == view.mapId) {
      share.mapEdits.insert(share.mapEdits.begin(), heldEdits_.begin(), heldEdits_.end());
    }
    heldEdits_.clear();
  }
  if (chestsMayChange && view.items != nullptr) {
    (void)followChestTriggers(*view.items, story);
  }
  ground_.show(view);
  sharePiles(view, share);
  // The host's word on this game's claim, once the changes it made are in.
  for (const coop::ClaimAnswer& answer : std::exchange(answers_, {})) {
    if (view.claim != nullptr && view.claim->state == FieldItemClaim::State::Sent &&
        view.claim->number == answer.claim) {
      view.claim->state =
          answer.granted ? FieldItemClaim::State::Granted : FieldItemClaim::State::Refused;
    }
  }
  return share;
}

void CoopGuestWorld::claim(const CoopWorldView& view, CoopWorldShare& share) {
  FieldItemClaim& asked = *view.claim;
  coop::ItemClaim claim{.claim = 0,
                        .kind = asked.chest ? coop::ClaimKind::Chest : coop::ClaimKind::GroundItem,
                        .key = static_cast<std::int32_t>(asked.index),
                        .id = 0,
                        .mapId = view.mapId};
  if (asked.chest) {
    const auto chests = view.items != nullptr
                            ? view.items->chests()
                            : std::span<const std::optional<game::field::Chest>>{};
    if (asked.index >= chests.size() || !chests[asked.index] || view.mapId.empty()) {
      asked.state = FieldItemClaim::State::Refused;
      return;
    }
    claim.key = chests[asked.index]->trigger;
  } else if (const auto id = ground_.idAt(view, asked.index); id && !view.mapId.empty()) {
    claim.id = *id;
  } else {
    // This game's own ground (off the host's map): no one else sees the item to take it.
    asked.state = FieldItemClaim::State::Granted;
    return;
  }
  claim.claim = ++claims_;
  asked.number = claim.claim;
  asked.state = FieldItemClaim::State::Sent;
  share.out.push_back({.to = coop::HostPlayerId, .message = std::move(claim)});
}

} // namespace dwe::app
