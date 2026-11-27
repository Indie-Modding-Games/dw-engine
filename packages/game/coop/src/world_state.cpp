#include <dwe/game/coop/world_state.hpp>

#include <algorithm>

namespace dwe::game::coop {

namespace {

bool mapEdit(const WorldEdit& edit) {
  return edit.kind == WorldEditKind::MapObject || edit.kind == WorldEditKind::MapBlock;
}

bool fact(const WorldEdit& edit) {
  return edit.kind == WorldEditKind::Trigger || edit.kind == WorldEditKind::Variable;
}

} // namespace

std::string_view askVerdictName(AskVerdict verdict) {
  switch (verdict) {
  case AskVerdict::Accepted:
    return "accepted";
  case AskVerdict::UnknownPlayer:
    return "unknown player";
  case AskVerdict::Stale:
    return "stale";
  case AskVerdict::WrongMap:
    return "wrong map";
  case AskVerdict::Nothing:
    return "nothing shared";
  case AskVerdict::Overtaken:
    return "overtaken";
  }
  return "unknown";
}

std::vector<std::vector<WorldEdit>> worldEditBatches(std::span<const WorldEdit> edits) {
  std::vector<std::vector<WorldEdit>> batches;
  for (std::size_t at = 0; at < edits.size(); at += MaxWorldEdits) {
    const std::size_t count = std::min(MaxWorldEdits, edits.size() - at);
    batches.emplace_back(edits.begin() + static_cast<std::ptrdiff_t>(at),
                         edits.begin() + static_cast<std::ptrdiff_t>(at + count));
  }
  return batches;
}

std::vector<WorldChange> WorldLedger::record(std::string_view mapId,
                                             std::span<const WorldEdit> edits) {
  std::vector<WorldEdit> kept;
  std::ranges::copy_if(edits, std::back_inserter(kept),
                       [this](const WorldEdit& edit) { return shared(edit); });
  std::vector<WorldChange> changes;
  for (std::vector<WorldEdit>& batch : worldEditBatches(kept)) {
    ++revision_;
    for (const WorldEdit& edit : batch) {
      changed(edit, {.revision = revision_, .by = HostPlayerId});
    }
    changes.push_back({.revision = revision_,
                       .origin = HostPlayerId,
                       .ask = 0,
                       .mapId = std::string{mapId},
                       .edits = std::move(batch)});
  }
  return changes;
}

WorldLedger::Taken WorldLedger::accept(PlayerId from, const WorldAsk& ask,
                                       std::string_view worldMap, bool known,
                                       const std::function<bool(const WorldEdit&)>& changes) {
  Taken taken;
  if (!known || !validGuest(from)) {
    taken.verdict = AskVerdict::UnknownPlayer;
    return taken;
  }
  const auto last = asks_.find(from);
  if (last != asks_.end() && ask.ask <= last->second) {
    taken.verdict = AskVerdict::Stale;
    return taken;
  }
  asks_[from] = ask.ask;
  const bool onMap = ask.mapId == worldMap;
  // Made on a load of this map the host has since replaced (a script's warp reset the room): what
  // the guest's scripts did there is gone with it.
  const bool oldLoad = onMap && ask.seen < mapLoad_;
  std::vector<WorldEdit> kept;
  bool wrongMap = false;
  bool overtook = false;
  for (const WorldEdit& edit : ask.edits) {
    if (!shared(edit)) {
      taken.refused.push_back(edit);
    } else if (mapEdit(edit) && !onMap) {
      // A door or an item of a map the session left: nothing of it is in front of anyone now.
      wrongMap = true;
    } else if (mapEdit(edit) && oldLoad) {
      overtook = true;
    } else if (oldLoad || overtaken(edit, from, ask.seen)) {
      taken.overtaken.push_back(edit);
      overtook = true;
    } else if (!changes || changes(edit)) {
      kept.push_back(edit);
    }
  }
  if (kept.empty()) {
    taken.verdict = overtook   ? AskVerdict::Overtaken
                    : wrongMap ? AskVerdict::WrongMap
                               : AskVerdict::Nothing;
    return taken;
  }
  ++revision_;
  for (const WorldEdit& edit : kept) {
    changed(edit, {.revision = revision_, .by = from});
  }
  taken.verdict = AskVerdict::Accepted;
  taken.change = WorldChange{.revision = revision_,
                             .origin = from,
                             .ask = ask.ask,
                             .mapId = ask.mapId,
                             .edits = std::move(kept)};
  return taken;
}

void WorldLedger::changed(const WorldEdit& edit, Changed at) {
  if (fact(edit)) {
    changedAt_[{edit.kind, edit.key}] = at;
  }
}

bool WorldLedger::overtaken(const WorldEdit& edit, PlayerId from, std::uint32_t seen) const {
  if (!fact(edit)) {
    return false;
  }
  const auto found = changedAt_.find({edit.kind, edit.key});
  return found != changedAt_.end() && found->second.revision > seen && found->second.by != from;
}

bool WorldReplica::take(const WorldChange& change) {
  if (change.revision <= revision_) {
    return false;
  }
  revision_ = change.revision;
  return true;
}

bool WorldReplica::take(const WorldSync& sync) {
  if (sync.revision < revision_) {
    return false;
  }
  if (sync.part + 1 == sync.parts) {
    revision_ = sync.revision;
  }
  return true;
}

std::vector<WorldAsk> WorldReplica::ask(std::string_view mapId, std::span<const WorldEdit> edits) {
  std::vector<WorldAsk> asks;
  for (std::vector<WorldEdit>& batch : worldEditBatches(edits)) {
    asks.push_back({.ask = ++asks_,
                    .seen = revision_,
                    .mapId = std::string{mapId},
                    .edits = std::move(batch)});
  }
  return asks;
}

std::vector<WorldSync> worldSyncParts(std::uint32_t revision, const std::vector<bool>& triggers,
                                      std::span<const std::int32_t> variables) {
  const std::size_t byTriggers = (triggers.size() + MaxSyncTriggers - 1) / MaxSyncTriggers;
  const std::size_t byVariables = (variables.size() + MaxSyncVariables - 1) / MaxSyncVariables;
  const std::size_t parts = std::max<std::size_t>({1, byTriggers, byVariables});
  std::vector<WorldSync> out;
  for (std::size_t part = 0; part < parts; ++part) {
    WorldSync sync{.revision = revision,
                   .part = static_cast<std::uint16_t>(part),
                   .parts = static_cast<std::uint16_t>(parts),
                   .firstTrigger = static_cast<std::int32_t>(part * MaxSyncTriggers),
                   .triggers = {},
                   .firstVariable = static_cast<std::int32_t>(part * MaxSyncVariables),
                   .variables = {}};
    for (std::size_t i = part * MaxSyncTriggers;
         i < triggers.size() && i < (part + 1) * MaxSyncTriggers; ++i) {
      sync.triggers.push_back(triggers[i]);
    }
    for (std::size_t i = part * MaxSyncVariables;
         i < variables.size() && i < (part + 1) * MaxSyncVariables; ++i) {
      sync.variables.push_back(variables[i]);
    }
    out.push_back(std::move(sync));
  }
  return out;
}

} // namespace dwe::game::coop
