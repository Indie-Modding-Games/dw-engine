#include "coop_world.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/field/grid.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "coop_snapshot.hpp"

namespace dwe::app {

namespace {

namespace coop = game::coop;

// How often a guest reports its transform: every third 60 Hz tick, as the host's snapshots.
constexpr std::uint64_t ReportTicks = 3;
// The guest's ticks per second: its states' sequence counts them.
constexpr std::int64_t GuestTicksPerSecond = 60;
// How far from its player a guest's held partner may stand (a gym course's equipment is a few
// cells away); a report beyond it is not drawn.
constexpr float HeldPartnerReach = 2000.0F;

std::uint16_t yawUnitsOf(float yaw) {
  constexpr double Tau = std::numbers::pi_v<double> * 2.0;
  double turn = std::fmod(static_cast<double>(yaw), Tau);
  if (turn < 0.0) {
    turn += Tau;
  }
  return static_cast<std::uint16_t>(std::lround(turn * 65536.0 / Tau) & 0xFFFF);
}

coop::Motion coopMotionOf(scene::Motion motion) {
  switch (motion) {
  case scene::Motion::Idle:
    return coop::Motion::Idle;
  case scene::Motion::Walk:
    return coop::Motion::Walk;
  case scene::Motion::Run:
    return coop::Motion::Run;
  }
  return coop::Motion::Idle;
}

std::int32_t clampedCoordinate(float value) {
  const float bounded =
      std::clamp(std::isfinite(value) ? value : 0.0F, -static_cast<float>(coop::MaxWorldCoordinate),
                 static_cast<float>(coop::MaxWorldCoordinate));
  return static_cast<std::int32_t>(std::lround(bounded));
}

} // namespace

coop::PlayerPose coopPoseOf(std::string mapId, const game::field::CharacterState& state) {
  return {.mapId = std::move(mapId),
          .x = clampedCoordinate(state.position.x),
          .y = clampedCoordinate(state.position.y),
          .z = clampedCoordinate(state.position.z),
          .yaw = yawUnitsOf(state.yaw),
          .motion = coopMotionOf(state.motion)};
}

scene::Motion sceneMotionOf(coop::Motion motion) {
  switch (motion) {
  case coop::Motion::Idle:
    return scene::Motion::Idle;
  case coop::Motion::Walk:
    return scene::Motion::Walk;
  case coop::Motion::Run:
    return scene::Motion::Run;
  }
  return scene::Motion::Idle;
}

game::field::CharacterState fieldStateOf(const coop::TrackedPose& pose) {
  return {
      .position = {pose.x, pose.y, pose.z}, .yaw = pose.yaw, .motion = sceneMotionOf(pose.motion)};
}

CoopHostWorld::CoopHostWorld(std::string hostName, std::string hostPartner)
    : authority_{std::move(hostName), std::move(hostPartner)} {}

std::optional<coop::PlayerId> CoopHostWorld::join(std::string name, std::string partner,
                                                  net::Millis now,
                                                  std::optional<coop::PlayerId> as) {
  const auto id = authority_.join(std::move(name), std::move(partner), now, as);
  if (id) {
    core::logInfo("coop: player {} joined the session ({} players)", *id, authority_.size());
  }
  return id;
}

void CoopHostWorld::leave(coop::PlayerId id) {
  if (authority_.leave(id)) {
    core::logInfo("coop: player {} left the session ({} players)", id, authority_.size());
  }
  tracks_.erase(id);
  held_.erase(id);
  placed_.erase(id);
  verdicts_.erase(id);
  ledger_.leave(id);
  synced_.erase(id);
  std::erase_if(asks_, [id](const auto& ask) { return ask.first == id; });
  std::erase_if(claims_, [id](const auto& claim) { return claim.first == id; });
  lastClaim_.erase(id);
}

void CoopHostWorld::setPartner(coop::PlayerId id, std::string species) {
  if (const coop::PlayerRecord* record = authority_.find(id);
      record != nullptr && record->partnerSpecies != species) {
    core::logInfo("coop: player {}'s partner is now {}", id, species);
    authority_.setPartner(id, std::move(species));
  }
}

void CoopHostWorld::receive(coop::PlayerId from, const coop::Message& message, net::Millis now) {
  if (const auto* form = std::get_if<coop::PartnerForm>(&message)) {
    // The link's player's own partner, whatever player the message names.
    setPartner(from, form->species);
    return;
  }
  if (const auto* ask = std::get_if<coop::WorldAsk>(&message)) {
    // Taken after the host's next tick (share), against its own world.
    asks_.emplace_back(from, *ask);
    return;
  }
  if (const auto* claim = std::get_if<coop::ItemClaim>(&message)) {
    claims_.emplace_back(from, *claim);
    return;
  }
  const auto* state = std::get_if<coop::PlayerState>(&message);
  if (state == nullptr) {
    return;
  }
  const coop::StateVerdict verdict = authority_.accept(from, *state, now);
  const auto last = verdicts_.find(from);
  if (verdict != coop::StateVerdict::Accepted &&
      (last == verdicts_.end() || last->second != verdict)) {
    // Once per run of the same verdict: a lagging guest refuses many in a row.
    core::logDebug("coop: player {}'s state {} refused: {}", from, state->sequence,
                   coop::stateVerdictName(verdict));
  }
  verdicts_[from] = verdict;
  if (verdict != coop::StateVerdict::Accepted) {
    return;
  }
  const net::Millis sentAt{static_cast<std::int64_t>(state->sequence) * 1000 / GuestTicksPerSecond};
  (void)tracks_[from].push(state->sequence, sentAt, *authority_.find(from)->pose, now);
  held_[from] = {.action = state->action, .partner = state->partner};
}

std::vector<CoopOutgoing> CoopHostWorld::tick(game::field::Field& field, std::uint32_t mapSerial,
                                              std::optional<std::int32_t> arrivalExit,
                                              net::Millis now) {
  std::vector<CoopOutgoing> out;
  const std::string& mapId = field.mapId();
  if (mapId.empty()) {
    return out;
  }
  authority_.setWorldMap(mapId);
  authority_.setHostPose(coopPoseOf(mapId, field.player()), now);
  if (mapSerial_ != mapSerial) {
    // A new map: the field put every guest near the host (Field::placeOtherPlayersNearHost); each
    // one is told so, with the exit the host came in by.
    mapSerial_ = mapSerial;
    placed_.clear();
  }
  const float radius = field.rules().player.radius;
  for (const coop::PlayerRecord& record : authority_.players()) {
    const coop::PlayerId id = record.id;
    if (id == coop::HostPlayerId) {
      continue;
    }
    const auto body = field.player(id);
    if (!body) {
      continue;
    }
    if (!placed_.contains(id)) {
      // Joined or the group changed map: the guest stands where the host's field put its body.
      if (auto correction = authority_.relocate(id, coopPoseOf(mapId, *body), arrivalExit)) {
        out.push_back({.to = id, .message = std::move(*correction)});
      }
      tracks_.erase(id);
      held_.erase(id);
      showHeld(field, id, {});
      (void)field.drivePlayer(id, std::nullopt);
      placed_.insert(id);
      continue;
    }
    if (auto correction = authority_.correctionDue(id)) {
      // Refused too often: back to its newest valid pose, on both sides.
      core::logInfo("coop: player {} corrected to its last valid place ({}, {})", id, correction->x,
                    correction->z);
      game::field::CharacterState back = *body;
      back.position = {static_cast<float>(correction->x), static_cast<float>(correction->y),
                       static_cast<float>(correction->z)};
      back.motion = scene::Motion::Idle;
      tracks_.erase(id);
      held_.erase(id);
      showHeld(field, id, {});
      (void)field.drivePlayer(id, back);
      out.push_back({.to = id, .message = std::move(*correction)});
      continue;
    }
    const auto track = tracks_.find(id);
    if (track == tracks_.end() || track->second.mapId() != mapId) {
      continue;
    }
    const auto held = held_.find(id);
    showHeld(field, id, held != held_.end() ? held->second : OwnerHeld{});
    if (const auto pose = track->second.sample(now)) {
      game::field::CharacterState driven = fieldStateOf(*pose);
      // The host's map is the judge of where a body can stand: a pose in a wall is not taken.
      if (game::field::fitsOnGrid(field.collision(), driven.position, radius)) {
        (void)field.drivePlayer(id, driven);
      }
    }
  }
  if (authority_.revision() != rosterSent_) {
    rosterSent_ = authority_.revision();
    const coop::Roster roster = authority_.roster();
    for (const coop::PlayerRecord& record : authority_.players()) {
      if (record.id != coop::HostPlayerId) {
        out.push_back({.to = record.id, .message = roster});
      }
    }
  }
  return out;
}

void CoopHostWorld::showHeld(game::field::Field& field, coop::PlayerId id, const OwnerHeld& held) {
  if (const auto player = field.playerEntity(id)) {
    field.showOwnerClip(*player, playedClipOf(held.action));
  }
  const auto partner = field.partnerEntity(id);
  if (!partner) {
    return;
  }
  std::optional<game::field::CharacterState> at;
  if (held.partner) {
    const coop::HeldPartner& pose = *held.partner;
    const game::field::CharacterState state{
        .position = {static_cast<float>(pose.x), static_cast<float>(pose.y),
                     static_cast<float>(pose.z)},
        .yaw = static_cast<float>(pose.yaw) * (std::numbers::pi_v<float> * 2.0F) / 65536.0F,
        .motion = sceneMotionOf(pose.motion)};
    const auto body = field.player(id);
    if (body && glm::distance(body->position, state.position) <= HeldPartnerReach) {
      at = state;
    }
  }
  (void)field.drivePartner(id, at);
  field.showOwnerClip(*partner, at ? playedClipOf(held.partner->action) : std::nullopt);
}

void CoopGuestWorld::receive(const coop::Message& message) {
  if (const auto* correction = std::get_if<coop::PlayerCorrection>(&message)) {
    if (correction->player == self_) {
      pending_ = *correction;
    }
  } else if (const auto* roster = std::get_if<coop::Roster>(&message)) {
    if (!roster_ || coop::sequenceNewer(roster->revision, roster_->revision)) {
      roster_ = *roster;
    }
  } else if (const auto* ground = std::get_if<coop::GroundItems>(&message)) {
    ground_.receive(*ground);
  } else if (const auto* answer = std::get_if<coop::ClaimAnswer>(&message)) {
    answers_.push_back(*answer);
  } else if (std::holds_alternative<coop::WorldChange>(message) ||
             std::holds_alternative<coop::WorldSync>(message)) {
    worldIn_.push_back(message);
  }
}

std::optional<CoopGuestWorld::Move> CoopGuestWorld::apply(const game::field::Field& field,
                                                          bool moving) {
  if (!pending_) {
    return std::nullopt;
  }
  const coop::PlayerCorrection& correction = *pending_;
  Move move{.map = correction.mapId,
            .exit = correction.exit,
            .at = {.position = {static_cast<float>(correction.x), static_cast<float>(correction.y),
                                static_cast<float>(correction.z)},
                   .yaw = static_cast<float>(correction.yaw) * (std::numbers::pi_v<float> * 2.0F) /
                          65536.0F}};
  if (field.mapChangePending() || moving) {
    // On its way (the host's move, or one of the guest's own story): wait for the arrival.
    return std::nullopt;
  }
  if (!asked_ && correction.exit && epoch_ != 0 && placedOn_ == correction.mapId &&
      field.mapId() == correction.mapId) {
    core::logInfo("coop: the host loaded {} again: this game loads it again too", correction.mapId);
    asked_ = true;
    move.reload = true;
    return move;
  }
  if (field.mapId() == correction.mapId &&
      (asked_ || glm::distance(field.player().position, move.at.position) <= 1.0F)) {
    // Arrived (or already there): from now on the reports carry this correction's epoch.
    core::logInfo("coop: placed by the host on {} at ({}, {}), epoch {}", correction.mapId,
                  correction.x, correction.z, correction.epoch);
    epoch_ = correction.epoch;
    placedOn_ = correction.mapId;
    pending_.reset();
    asked_ = false;
    if (field.mapId() == move.map &&
        glm::distance(field.player().position, move.at.position) > 1.0F) {
      return move;
    }
    return std::nullopt;
  }
  // Not there yet: ask for it (again, when a story warp of the guest's own took it elsewhere).
  asked_ = true;
  if (field.mapId() != correction.mapId) {
    core::logInfo("coop: the host leads to {}", correction.mapId);
  }
  return move;
}

std::optional<coop::PlayerState> CoopGuestWorld::report(const game::field::Field& field,
                                                        bool moving, std::uint64_t tick) const {
  if (tick % ReportTicks != 0 || pending_ || epoch_ == 0 || field.mapId().empty() ||
      field.mapChangePending() || moving) {
    return std::nullopt;
  }
  const coop::PlayerPose pose = coopPoseOf(field.mapId(), field.player());
  // The partner goes along while this game holds it (a gym course, a script): the host does not
  // know where its course or scene put it.
  std::optional<coop::HeldPartner> partner;
  const std::optional<game::field::PlayedClip> partnerClip =
      field.scriptedClipOf(field.partnerEntity());
  if (field.heldByScript() || partnerClip) {
    const coop::PlayerPose at = coopPoseOf(field.mapId(), field.partner());
    partner = coop::HeldPartner{.x = at.x,
                                .y = at.y,
                                .z = at.z,
                                .yaw = at.yaw,
                                .motion = at.motion,
                                .action = coopActionOf(partnerClip)};
  }
  return coop::PlayerState{.player = self_,
                           .sequence = static_cast<std::uint32_t>(tick),
                           .epoch = epoch_,
                           .mapId = pose.mapId,
                           .x = pose.x,
                           .y = pose.y,
                           .z = pose.z,
                           .yaw = pose.yaw,
                           .motion = pose.motion,
                           .action = coopActionOf(field.scriptedClipOf(field.playerEntity())),
                           .partner = partner};
}

std::optional<coop::PartnerForm> CoopGuestWorld::partnerForm(std::string_view species,
                                                             std::uint64_t tick) {
  constexpr std::uint64_t ResendTicks = 60;
  if (!roster_ || species.empty() || partnerOf(self_) == species) {
    return std::nullopt;
  }
  if (formSent_ == species && formSentAt_ && tick - *formSentAt_ < ResendTicks) {
    return std::nullopt;
  }
  formSent_ = std::string{species};
  formSentAt_ = tick;
  return coop::PartnerForm{.player = self_, .species = formSent_};
}

bool coopPlayerHere(const game::field::Field& field, std::span<const coop::PlayerId> here,
                    coop::PlayerId player) {
  return !field.mapId().empty() && !field.mapChangePending() &&
         std::ranges::find(here, player) != here.end();
}

bool CoopGuestWorld::inSession(coop::PlayerId id) const {
  return !roster_ ||
         std::ranges::any_of(roster_->players, [id](const auto& entry) { return entry.id == id; });
}

std::string CoopGuestWorld::partnerOf(coop::PlayerId id) const {
  if (!roster_) {
    return {};
  }
  const auto found = std::ranges::find(roster_->players, id, &coop::RosterEntry::id);
  return found != roster_->players.end() ? found->partnerSpecies : std::string{};
}

} // namespace dwe::app
