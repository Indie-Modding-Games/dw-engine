#include <dwe/game/battle/status_visuals.hpp>

#include <algorithm>

namespace dwe::game::battle {

namespace {

// rand() % 100 - 50: an offset of up to 50 units either way.
constexpr std::int32_t BubbleSpread = 100;
constexpr std::int32_t BubbleSpreadHalf = 50;
// BTL_tickConfusionEffect: the circle turns 7 and each object spins 0x5B a frame (4096 a turn).
constexpr std::int32_t ConfusionTurn = 7;
constexpr std::int32_t ConfusionSpin = 0x5B;
// The original's rand() gives 15 bits; the battle's own generator is cut to the same.
constexpr std::uint32_t RandMask = 0x7FFF;

std::int32_t rand15(ruleset::Random& random) {
  return static_cast<std::int32_t>(random.next() & RandMask);
}

template <typename Pool>
auto firstFree(Pool& pool) {
  return std::ranges::find_if(pool, [](const auto& slot) { return !slot.has_value(); });
}

} // namespace

void StatusVisuals::show(ruleset::Random& random, FighterId fighter, Kind kind,
                         std::int32_t stunFrames) {
  if (shown_.contains(fighter)) {
    return;
  }
  // Each add takes the first free slot of its pool, or none (statusFxId -1) when it is full.
  std::optional<std::size_t> slot;
  switch (kind) {
  case Kind::Poison:
    // battle_effect.c: BTL_addPoisonEffect: the first of the 4 slots, its frame 0.
    if (const auto free = firstFree(poisons_); free != poisons_.end()) {
      *free = Poison{.fighter = fighter, .frame = 0};
      slot = static_cast<std::size_t>(free - poisons_.begin());
    }
    break;
  case Kind::Confusion:
    if (const auto free = firstFree(confusions_); free != confusions_.end()) {
      Confusion confusion{.fighter = fighter, .angle = 0, .spin = 0, .offsets = {}};
      for (std::int32_t& offset : confusion.offsets) {
        offset = rand15(random);
      }
      *free = confusion;
      slot = static_cast<std::size_t>(free - confusions_.begin());
    }
    break;
  case Kind::Stun:
    if (const auto free = firstFree(stuns_); free != stuns_.end()) {
      *free = Stun{
          .fighter = fighter, .stage = StunStage::Countdown, .frame = 0, .duration = stunFrames};
      slot = static_cast<std::size_t>(free - stuns_.begin());
    }
    break;
  }
  if (slot) {
    shown_[fighter] = *slot;
  }
}

void StatusVisuals::hide(FighterId fighter, Kind kind) {
  const auto found = shown_.find(fighter);
  if (found == shown_.end()) {
    return;
  }
  const std::size_t slot = found->second;
  shown_.erase(found);
  // The kind's pool at the fighter's index, if that slot is still the fighter's.
  const auto release = [fighter, slot](auto& pool) {
    if (slot < pool.size() && pool[slot] && pool[slot]->fighter == fighter) {
      pool[slot].reset();
    }
  };
  switch (kind) {
  case Kind::Poison:
    release(poisons_);
    break;
  case Kind::Confusion:
    release(confusions_);
    break;
  case Kind::Stun:
    release(stuns_);
    break;
  }
}

void StatusVisuals::tick(ruleset::Random& random, std::span<const Fighter> fighters) {
  // BTL_tickPoisonEffect: a bubble on the first frame of every 30.
  for (std::optional<Poison>& poison : poisons_) {
    if (!poison) {
      continue;
    }
    poison->frame = (poison->frame + 1) % PoisonCycle;
    if (poison->frame == 1) {
      addBubble(random, poison->fighter);
    }
  }
  for (std::optional<Confusion>& confusion : confusions_) {
    if (confusion) {
      confusion->angle += ConfusionTurn;
      confusion->spin += ConfusionSpin;
    }
  }
  for (std::size_t slot = 0; slot < stuns_.size(); ++slot) {
    if (stuns_.at(slot)) {
      tickStun(slot);
    }
  }
  // BTL_tickPoisonBubble, BTL_tickStunSubEffect: they end after their frames. The ones just added
  // tick too, as world objects added during the frame's ticks do.
  for (std::optional<Bubble>& bubble : bubbles_) {
    if (bubble && ++bubble->frame >= BubbleFrames) {
      bubble.reset();
    }
  }
  for (std::optional<StunRing>& ring : stunRings_) {
    if (ring && ++ring->frame >= StunRingFrames) {
      ring.reset();
    }
  }
  tickEffects(random, fighters);
}

void StatusVisuals::clear() {
  poisons_.fill(std::nullopt);
  bubbles_.fill(std::nullopt);
  confusions_.fill(std::nullopt);
  stuns_.fill(std::nullopt);
  stunRings_.fill(std::nullopt);
  shown_.clear();
  finisherAuras_.fill(std::nullopt);
  itemBursts_.fill(std::nullopt);
  buffDisk_.reset();
  buffTrails_.reset();
  buffRings_.reset();
}

std::optional<std::size_t> StatusVisuals::shownBy(FighterId fighter) const {
  const auto found = shown_.find(fighter);
  return found != shown_.end() ? std::optional{found->second} : std::nullopt;
}

std::size_t StatusVisuals::poisons() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(poisons_, [](const auto& slot) { return slot.has_value(); }));
}

// BTL_addPoisonBubble: with a free slot, two rolls for its offsets.
void StatusVisuals::addBubble(ruleset::Random& random, FighterId fighter) {
  const auto free = firstFree(bubbles_);
  if (free == bubbles_.end()) {
    return;
  }
  const std::int32_t offsetX = (rand15(random) % BubbleSpread) - BubbleSpreadHalf;
  const std::int32_t offsetZ = (rand15(random) % BubbleSpread) - BubbleSpreadHalf;
  *free = Bubble{.fighter = fighter, .frame = 0, .offsetX = offsetX, .offsetZ = offsetZ};
}

// BTL_tickStunEffect: a ring every 6 frames until the sign's frames run out, then 8 frames of
// flashing and 5 of shrinking, and it ends by itself (the fighter keeps its statusFxId).
void StatusVisuals::tickStun(std::size_t slot) {
  Stun& stun = *stuns_.at(slot);
  ++stun.frame;
  switch (stun.stage) {
  case StunStage::Countdown:
    if (stun.frame % StunRingEvery == 0) {
      if (const auto free = firstFree(stunRings_); free != stunRings_.end()) {
        *free = StunRing{.fighter = stun.fighter, .frame = 0};
      }
    }
    if (stun.frame >= stun.duration) {
      stun.stage = StunStage::Flash;
      stun.frame = 1;
    }
    break;
  case StunStage::Flash:
    if (stun.frame >= StunFlashFrames) {
      stun.stage = StunStage::Shrink;
      stun.frame = 1;
    }
    break;
  case StunStage::Shrink:
    if (stun.frame >= StunShrinkFrames) {
      stuns_.at(slot).reset();
    }
    break;
  }
}

} // namespace dwe::game::battle
