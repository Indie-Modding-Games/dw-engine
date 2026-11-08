#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace dwe::game::versus {

// The sides of a versus series: the host is the first player (1P), the guest the second (2P).
enum class Side : std::uint8_t { Host = 0, Guest = 1 };

// How one battle of a series ended (vs_combat.c: VS__combatMain: 1, -1, 2).
enum class RoundResult : std::int8_t { HostWon = 1, GuestWon = -1, BothLost = 2 };

// A series of versus battles (vs_scene.c: VS_initializeVS): every battle is played, whatever the
// score; each one's result marks a win or a loss per side (VS_ROUND_WON, VS_ROUND_LOST,
// VS_STATE_WINS, VS_STATE_LOSSES), and a battle both lost is a loss for each.
class Series {
public:
  explicit Series(std::int32_t battles);

  // The battle to play next, from 0; the count once all are played.
  [[nodiscard]] std::int32_t round() const { return static_cast<std::int32_t>(results_.size()); }
  [[nodiscard]] std::int32_t battles() const { return battles_; }
  [[nodiscard]] bool finished() const { return round() >= battles_; }
  void record(RoundResult result);

  [[nodiscard]] const std::vector<RoundResult>& results() const { return results_; }
  [[nodiscard]] std::int32_t wins(Side side) const;
  [[nodiscard]] std::int32_t losses(Side side) const;
  // Whether `side` won the battle `round` (the scoreboard's circle; a cross otherwise).
  [[nodiscard]] bool wonRound(Side side, std::int32_t round) const;
  // The side with more wins once the series is over; none for a draw or while it goes on.
  [[nodiscard]] std::optional<Side> winner() const;

private:
  std::int32_t battles_;
  std::vector<RoundResult> results_;
};

} // namespace dwe::game::versus
