#pragma once

#include <dwe/game/gym/gym_rules.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace dwe::game::gym {

class GymPorts;

// The roulette over a course (trn_slots.c: TRN_SLOT_MACHINE): the player stops three reels one
// by one, and the line they show sets the rate of the gains. A hidden assist, rolled per session,
// nudges the stops towards (or away from) a match.
class SlotMachine {
public:
  static constexpr std::size_t Reels = 3;

  // TRN_createSlotMachineBox (TRN2_createSlotMachineBox: the same, with equal reels and assist
  // rolls): `symbol` is the course's own symbol, which pays more. The rules must outlive the
  // machine.
  SlotMachine(const SlotRules& rules, std::int32_t symbol, GymPorts& ports);

  // TRN_startSlotSpin (and TRN2_startSlotSpin): the reels start without a press once the partner
  // is at the machine.
  void startSpin() { autoStart_ = true; }
  // TRN_tickSlotMachine: one frame; `confirm` is the button newly pressed.
  void tick(GymPorts& ports, bool confirm);

  // TRN_getSlotSessionResult (and TRN2_getSlotSessionResult): the rate in tenths once the player
  // saw the line and the box shrank away (TRN_tickSlotSession hands TRN_getSlotMachineResult
  // over then).
  [[nodiscard]] std::optional<std::int32_t> result() const { return result_; }
  // The symbol a reel shows `row` cells below its line (negative: above).
  [[nodiscard]] std::int32_t symbolAt(std::size_t reel, std::int32_t row) const;
  // TRN_renderSlotMachine: how far down, in pixels, a reel is drawn from its resting place.
  [[nodiscard]] std::int32_t drawnScroll(std::size_t reel) const;
  // Whether the box stands (it grew in and the player has not seen the line through yet).
  [[nodiscard]] bool standing() const { return growing_ == 0 && state_ < 7; }
  // Whether the box shrinks away: the line was seen.
  [[nodiscard]] bool closing() const { return state_ >= 7; }
  // The rate the line paid, once the reels stopped.
  [[nodiscard]] std::optional<std::int32_t> payout() const { return payout_; }

private:
  enum class Assist : std::uint8_t { None, Match, Jackpot };

  void spinFrame(GymPorts& ports, bool confirm);
  void pay(GymPorts& ports);
  // TRN_chooseReelStop (and TRN2_chooseReelStop): where the reel the player just stopped settles.
  void chooseReelStop(std::size_t reel, GymPorts& ports);
  // Aims the reel at `target` when it is one or two cells away; otherwise gives up on the reel.
  void scanFor(std::size_t reel, std::int32_t target);
  // Aims the reel at the symbol it stops on now.
  void aimAtLine(std::size_t reel);
  [[nodiscard]] std::int32_t symbolAhead(std::size_t reel, std::int32_t cells) const;
  [[nodiscard]] std::int32_t step(std::size_t reel) const;
  [[nodiscard]] std::int32_t lineSymbol(std::size_t reel) const;

  const SlotRules* rules_;
  std::int32_t symbol_;
  Assist assist_;
  // 0 waits for the start, 1 to 4 spin with one more reel told to stop each press, 5 shows the
  // line, 6 closes the box, 7 waits for it to shrink away.
  std::int32_t state_ = 0;
  // The frames the box still grows in (the creating frame draws the first), and shrinks in.
  std::int32_t growing_ = 4;
  std::int32_t shrinking_ = 0;
  bool autoStart_ = false;
  std::array<std::int32_t, Reels> position_{};
  std::array<std::int32_t, Reels> scroll_{};
  // In pixels per frame.
  std::array<std::int32_t, Reels> speed_{};
  std::array<std::int32_t, Reels> stopSteps_{};
  std::array<bool, Reels> settling_{};
  // The symbol the assist aims a reel at; none when it gave up on that reel.
  std::array<std::optional<std::int32_t>, Reels> target_{};
  std::int32_t resultTimer_ = 0;
  std::optional<std::int32_t> payout_;
  std::optional<std::int32_t> result_;
};

} // namespace dwe::game::gym
