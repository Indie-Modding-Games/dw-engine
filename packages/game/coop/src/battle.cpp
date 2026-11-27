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
  case BattleActionKind::Order:
  case BattleActionKind::Stance:
    return role != BattleRole::Helper;
  case BattleActionKind::CloseEnd:
    return role == BattleRole::Starter;
  case BattleActionKind::ThrowItem:
  case BattleActionKind::Cheer:
    return true;
  }
  return false;
}

bool spoilsInRange(const BattleSpoils& spoils) {
  constexpr std::int32_t MaxPool = 9999;
  constexpr std::int32_t MaxCare = 100;
  constexpr std::int32_t MaxLives = 99;
  constexpr std::int32_t MaxCount = 9999;
  constexpr std::int32_t MinutesPerDay = 24 * 60;
  constexpr std::int64_t FramesPerDay = std::int64_t{60} * 60 * MinutesPerDay;
  const auto within = [](auto value, auto low, auto high) { return value >= low && value <= high; };
  return within(spoils.hp, 0, MaxPool) && within(spoils.mp, 0, MaxPool) &&
         std::ranges::all_of(spoils.statGains,
                             [&](std::int32_t gain) { return within(gain, 0, MaxPool); }) &&
         std::ranges::all_of(
             spoils.care, [&](std::int32_t change) { return within(change, -MaxCare, MaxCare); }) &&
         within(spoils.food, -MaxCount, MaxCount) && within(spoils.livesLost, 0, MaxLives) &&
         within(spoils.elapsedMinutes, 0, MinutesPerDay) &&
         within(spoils.lifetimeHoursLost, 0, MaxCount) &&
         within(spoils.mpDiscountLearned, 0, MaxCare) &&
         std::ranges::all_of(spoils.counters,
                             [&](std::int32_t count) { return within(count, 0, MaxCount); }) &&
         within(spoils.frames, std::int64_t{0}, FramesPerDay);
}

BattleAskVerdict BattleReferee::judge(const BattleAsk& ask, const HostMoment& host) const {
  if (running_) {
    return {.waits = false, .refusal = BattleRefusal::Busy};
  }
  if (!host.map.empty() && ask.mapId != host.map) {
    return {.waits = false, .refusal = BattleRefusal::NotHere};
  }
  if (host.map.empty() || !host.guestHere || host.battling || host.held) {
    return {.waits = true, .refusal = std::nullopt};
  }
  return {};
}

bool BattleReferee::wait(PlayerId guest, const BattleAsk& ask, std::uint64_t tick) {
  auto kept = std::ranges::find(waiting_, guest, &WaitingAsk::guest);
  if (kept == waiting_.end() || kept->ask.ask != ask.ask) {
    forget(guest);
    waiting_.push_back({.guest = guest, .ask = ask, .since = tick});
    return true;
  }
  if (tick - kept->since < BattleAskWaitTicks) {
    return true;
  }
  forget(guest);
  return false;
}

void BattleReferee::forget(PlayerId guest) {
  std::erase_if(waiting_, [guest](const WaitingAsk& waiting) { return waiting.guest == guest; });
}

BattleBegin BattleReferee::begin(PlayerId starter, std::uint32_t ask, std::string mapId,
                                 std::vector<std::int16_t> enemyNpcs,
                                 std::vector<std::string> species, bool fixedFight,
                                 std::vector<PlayerId> allies, bool folder) {
  const std::uint32_t battle = next_++;
  if (next_ == 0) {
    next_ = 1;
  }
  running_ = Running{.battle = battle, .starter = starter, .ask = ask, .allies = allies};
  forget(starter);
  return {.battle = battle,
          .starter = starter,
          .ask = starter != HostPlayerId ? ask : 0,
          .mapId = std::move(mapId),
          .enemyNpcs = std::move(enemyNpcs),
          .species = std::move(species),
          .fixedFight = fixedFight,
          .allies = std::move(allies),
          .folder = folder};
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
    forget(guest);
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

std::optional<PlayerId> BattleMirror::waitingFor() const {
  if (!running_ || !frame_ || frame_->phase != BattlePhase::End ||
      action(BattleActionKind::CloseEnd)) {
    return std::nullopt;
  }
  return running_->starter;
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
