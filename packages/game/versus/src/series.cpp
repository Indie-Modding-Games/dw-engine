#include <dwe/game/versus/series.hpp>

#include <algorithm>

namespace dwe::game::versus {

Series::Series(std::int32_t battles) : battles_{std::max(battles, 1)} {}

void Series::record(RoundResult result) {
  if (!finished()) {
    results_.push_back(result);
  }
}

bool Series::wonRound(Side side, std::int32_t round) const {
  if (round < 0 || round >= this->round()) {
    return false;
  }
  const RoundResult result = results_.at(static_cast<std::size_t>(round));
  return result == (side == Side::Host ? RoundResult::HostWon : RoundResult::GuestWon);
}

std::int32_t Series::wins(Side side) const {
  std::int32_t wins = 0;
  for (std::int32_t round = 0; round < this->round(); ++round) {
    wins += wonRound(side, round) ? 1 : 0;
  }
  return wins;
}

std::int32_t Series::losses(Side side) const {
  return round() - wins(side);
}

std::optional<Side> Series::winner() const {
  if (!finished()) {
    return std::nullopt;
  }
  const std::int32_t host = wins(Side::Host);
  const std::int32_t guest = wins(Side::Guest);
  if (host == guest) {
    return std::nullopt;
  }
  return host > guest ? Side::Host : Side::Guest;
}

} // namespace dwe::game::versus
