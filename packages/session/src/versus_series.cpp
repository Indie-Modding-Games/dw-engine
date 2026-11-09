#include <dwe/session/versus_match.hpp>
#include <dwe/session/versus_series.hpp>

#include <cstddef>
#include <utility>

namespace dwe::session {

VersusSeries::VersusSeries(const BattleAdapter& battles, game::versus::VersusLink link,
                           VersusPlacement placement)
    : battles_{&battles}, link_{std::move(link)}, placement_{std::move(placement)} {}

void VersusSeries::poll(net::Millis now) {
  link_.poll(now);
}

game::battle::FighterId VersusSeries::localFighter() const {
  return link_.side() == game::versus::Side::Host ? game::battle::PartnerId : game::battle::RivalId;
}

core::Result<void> VersusSeries::startNext(const BattlePrep& prepare) {
  if (link_.phase() != game::versus::LinkPhase::Playing || link_.lockstep() != nullptr ||
      (battle_ && !roundSettled())) {
    return core::fail("versus: no battle can start now");
  }
  const std::int32_t round = link_.series().round();
  const auto place = static_cast<std::size_t>(round);
  const auto& host = link_.lineup(game::versus::Side::Host);
  const auto& guest = link_.lineup(game::versus::Side::Guest);
  if (!host || !guest || place >= host->size() || place >= guest->size()) {
    link_.leave(game::versus::LeaveReason::BadLineup);
    return core::fail("versus: a lineup lacks the partner of this battle");
  }
  auto started = battles_->startVersus(host->at(place), guest->at(place), placement_,
                                       roundSeed(*link_.setup(), round));
  if (!started) {
    link_.leave(game::versus::LeaveReason::BadLineup);
    return std::unexpected{started.error()};
  }
  if (auto lockstep = link_.startRound(); !lockstep) {
    return lockstep;
  }
  battle_.emplace(std::move(*started));
  if (prepare) {
    prepare(*battle_);
  }
  pending_.reset();
  return {};
}

game::battle::BattleEvents VersusSeries::frame() {
  game::battle::BattleEvents events;
  if (!battle_ || link_.lockstep() == nullptr || link_.awaitingResult()) {
    return events;
  }
  if (link_.lockstep()->wantsInput()) {
    link_.submitInput(versusInputOf(pending_.value_or(game::battle::Command::None)));
    pending_.reset();
  }
  while (link_.lockstep() != nullptr && !link_.awaitingResult() && !battle_->finished()) {
    const auto inputs = link_.lockstep()->ready();
    if (!inputs) {
      break;
    }
    commandVersus(*battle_, *inputs);
    const game::battle::BattleEvents& tick = battle_->step();
    events.insert(events.end(), tick.begin(), tick.end());
    link_.playedTick(battle_->checksum());
    if (battle_->finished()) {
      link_.finishRound(roundResultOf(*battle_->result()), battle_->checksum());
    }
  }
  return events;
}

bool VersusSeries::roundSettled() const {
  return battle_ && battle_->finished() && !link_.awaitingResult() && link_.lockstep() == nullptr;
}

} // namespace dwe::session
