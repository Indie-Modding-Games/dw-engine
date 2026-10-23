#include <dwe/core/log.hpp>
#include <dwe/game/gym/course_flow.hpp>
#include <dwe/game/gym/slot_machine.hpp>

#include <algorithm>
#include <cstddef>

namespace dwe::game::gym {

SlotMachine::SlotMachine(const SlotRules& rules, std::int32_t symbol, GymPorts& ports)
    : rules_(&rules), symbol_(symbol),
      assist_(static_cast<Assist>(rules.assistByRoll.at(static_cast<std::size_t>(
          ports.randomBelow(static_cast<std::int32_t>(rules.assistByRoll.size())))))) {
  for (std::size_t reel = 0; reel < Reels; ++reel) {
    position_.at(reel) = ports.randomBelow(static_cast<std::int32_t>(rules.reels.at(reel).size()));
  }
  core::logDebug("[port:gym] TRN_createSlotMachineBox (trn_slots.c): symbol {}, assist {}", symbol,
                 static_cast<int>(assist_));
}

std::int32_t SlotMachine::symbolAt(std::size_t reel, std::int32_t row) const {
  return symbolAhead(reel, -row);
}

std::int32_t SlotMachine::symbolAhead(std::size_t reel, std::int32_t cells) const {
  const auto& strip = rules_->reels.at(reel);
  const auto size = static_cast<std::int32_t>(strip.size());
  // The reel turns towards lower positions, so the cells ahead are the ones before it.
  const std::int32_t index = (((position_.at(reel) - cells) % size) + size) % size;
  return strip.at(static_cast<std::size_t>(index));
}

std::int32_t SlotMachine::lineSymbol(std::size_t reel) const {
  return symbolAhead(reel, 0);
}

std::int32_t SlotMachine::step(std::size_t reel) const {
  // A reel never rests exactly a cell further: it holds one pixel short for a frame.
  const std::int32_t next = scroll_.at(reel) + speed_.at(reel);
  return next == rules_->cellPixels ? next - 1 : next;
}

std::int32_t SlotMachine::drawnScroll(std::size_t reel) const {
  if (settling_.at(reel)) {
    return scroll_.at(reel) + rules_->settlePixels;
  }
  return step(reel);
}

void SlotMachine::tick(GymPorts& ports, bool confirm) {
  switch (state_) {
  case 0:
    if (confirm || autoStart_) {
      state_ = 1;
      speed_.fill(rules_->spinSpeed);
      stopSteps_.fill(0);
      ports.playSound(rules_->soundSlot, rules_->sounds.start);
      core::logDebug("[port:gym] TRN_tickSlotMachine (trn_slots.c): spin");
    }
    break;
  case 1:
  case 2:
  case 3:
  case 4:
    spinFrame(ports, confirm);
    break;
  case 5:
    if (resultTimer_ == 0) {
      pay(ports);
    } else if (resultTimer_ >= rules_->resultFrames || confirm) {
      state_ = 6;
    }
    ++resultTimer_;
    break;
  default:
    result_ = payout_;
    break;
  }
}

void SlotMachine::spinFrame(GymPorts& ports, bool confirm) {
  // The reels before this one were told to stop; this one stops on this press.
  const auto pressed = static_cast<std::size_t>(state_ - 1);
  for (std::size_t reel = 0; reel < Reels; ++reel) {
    settling_.at(reel) = false;
    scroll_.at(reel) = step(reel);
    if (scroll_.at(reel) >= rules_->cellPixels) {
      scroll_.at(reel) -= rules_->cellPixels;
      const auto size = static_cast<std::int32_t>(rules_->reels.at(reel).size());
      position_.at(reel) = (position_.at(reel) + size - 1) % size;
      if (reel < pressed) {
        if (stopSteps_.at(reel) > 0) {
          --stopSteps_.at(reel);
        } else {
          scroll_.at(reel) = 0;
          speed_.at(reel) = 0;
          ports.playSound(rules_->soundSlot, rules_->sounds.stop);
          core::logDebug("[port:gym] TRN_tickSlotMachine (trn_slots.c): reel {} on {}", reel,
                         lineSymbol(reel));
        }
      }
    }
    if (reel == pressed && confirm) {
      chooseReelStop(reel, ports);
    }
    if (step(reel) >= rules_->cellPixels && stopSteps_.at(reel) <= 0 &&
        (reel < pressed || (reel == pressed && confirm))) {
      settling_.at(reel) = true;
    }
  }
  if (state_ < 4 && confirm) {
    ports.playSound(rules_->soundSlot, rules_->sounds.press);
    ++state_;
  }
  if (std::ranges::all_of(speed_, [](std::int32_t speed) { return speed == 0; })) {
    resultTimer_ = 0;
    ++state_;
  }
}

void SlotMachine::pay(GymPorts& ports) {
  const std::int32_t first = lineSymbol(0);
  const bool triple = first == lineSymbol(1) && first == lineSymbol(2);
  const SlotPayouts& payouts = rules_->payouts;
  if (!triple) {
    payout_ = payouts.miss;
    ports.playSound(rules_->soundSlot, rules_->sounds.miss);
  } else if (first == rules_->jackpotSymbol) {
    payout_ = payouts.jackpot;
    ports.playSound(rules_->soundSlot, rules_->sounds.jackpot);
  } else {
    payout_ = first == symbol_ ? payouts.ownSymbol : payouts.triple;
    ports.playSound(rules_->soundSlot, rules_->sounds.triple);
  }
  core::logDebug("[port:gym] TRN_tickSlotMachine (trn_slots.c): line {} {} {}, pays {}", first,
                 lineSymbol(1), lineSymbol(2), *payout_);
}

void SlotMachine::aimAtLine(std::size_t reel) {
  // Between two cells the reel still rolls onto the next one.
  target_.at(reel) = symbolAhead(reel, scroll_.at(reel) == 0 ? 0 : 1);
}

void SlotMachine::scanFor(std::size_t reel, std::int32_t target) {
  for (std::int32_t cells = 1; cells <= 2; ++cells) {
    if (symbolAhead(reel, cells) == target) {
      stopSteps_.at(reel) = cells - 1;
      target_.at(reel) = target;
      return;
    }
  }
  stopSteps_.at(reel) = 0;
  target_.at(reel).reset();
}

void SlotMachine::chooseReelStop(std::size_t reel, GymPorts& ports) {
  const std::int32_t jackpot = rules_->jackpotSymbol;
  const std::optional<std::int32_t>& first = target_.at(0);
  switch (reel) {
  case 0:
    switch (assist_) {
    case Assist::None:
      target_.at(reel) = symbolAhead(reel, 1);
      break;
    case Assist::Match:
      aimAtLine(reel);
      break;
    case Assist::Jackpot:
      scanFor(reel, jackpot);
      break;
    }
    break;
  case 1:
    switch (assist_) {
    case Assist::None:
      target_.at(reel) = symbolAhead(reel, 1);
      break;
    case Assist::Match:
      if (first == jackpot) {
        aimAtLine(reel);
      } else if (first) {
        scanFor(reel, *first);
      }
      break;
    case Assist::Jackpot:
      if (first == jackpot) {
        scanFor(reel, jackpot);
      } else {
        target_.at(reel).reset();
      }
      break;
    }
    break;
  default:
    // The last reel: a near miss is pushed past the match on purpose, while the assist completes
    // a pair it aimed at.
    switch (assist_) {
    case Assist::None:
      if (first && first == target_.at(1) && *first == symbolAhead(reel, 1)) {
        stopSteps_.at(reel) = ports.randomBelow(2) + 1;
      }
      break;
    case Assist::Match:
      if (!first || first != target_.at(1)) {
        break;
      }
      if (*first != jackpot) {
        scanFor(reel, *first);
      } else if (symbolAhead(reel, 1) == jackpot) {
        stopSteps_.at(reel) = ports.randomBelow(2) + 1;
      }
      break;
    case Assist::Jackpot:
      if (first == jackpot && target_.at(1) == jackpot) {
        scanFor(reel, jackpot);
      } else {
        target_.at(reel).reset();
      }
      break;
    }
    break;
  }
  core::logDebug("[port:gym] TRN_chooseReelStop (trn_slots.c): reel {}, {} more cells", reel,
                 stopSteps_.at(reel));
}

} // namespace dwe::game::gym
