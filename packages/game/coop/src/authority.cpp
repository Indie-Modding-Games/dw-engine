#include <dwe/game/coop/authority.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

namespace dwe::game::coop {

namespace {

constexpr float YawUnitsToRadians = 6.2831853071795864769F / 65536.0F;
// Names and species longer than the roster carries are cut (protocol.cpp checks the same bound).
constexpr std::size_t MaxRosterText = 64;

std::string clipped(std::string text) {
  if (text.size() > MaxRosterText) {
    text.resize(MaxRosterText);
  }
  return text;
}

double groundDistance(const PlayerPose& a, const PlayerPose& b) {
  const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
  const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
  return std::sqrt((dx * dx) + (dz * dz));
}

float yawRadians(std::uint16_t units) {
  return static_cast<float>(units) * YawUnitsToRadians;
}

float groundDistance(const TrackedPose& a, const TrackedPose& b) {
  return std::hypot(a.x - b.x, a.z - b.z);
}

// The yaw `t` of the way from `from` to `to` (radians) the short way round, in [0, 2 pi).
float mixYaw(float from, float to, float t) {
  constexpr float Turn = 6.2831853071795864769F;
  const float yaw = std::fmod(from + (std::remainder(to - from, Turn) * t), Turn);
  return yaw < 0.0F ? yaw + Turn : yaw;
}

TrackedPose tracked(const PlayerPose& pose) {
  return {.x = static_cast<float>(pose.x),
          .y = static_cast<float>(pose.y),
          .z = static_cast<float>(pose.z),
          .yaw = yawRadians(pose.yaw),
          .motion = pose.motion};
}

PlayerCorrection correctionOf(const PlayerRecord& record, std::optional<std::int32_t> exit) {
  const PlayerPose& pose = *record.pose;
  return {.player = record.id,
          .epoch = record.epoch,
          .mapId = pose.mapId,
          .exit = exit,
          .x = pose.x,
          .y = pose.y,
          .z = pose.z,
          .yaw = pose.yaw};
}

} // namespace

std::string_view stateVerdictName(StateVerdict verdict) {
  switch (verdict) {
  case StateVerdict::Accepted:
    return "accepted";
  case StateVerdict::UnknownPlayer:
    return "unknown player";
  case StateVerdict::NotOwner:
    return "not its owner";
  case StateVerdict::Stale:
    return "stale";
  case StateVerdict::OldEpoch:
    return "before the newest correction";
  case StateVerdict::WrongMap:
    return "on another map";
  case StateVerdict::TooFar:
    return "too far";
  }
  return "?";
}

WorldAuthority::WorldAuthority(std::string hostName, std::string hostPartner, AuthorityRules rules)
    : rules_{rules} {
  PlayerRecord host;
  host.id = HostPlayerId;
  host.name = clipped(std::move(hostName));
  host.partnerSpecies = clipped(std::move(hostPartner));
  players_.push_back(std::move(host));
}

std::optional<PlayerId> WorldAuthority::join(std::string name, std::string partnerSpecies,
                                             net::Millis now, std::optional<PlayerId> as) {
  if (as && (!validGuest(*as) || find(*as) != nullptr)) {
    return std::nullopt;
  }
  for (PlayerId id = as.value_or(GuestPlayerId); validPlayer(id); ++id) {
    if (find(id) != nullptr) {
      continue;
    }
    PlayerRecord guest;
    guest.id = id;
    guest.name = clipped(std::move(name));
    guest.partnerSpecies = clipped(std::move(partnerSpecies));
    guest.heardAt = now;
    guest.acceptedAt = now;
    players_.push_back(std::move(guest));
    std::ranges::sort(players_, {}, &PlayerRecord::id);
    ++revision_;
    return id;
  }
  return std::nullopt;
}

bool WorldAuthority::leave(PlayerId id) {
  if (id == HostPlayerId) {
    return false;
  }
  const auto removed = std::erase_if(players_, [id](const PlayerRecord& r) { return r.id == id; });
  if (removed > 0) {
    ++revision_;
  }
  return removed > 0;
}

void WorldAuthority::setPartner(PlayerId id, std::string partnerSpecies) {
  PlayerRecord* player = record(id);
  partnerSpecies = clipped(std::move(partnerSpecies));
  if (player != nullptr && player->partnerSpecies != partnerSpecies) {
    player->partnerSpecies = std::move(partnerSpecies);
    ++revision_;
  }
}

const PlayerRecord* WorldAuthority::find(PlayerId id) const {
  const auto found = std::ranges::find(players_, id, &PlayerRecord::id);
  return found != players_.end() ? &*found : nullptr;
}

PlayerRecord* WorldAuthority::record(PlayerId id) {
  const auto found = std::ranges::find(players_, id, &PlayerRecord::id);
  return found != players_.end() ? &*found : nullptr;
}

Roster WorldAuthority::roster() const {
  Roster roster{.revision = revision_, .players = {}};
  for (const PlayerRecord& player : players_) {
    roster.players.push_back(
        {.id = player.id, .name = player.name, .partnerSpecies = player.partnerSpecies});
  }
  return roster;
}

void WorldAuthority::setWorldMap(std::string mapId) {
  worldMap_ = std::move(mapId);
}

std::vector<PlayerId> WorldAuthority::playersOn(std::string_view mapId) const {
  std::vector<PlayerId> present;
  for (const PlayerRecord& player : players_) {
    if (player.pose && player.pose->mapId == mapId) {
      present.push_back(player.id);
    }
  }
  return present;
}

void WorldAuthority::setHostPose(PlayerPose pose, net::Millis now) {
  PlayerRecord& host = *record(HostPlayerId);
  host.pose = std::move(pose);
  host.heardAt = now;
  host.acceptedAt = now;
}

StateVerdict WorldAuthority::accept(PlayerId from, const PlayerState& state, net::Millis now) {
  PlayerRecord* player = record(state.player);
  if (player == nullptr || from == HostPlayerId) {
    return StateVerdict::UnknownPlayer;
  }
  if (from != state.player) {
    return StateVerdict::NotOwner;
  }
  player->heardAt = now;
  if (state.epoch != player->epoch) {
    // Epochs only grow on the host; one the host has not given yet is as foreign as an old one.
    return StateVerdict::OldEpoch;
  }
  if (player->sequence && !sequenceNewer(state.sequence, *player->sequence)) {
    return StateVerdict::Stale;
  }
  PlayerPose pose{.mapId = state.mapId,
                  .x = state.x,
                  .y = state.y,
                  .z = state.z,
                  .yaw = state.yaw,
                  .motion = state.motion};
  const auto refuse = [&](StateVerdict verdict) {
    ++player->refusedInRow;
    return verdict;
  };
  if (!worldMap_.empty() && state.mapId != worldMap_) {
    return refuse(StateVerdict::WrongMap);
  }
  if (player->pose && player->pose->mapId == pose.mapId) {
    // The time the owner had to move: its ticks since the newest state taken (the first state of an
    // epoch is measured from where the host put it, with the real time since then), never more
    // than the real time plus a little.
    const double real =
        static_cast<double>((now - player->acceptedAt).count() + rules_.clockSlack.count()) /
        1000.0;
    double seconds = real;
    if (player->sequence) {
      const std::uint32_t ticks = state.sequence - *player->sequence;
      seconds = std::min(real, static_cast<double>(ticks) / rules_.ticksPerSecond);
    }
    const double reach = (static_cast<double>(rules_.maxSpeed) * std::max(seconds, 0.0)) +
                         static_cast<double>(rules_.slack);
    if (groundDistance(*player->pose, pose) > reach) {
      return refuse(StateVerdict::TooFar);
    }
  }
  player->sequence = state.sequence;
  player->pose = std::move(pose);
  player->acceptedAt = now;
  player->refusedInRow = 0;
  return StateVerdict::Accepted;
}

std::optional<PlayerCorrection> WorldAuthority::relocate(PlayerId id, PlayerPose pose,
                                                         std::optional<std::int32_t> exit) {
  PlayerRecord* player = record(id);
  if (player == nullptr || id == HostPlayerId) {
    return std::nullopt;
  }
  ++player->epoch;
  player->sequence.reset();
  player->pose = std::move(pose);
  player->refusedInRow = 0;
  return correctionOf(*player, exit);
}

std::optional<PlayerCorrection> WorldAuthority::correctionDue(PlayerId id) {
  PlayerRecord* player = record(id);
  if (player == nullptr || id == HostPlayerId || !player->pose ||
      player->refusedInRow < rules_.refusalsBeforeCorrection) {
    return std::nullopt;
  }
  PlayerPose pose = *player->pose;
  return relocate(id, std::move(pose));
}

std::vector<PlayerId> WorldAuthority::silent(net::Millis now, net::Millis limit) const {
  std::vector<PlayerId> quiet;
  for (const PlayerRecord& player : players_) {
    if (player.id != HostPlayerId && now - player.heardAt >= limit) {
      quiet.push_back(player.id);
    }
  }
  return quiet;
}

void WorldAuthority::heard(PlayerId id, net::Millis now) {
  if (PlayerRecord* player = record(id)) {
    player->heardAt = now;
  }
}

net::Millis PoseTrack::playhead(net::Millis now) const {
  return now - *offset_ - rules_.delay;
}

const PoseTrack::Entry* PoseTrack::stateBefore(std::size_t index) const {
  for (std::size_t i = std::min(index, entries_.size()); i-- > 0;) {
    if (!entries_[i].bridge) {
      return &entries_[i];
    }
  }
  return nullptr;
}

bool PoseTrack::jumped(const Entry* from, const Entry& to) const {
  if (from == nullptr) {
    return false;
  }
  const float seconds =
      static_cast<float>(std::max(to.senderTime - from->senderTime, net::Millis{0}).count()) /
      1000.0F;
  return groundDistance(from->pose, to.pose) > rules_.snapSlack + (rules_.maxSpeed * seconds);
}

bool PoseTrack::push(std::uint32_t sequence, net::Millis senderTime, const PlayerPose& pose,
                     net::Millis now) {
  const bool newest = entries_.empty() || sequenceNewer(sequence, entries_.back().sequence);
  if (pose.mapId != mapId_) {
    // A straggler of the map before must not take the track back there.
    if (!newest) {
      return false;
    }
    reset();
    mapId_ = pose.mapId;
  }
  if (std::ranges::any_of(entries_,
                          [&](const Entry& e) { return !e.bridge && e.sequence == sequence; })) {
    return false;
  }
  Entry entry{.sequence = sequence, .senderTime = senderTime, .pose = tracked(pose)};
  if (!newest) {
    // Late, but still ahead of the playback: it fills its place (never before a catch-up).
    if (senderTime <= playhead(now)) {
      return false;
    }
    const auto place =
        std::ranges::find_if(entries_, [&](const Entry& e) { return e.senderTime > senderTime; });
    if (place == entries_.end() ||
        std::any_of(place, entries_.end(), [](const Entry& e) { return e.bridge; })) {
      return false;
    }
    const auto index = static_cast<std::size_t>(place - entries_.begin());
    entry.jump = jumped(stateBefore(index), entry);
    entries_.insert(place, entry);
    const auto next = std::find_if(entries_.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                                   entries_.end(), [](const Entry& e) { return !e.bridge; });
    if (next != entries_.end()) {
      next->jump = jumped(&entries_[index], *next);
    }
    return true;
  }
  const std::optional<TrackedPose> drawn = sample(now);
  const net::Millis offset = now - senderTime;
  // The smallest offset is the quickest delivery; it creeps up slowly so a route that got a little
  // slower is followed too, and jumps after a pause of the sender's clock.
  if (!offset_ || offset - *offset_ > rules_.resync) {
    offset_ = offset;
  } else {
    offset_ = std::min(offset, *offset_ + net::Millis{1});
  }
  entry.jump = jumped(stateBefore(entries_.size()), entry);
  if (drawn && !entry.jump && !entries_.empty()) {
    // States came again after the playback ran past the newest: it goes on from where it was
    // drawn, catching up within the delay instead of jumping to the new state's time.
    const net::Millis head = playhead(now);
    if (head > entries_.back().senderTime && head < senderTime) {
      entries_.push_back({.sequence = entries_.back().sequence,
                          .senderTime = head,
                          .pose = *drawn,
                          .jump = false,
                          .bridge = true});
    }
  }
  entries_.push_back(entry);
  while (entries_.size() > rules_.capacity) {
    entries_.erase(entries_.begin());
  }
  return true;
}

std::optional<TrackedPose> PoseTrack::sample(net::Millis now) const {
  if (entries_.empty() || !offset_) {
    return std::nullopt;
  }
  const net::Millis at = playhead(now);
  const Entry& newest = entries_.back();
  if (at >= newest.senderTime) {
    TrackedPose pose = newest.pose;
    const Entry* before = stateBefore(entries_.size() - 1);
    if (before == nullptr || newest.jump || newest.pose.motion == Motion::Idle) {
      return pose;
    }
    const auto span = static_cast<float>((newest.senderTime - before->senderTime).count());
    if (span <= 0.0F) {
      return pose;
    }
    const float ahead =
        static_cast<float>(std::min(at - newest.senderTime, rules_.extrapolation).count()) / span;
    pose.x += (newest.pose.x - before->pose.x) * ahead;
    pose.y += (newest.pose.y - before->pose.y) * ahead;
    pose.z += (newest.pose.z - before->pose.z) * ahead;
    return pose;
  }
  if (at <= entries_.front().senderTime) {
    return entries_.front().pose;
  }
  const auto after =
      std::ranges::find_if(entries_, [&](const Entry& e) { return e.senderTime > at; });
  const Entry& to = *after;
  const Entry& from = *std::prev(after);
  if (to.jump) {
    return from.pose;
  }
  const auto span = static_cast<float>((to.senderTime - from.senderTime).count());
  const float t = span > 0.0F ? static_cast<float>((at - from.senderTime).count()) / span : 1.0F;
  return TrackedPose{.x = std::lerp(from.pose.x, to.pose.x, t),
                     .y = std::lerp(from.pose.y, to.pose.y, t),
                     .z = std::lerp(from.pose.z, to.pose.z, t),
                     .yaw = mixYaw(from.pose.yaw, to.pose.yaw, t),
                     .motion = t < 0.5F ? from.pose.motion : to.pose.motion};
}

std::optional<std::uint32_t> PoseTrack::newest() const {
  if (entries_.empty()) {
    return std::nullopt;
  }
  return entries_.back().sequence;
}

void PoseTrack::reset() {
  entries_.clear();
  mapId_.clear();
  offset_.reset();
}

} // namespace dwe::game::coop
