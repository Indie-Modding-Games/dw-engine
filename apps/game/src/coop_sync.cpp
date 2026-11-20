#include "coop_sync.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <cmath>
#include <variant>

#include "coop_snapshot_interpolation.hpp"

namespace dwe::app {

namespace {

std::int32_t unitsOf(float value) {
  return static_cast<std::int32_t>(std::lround(value));
}

game::coop::SyncHeld heldOf(game::coop::PlayerId id, const std::string& mapId,
                            const glm::vec3& position) {
  return {.id = id,
          .mapId = mapId,
          .x = unitsOf(position.x),
          .y = unitsOf(position.y),
          .z = unitsOf(position.z)};
}

} // namespace

std::vector<game::coop::SyncHeld> coopSyncAuthority(const game::field::Field& field) {
  std::vector<game::coop::SyncHeld> players{
      heldOf(game::coop::HostPlayerId, field.mapId(), field.player().position)};
  for (game::coop::PlayerId id = game::coop::GuestPlayerId; game::coop::validPlayer(id); ++id) {
    if (const auto guest = field.player(id)) {
      players.push_back(heldOf(id, field.mapId(), guest->position));
    }
  }
  return players;
}

game::coop::SyncReport coopSyncReport(const game::field::Field& field,
                                      const PresentedCoopWorld* presented,
                                      game::coop::PlayerId self,
                                      const game::coop::SequenceWatch& snapshots,
                                      std::uint32_t sequence) {
  game::coop::SyncReport report{.sequence = sequence,
                                .snapshotTick = snapshots.newest().value_or(0),
                                .snapshotsLost = snapshots.lost(),
                                .snapshotsLate = snapshots.late(),
                                .players = {heldOf(self, field.mapId(), field.player().position)}};
  if (presented != nullptr) {
    for (const PresentedCoopEntity& entity : presented->entities) {
      if (entity.role == game::coop::EntityRole::Player && entity.owner != self &&
          std::ranges::none_of(report.players, [&](const game::coop::SyncHeld& held) {
            return held.id == entity.owner;
          })) {
        report.players.push_back(heldOf(entity.owner, presented->mapId, entity.position));
      }
    }
  }
  return report;
}

void CoopSync::ask(std::string_view value) {
  if (value == "off") {
    // The guests told to report hear it is off on the next tick.
    on_ = false;
    monitor_.reset();
    core::logInfo("coop sync: off");
    return;
  }
  if (!on_) {
    core::logInfo("coop sync: on (the guests report a couple of times a second)");
  }
  on_ = true;
  reportAsked_ = value != "on";
}

std::vector<std::string> CoopSync::hostTick(const game::field::Field& field,
                                            std::uint32_t worldTick,
                                            std::span<const CoopSyncPeer> guests, net::Millis now) {
  std::vector<std::string> out;
  // A guest that went is no longer told nor compared.
  for (auto told = told_.begin(); told != told_.end();) {
    if (std::ranges::none_of(guests,
                             [&](const CoopSyncPeer& guest) { return guest.id == *told; })) {
      if (monitor_) {
        monitor_->forget(*told);
      }
      told = told_.erase(told);
    } else {
      ++told;
    }
  }
  for (const CoopSyncPeer& guest : guests) {
    // The watch reaches each guest once (again for one that joins later), on and off alike.
    if (on_ != told_.contains(guest.id)) {
      if (guest.bridge->send(game::coop::SyncWatch{.on = on_})) {
        if (on_) {
          told_.insert(guest.id);
        } else {
          told_.erase(guest.id);
        }
      }
    }
    for (const game::coop::Message& message : guest.bridge->takeSyncMessages()) {
      if (const auto* report = std::get_if<game::coop::SyncReport>(&message);
          report != nullptr && on_) {
        if (!monitor_) {
          monitor_.emplace();
        }
        monitor_->report(guest.id, *report, now);
      }
    }
  }
  if (!on_) {
    return out;
  }
  if (!monitor_) {
    monitor_.emplace();
  }
  monitor_->authority(coopSyncAuthority(field), worldTick, now);
  for (const CoopSyncPeer& guest : guests) {
    monitor_->states(guest.id, guest.bridge->commandWatch());
  }
  for (std::string& event : monitor_->takeEvents()) {
    out.push_back(std::move(event));
  }
  ++hostTicks_;
  if (reportAsked_ || hostTicks_ % SummaryTicks == 0) {
    reportAsked_ = false;
    for (std::string& line : monitor_->lines(now)) {
      out.push_back(std::move(line));
    }
  }
  return out;
}

void CoopSync::guestTick(const game::field::Field& field, const PresentedCoopWorld* presented,
                         game::coop::PlayerId self, CoopGameplayBridge& host, std::uint64_t tick) {
  for (const game::coop::Message& message : host.takeSyncMessages()) {
    if (const auto* watch = std::get_if<game::coop::SyncWatch>(&message)) {
      if (watch->on != watched_) {
        core::logInfo("coop sync: the host {} watching this game's sync",
                      watch->on ? "started" : "stopped");
      }
      watched_ = watch->on;
    }
  }
  if (!watched_ || tick % ReportTicks != 0) {
    return;
  }
  const game::coop::SyncReport report =
      coopSyncReport(field, presented, self, host.snapshotWatch(), ++reports_);
  if (auto sent = host.send(report); !sent) {
    core::logWarning("coop sync: could not send the report: {}", sent.error().message);
  }
}

void CoopSync::reset() {
  told_.clear();
  monitor_.reset();
  watched_ = false;
  reports_ = 0;
  hostTicks_ = 0;
}

} // namespace dwe::app
