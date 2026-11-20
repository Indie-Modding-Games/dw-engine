#include <dwe/game/coop/authority.hpp>
#include <dwe/game/coop/sync_watch.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace dwe::game::coop {

SequenceWatch::Arrival SequenceWatch::observe(std::uint32_t number) {
  ++received_;
  if (!newest_) {
    newest_ = number;
    return Arrival::First;
  }
  if (number == *newest_) {
    ++repeats_;
    return Arrival::Repeat;
  }
  if (!sequenceNewer(number, *newest_)) {
    // It was counted lost when the gap opened: it only came late.
    ++late_;
    if (lost_ > 0) {
      --lost_;
    }
    return Arrival::Late;
  }
  const std::uint32_t gap = number - *newest_ - 1;
  newest_ = number;
  lost_ += gap;
  return gap == 0 ? Arrival::InOrder : Arrival::AfterGap;
}

namespace {

std::string placeText(const std::string& mapId, std::int32_t x, std::int32_t y, std::int32_t z) {
  return std::format("{} ({}, {}, {})", mapId.empty() ? std::string{"-"} : mapId, x, y, z);
}

float distanceOf(const SyncHeld& a, const SyncHeld& b) {
  const auto dx = static_cast<float>(a.x - b.x);
  const auto dy = static_cast<float>(a.y - b.y);
  const auto dz = static_cast<float>(a.z - b.z);
  return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

} // namespace

void SyncMonitor::authority(std::vector<SyncHeld> players, std::uint32_t worldTick,
                            net::Millis now) {
  for (const SyncHeld& player : players) {
    const auto known = maps_.find(player.id);
    if (known != maps_.end() && known->second != player.mapId) {
      events_.push_back(
          std::format("map: player {} went from {} to {}", player.id, known->second, player.mapId));
    }
    maps_[player.id] = player.mapId;
  }
  std::erase_if(maps_, [&](const auto& entry) {
    return std::ranges::none_of(players, [&](const SyncHeld& p) { return p.id == entry.first; });
  });
  authority_ = std::move(players);
  worldTick_ = worldTick;
  for (const auto& [guest, state] : guests_) {
    if (state.newest) {
      compare(guest, now);
    }
  }
}

void SyncMonitor::report(PlayerId guest, const SyncReport& report, net::Millis now) {
  Guest& state = guests_[guest];
  if (const auto arrival = state.reports.observe(report.sequence);
      arrival == SequenceWatch::Arrival::Late || arrival == SequenceWatch::Arrival::Repeat) {
    return;
  }
  if (report.snapshotsLost > state.snapshotsLost) {
    events_.push_back(std::format("lost: player {} missed {} more snapshots ({} in all)", guest,
                                  report.snapshotsLost - state.snapshotsLost,
                                  report.snapshotsLost));
  }
  if (report.snapshotsLate > state.snapshotsLate) {
    events_.push_back(std::format("late: player {} got {} more snapshots out of order ({} in all)",
                                  guest, report.snapshotsLate - state.snapshotsLate,
                                  report.snapshotsLate));
  }
  state.snapshotsLost = report.snapshotsLost;
  state.snapshotsLate = report.snapshotsLate;
  for (const SyncHeld& held : report.players) {
    const auto known = state.heldMaps.find(held.id);
    if (known != state.heldMaps.end() && known->second != held.mapId) {
      events_.push_back(std::format("map: player {} now holds player {} on {} (was {})", guest,
                                    held.id, held.mapId, known->second));
    }
    state.heldMaps[held.id] = held.mapId;
  }
  state.newest = report;
  state.reportedAt = now;
  compare(guest, now);
}

void SyncMonitor::states(PlayerId guest, const SequenceWatch& watch) {
  Guest& state = guests_[guest];
  if (watch.lost() > state.statesLost) {
    events_.push_back(std::format("lost: the host missed {} more states of player {} ({} in all)",
                                  watch.lost() - state.statesLost, guest, watch.lost()));
  }
  if (watch.late() > state.statesLate) {
    events_.push_back(std::format("late: the host got {} more states of player {} out of order",
                                  watch.late() - state.statesLate, guest));
  }
  state.statesLost = watch.lost();
  state.statesLate = watch.late();
}

void SyncMonitor::forget(PlayerId guest) {
  guests_.erase(guest);
  std::erase_if(divergences_, [&](const auto& entry) { return entry.first.first == guest; });
}

void SyncMonitor::compare(PlayerId guest, net::Millis now) {
  const SyncReport& report = *guests_.at(guest).newest;
  for (const SyncHeld& held : report.players) {
    const auto authority = std::ranges::find(authority_, held.id, &SyncHeld::id);
    std::optional<SyncDivergence> found;
    if (authority == authority_.end()) {
      found = SyncDivergence{.guest = guest,
                             .player = held.id,
                             .heldMap = held.mapId,
                             .authorityMap = {},
                             .distance = -1.0F,
                             .since = now,
                             .persistent = false};
    } else if (authority->mapId != held.mapId) {
      found = SyncDivergence{.guest = guest,
                             .player = held.id,
                             .heldMap = held.mapId,
                             .authorityMap = authority->mapId,
                             .distance = -1.0F,
                             .since = now,
                             .persistent = false};
    } else if (const float distance = distanceOf(*authority, held); distance > rules_.tolerance) {
      found = SyncDivergence{.guest = guest,
                             .player = held.id,
                             .heldMap = held.mapId,
                             .authorityMap = authority->mapId,
                             .distance = distance,
                             .since = now,
                             .persistent = false};
    }
    const std::pair key{guest, held.id};
    const auto open = divergences_.find(key);
    if (!found) {
      if (open != divergences_.end()) {
        if (open->second.persistent) {
          events_.push_back(std::format(
              "in sync: player {} holds player {} where the host has it again", guest, held.id));
        }
        divergences_.erase(open);
      }
      continue;
    }
    if (open != divergences_.end()) {
      found->since = open->second.since;
      found->persistent = open->second.persistent;
    }
    if (!found->persistent && now - found->since >= rules_.persistAfter) {
      found->persistent = true;
      events_.push_back(std::format(
          "diverged: player {} holds player {} at {} for {} ms; the host has it on {}{}", guest,
          held.id, placeText(held.mapId, held.x, held.y, held.z), (now - found->since).count(),
          found->authorityMap.empty() ? std::string{"nothing (gone)"} : found->authorityMap,
          found->distance >= 0.0F ? std::format(", {:.0f} units away", found->distance)
                                  : std::string{}));
    }
    divergences_[key] = *found;
  }
  // A player the guest no longer holds is no longer diverging there.
  std::erase_if(divergences_, [&](const auto& entry) {
    return entry.first.first == guest &&
           std::ranges::none_of(
               report.players, [&](const SyncHeld& held) { return held.id == entry.first.second; });
  });
}

std::vector<std::string> SyncMonitor::takeEvents() {
  return std::exchange(events_, {});
}

std::vector<SyncDivergence> SyncMonitor::divergences() const {
  std::vector<SyncDivergence> out;
  for (const auto& [key, divergence] : divergences_) {
    out.push_back(divergence);
  }
  return out;
}

bool SyncMonitor::converged(net::Millis since) const {
  if (!divergences_.empty() || authority_.empty()) {
    return false;
  }
  return std::ranges::all_of(guests_, [&](const auto& entry) {
    const Guest& guest = entry.second;
    return guest.newest && guest.reportedAt >= since &&
           guest.newest->players.size() == authority_.size();
  });
}

std::vector<std::string> SyncMonitor::lines(net::Millis now) const {
  std::vector<std::string> out;
  out.push_back(std::format("host world tick {}, {} players, {} guests reporting", worldTick_,
                            authority_.size(), guests_.size()));
  for (const SyncHeld& player : authority_) {
    out.push_back(std::format("  player {}: {}", player.id,
                              placeText(player.mapId, player.x, player.y, player.z)));
  }
  for (const auto& [id, guest] : guests_) {
    if (!guest.newest) {
      out.push_back(std::format("guest {}: no report yet", id));
      continue;
    }
    const SyncReport& report = *guest.newest;
    out.push_back(std::format(
        "guest {}: report {} ms ago, snapshot tick {} ({} behind), snapshots lost {} late {}, "
        "states lost {} late {}",
        id, (now - guest.reportedAt).count(), report.snapshotTick,
        static_cast<std::int64_t>(worldTick_) - static_cast<std::int64_t>(report.snapshotTick),
        report.snapshotsLost, report.snapshotsLate, guest.statesLost, guest.statesLate));
    for (const SyncHeld& held : report.players) {
      const auto open = divergences_.find({id, held.id});
      out.push_back(std::format("    holds player {}: {}{}", held.id,
                                placeText(held.mapId, held.x, held.y, held.z),
                                open == divergences_.end() ? std::string{""}
                                : open->second.persistent  ? std::string{"  DIVERGED"}
                                                           : std::string{"  (off)"}));
    }
  }
  return out;
}

} // namespace dwe::game::coop
