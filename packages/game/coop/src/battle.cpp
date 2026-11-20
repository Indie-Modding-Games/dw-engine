#include <dwe/game/coop/battle.hpp>

#include <algorithm>
#include <utility>

namespace dwe::game::coop {

namespace {

// The standard half-range comparison, so a frame count that wraps still orders.
bool newer(std::uint32_t candidate, std::uint32_t current) {
  return candidate != current && static_cast<std::uint32_t>(candidate - current) < 0x8000'0000U;
}

} // namespace

bool roleAllows(BattleRole role, BattleActionKind kind) {
  switch (kind) {
  case BattleActionKind::Command:
    return role != BattleRole::Helper;
  case BattleActionKind::CloseEnd:
    return role == BattleRole::Starter;
  case BattleActionKind::ThrowItem:
  case BattleActionKind::Cheer:
    return true;
  }
  return false;
}

std::optional<BattleRefusal> BattleReferee::judge(const BattleAsk& ask, std::string_view hostMap,
                                                  bool hostBusy) const {
  if (running_ || hostBusy) {
    return BattleRefusal::Busy;
  }
  if (ask.mapId != hostMap) {
    return BattleRefusal::NotHere;
  }
  return std::nullopt;
}

BattleBegin BattleReferee::begin(PlayerId starter, std::uint32_t ask, std::string mapId,
                                 std::vector<std::int16_t> enemyNpcs,
                                 std::vector<std::string> species, bool fixedFight,
                                 std::vector<PlayerId> allies) {
  const std::uint32_t battle = next_++;
  if (next_ == 0) {
    next_ = 1;
  }
  running_ = Running{.battle = battle, .starter = starter, .ask = ask, .allies = allies};
  return {.battle = battle,
          .starter = starter,
          .ask = starter != HostPlayerId ? ask : 0,
          .mapId = std::move(mapId),
          .enemyNpcs = std::move(enemyNpcs),
          .species = std::move(species),
          .fixedFight = fixedFight,
          .allies = std::move(allies)};
}

void BattleReferee::receive(PlayerId guest, const BattleOffer& offer) {
  if (validGuest(guest)) {
    offers_.at(guest) = offer;
  }
}

std::optional<BattleFighterProfile> BattleReferee::offered(PlayerId guest) const {
  if (!validGuest(guest) || !present_.at(guest)) {
    return std::nullopt;
  }
  const std::optional<BattleOffer>& offer = offers_.at(guest);
  if (!offer || !offer->available) {
    return std::nullopt;
  }
  return offer->fighter;
}

std::optional<BattleRole> BattleReferee::roleOf(PlayerId player) const {
  if (!running_) {
    return std::nullopt;
  }
  if (running_->starter == player) {
    return BattleRole::Starter;
  }
  return std::ranges::contains(running_->allies, player) ? BattleRole::Ally : BattleRole::Helper;
}

bool BattleReferee::allows(PlayerId from, const BattleAction& action) const {
  if (!running_ || !present(from) || action.battle != running_->battle) {
    return false;
  }
  const std::optional<BattleRole> role = roleOf(from);
  return role && roleAllows(*role, action.kind);
}

std::vector<BattleReferee::Addressed>
BattleReferee::end(BattleOutcome outcome, std::vector<std::int16_t> leftMap, std::int32_t bits,
                   std::span<const std::pair<PlayerId, BattleSpoils>> spoils) {
  std::vector<Addressed> ends;
  if (!running_) {
    return ends;
  }
  const Running ended = *std::exchange(running_, std::nullopt);
  for (const PlayerId guest : guests()) {
    BattleEnd end{.battle = ended.battle,
                  .outcome = outcome,
                  .leftMap = leftMap,
                  .bits = bits,
                  .spoils = std::nullopt};
    const bool fought = ended.starter == guest || std::ranges::contains(ended.allies, guest);
    const auto own = std::ranges::find(spoils, guest, &std::pair<PlayerId, BattleSpoils>::first);
    if (fought && own != spoils.end()) {
      end.spoils = own->second;
    }
    ends.push_back({.to = guest, .end = std::move(end)});
  }
  return ends;
}

void BattleReferee::guestJoined(PlayerId guest) {
  if (validGuest(guest)) {
    present_.at(guest) = true;
  }
}

void BattleReferee::guestLeft(PlayerId guest) {
  if (validGuest(guest)) {
    present_.at(guest) = false;
    offers_.at(guest).reset();
  }
}

bool BattleReferee::present(PlayerId guest) const {
  return validGuest(guest) && present_.at(guest);
}

std::vector<PlayerId> BattleReferee::guests() const {
  std::vector<PlayerId> there;
  for (PlayerId guest = GuestPlayerId; validPlayer(guest); ++guest) {
    if (present_.at(guest)) {
      there.push_back(guest);
    }
  }
  return there;
}

bool BattleReferee::starterPresent() const {
  return !running_ || running_->starter == HostPlayerId || present(running_->starter);
}

BattleAsk BattleMirror::ask(std::string mapId, std::vector<std::int16_t> enemyNpcs, bool fixedFight,
                            bool predefined, BattleFighterProfile fighter) {
  const std::uint32_t number = nextAsk_++;
  if (nextAsk_ == 0) {
    nextAsk_ = 1;
  }
  pending_ = number;
  return {.ask = number,
          .mapId = std::move(mapId),
          .enemyNpcs = std::move(enemyNpcs),
          .fixedFight = fixedFight,
          .predefined = predefined,
          .fighter = std::move(fighter)};
}

void BattleMirror::receive(const BattleBegin& begin) {
  if (begin.starter == self_) {
    // A begin for an ask that is not the one waiting (an old one, or none) is not this guest's to
    // run: it still shows it.
    if (pending_ && *pending_ == begin.ask) {
      pending_.reset();
    }
  }
  running_ = begin;
  frame_.reset();
  began_ = begin;
  ended_.reset();
}

void BattleMirror::receive(const BattleRefused& refused) {
  if (!pending_ || *pending_ != refused.ask) {
    return;
  }
  pending_.reset();
  refused_ = refused;
}

void BattleMirror::receive(const BattleFrame& frame) {
  if (!running_ || frame.battle != running_->battle) {
    return;
  }
  if (frame_ && !newer(frame.frame, frame_->frame)) {
    return;
  }
  frame_ = frame;
}

void BattleMirror::receive(const BattleEnd& end) {
  if (!running_ || end.battle != running_->battle) {
    return;
  }
  BattleEnd ended = end;
  if (role() == BattleRole::Helper) {
    ended.spoils.reset();
  }
  running_.reset();
  frame_.reset();
  ended_ = std::move(ended);
}

std::optional<BattleBegin> BattleMirror::takeBegan() {
  return std::exchange(began_, std::nullopt);
}

std::optional<BattleRefused> BattleMirror::takeRefused() {
  return std::exchange(refused_, std::nullopt);
}

std::optional<BattleEnd> BattleMirror::takeEnded() {
  return std::exchange(ended_, std::nullopt);
}

std::optional<BattleRole> BattleMirror::role() const {
  if (!running_) {
    return std::nullopt;
  }
  if (running_->starter == self_) {
    return BattleRole::Starter;
  }
  return std::ranges::contains(running_->allies, self_) ? BattleRole::Ally : BattleRole::Helper;
}

std::optional<BattleOffer> BattleMirror::offer(bool available, BattleFighterProfile fighter) {
  BattleOffer next{.available = available, .fighter = std::move(fighter)};
  if (offered_ && *offered_ == next) {
    return std::nullopt;
  }
  offered_ = next;
  return next;
}

std::optional<BattleAction> BattleMirror::action(BattleActionKind kind, std::int32_t value,
                                                 std::string item) const {
  const std::optional<BattleRole> mine = role();
  if (!mine || !roleAllows(*mine, kind)) {
    return std::nullopt;
  }
  return BattleAction{
      .battle = running_->battle, .kind = kind, .value = value, .item = std::move(item)};
}

BattleMirror::Dropped BattleMirror::hostLeft() {
  Dropped dropped{.ask = std::exchange(pending_, std::nullopt), .battle = running_.has_value()};
  offered_.reset();
  running_.reset();
  frame_.reset();
  began_.reset();
  refused_.reset();
  ended_.reset();
  return dropped;
}

} // namespace dwe::game::coop
