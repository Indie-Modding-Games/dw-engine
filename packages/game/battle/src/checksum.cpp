#include <dwe/game/battle/battle.hpp>

#include <cstdint>
#include <optional>
#include <type_traits>

namespace dwe::game::battle {

namespace {

// FNV-1a over the values, one by one as 64-bit numbers, so the hash does not depend on padding or
// on the machine's byte order.
class Hasher {
public:
  template <typename T>
    requires std::is_integral_v<T> || std::is_enum_v<T>
  void add(T value) {
    auto bits = static_cast<std::uint64_t>(value);
    for (int byte = 0; byte < 8; ++byte) {
      hash_ ^= bits & 0xFFU;
      hash_ *= Prime;
      bits >>= 8U;
    }
  }

  template <typename T>
  void add(const std::optional<T>& value) {
    add(value.has_value());
    if (value) {
      add(*value);
    }
  }

  void add(Point point) {
    add(point.x);
    add(point.z);
  }

  [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
  static constexpr std::uint64_t Offset = 0xcbf29ce484222325ULL;
  static constexpr std::uint64_t Prime = 0x100000001b3ULL;
  std::uint64_t hash_ = Offset;
};

void addCommands(Hasher& hash, const CommandState& commands) {
  hash.add(commands.buffered);
  hash.add(commands.current);
  hash.add(commands.delay);
  hash.add(commands.changeTarget);
  hash.add(commands.savedChargeMode);
}

void addFighter(Hasher& hash, const Fighter& fighter) {
  hash.add(fighter.hp);
  hash.add(fighter.mp);
  hash.add(fighter.pendingDamage);
  hash.add(fighter.pendingMp);
  hash.add(fighter.position);
  hash.add(fighter.facing.x);
  hash.add(fighter.facing.z);
  hash.add(fighter.rootRemainder);
  hash.add(fighter.pose);
  hash.add(fighter.poseFrames);
  hash.add(fighter.target);
  hash.add(fighter.chasingAttacker);
  hash.add(fighter.chargeMode);
  hash.add(fighter.planned);
  hash.add(fighter.queuedMove);
  hash.add(fighter.moveRange);
  hash.add(fighter.outOfRangeFrames);
  hash.add(fighter.chargingUp);
  hash.add(fighter.coolingDown);
  hash.add(fighter.cooldown);
  hash.add(fighter.disobeyFrames);
  hash.add(fighter.disobeying);
  hash.add(fighter.attack.has_value());
  if (fighter.attack) {
    hash.add(fighter.attack->move);
    hash.add(fighter.attack->target);
    hash.add(fighter.attack->frame);
    hash.add(fighter.attack->released);
  }
  hash.add(fighter.knockedBack);
  hash.add(fighter.blocking);
  hash.add(fighter.struck);
  hash.add(fighter.invulnerableFrames);
  hash.add(fighter.incoming.size());
  const StatusState& status = fighter.status;
  hash.add(status.poisoned);
  hash.add(status.poisonFrames);
  hash.add(status.confused);
  hash.add(status.confusionFrames);
  hash.add(status.stunned);
  hash.add(status.stunFrames);
  hash.add(status.flattened);
  hash.add(status.flatFrames);
  hash.add(fighter.speedReserve);
  hash.add(fighter.buffsLeft);
  hash.add(fighter.buffPriority);
  hash.add(fighter.finisherProgress);
  hash.add(fighter.fainted);
  hash.add(fighter.shown.clip);
  hash.add(fighter.shown.frame);
}

} // namespace

std::uint64_t Battle::checksum() const {
  Hasher hash;
  hash.add(state_.frame);
  hash.add(random_.state());
  hash.add(result_.has_value());
  for (const Fighter& fighter : state_.fighters) {
    addFighter(hash, fighter);
  }
  addCommands(hash, state_.commands);
  if (state_.rival) {
    addCommands(hash, state_.rival->commands);
  }
  hash.add(state_.finisher.fighter);
  hash.add(state_.finisher.auraFrames);
  hash.add(state_.finisher.charge);
  hash.add(state_.inFlight.size());
  for (const AttackInFlight& attack : state_.inFlight) {
    hash.add(attack.attacker);
    hash.add(attack.defender);
    hash.add(attack.move);
    hash.add(attack.framesLeft);
  }
  hash.add(state_.sharedCooldown);
  hash.add(state_.rivalSharedCooldown);
  hash.add(state_.hitsOff);
  if (state_.matchClock) {
    hash.add(state_.matchClock->seconds);
    hash.add(state_.matchClock->frames);
  }
  return hash.value();
}

} // namespace dwe::game::battle
