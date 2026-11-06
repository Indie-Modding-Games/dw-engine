#include <dwe/game/battle/effect_runner.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::game::battle {

namespace {

using assets::EffectComparison;
using assets::EffectInstruction;
using assets::EffectOpcode;
using assets::EffectOperator;
using assets::EffectVariable;
using assets::EffectWidth;

// The held sounds the original tracks at once (battle_effect.c: EFE_SOUND_DATA).
constexpr std::size_t HeldSoundCount = 10;
// The sounds BTL_playEFESound plays with playSound2 and keeps the voice of.
constexpr std::array<std::int32_t, 4> HeldSounds = {0x12, 0x20, 0x21, 0x22};
// An instance whose frame is -1 is free (battle_effect.c: EfeInstance.frame).
constexpr std::int32_t FreeFrame = -1;
// The original loops until a script stops; a script that never does would hang it. The runner gives
// up after this many instructions in one run.
constexpr std::size_t MaxStepsPerRun = std::size_t{1} << 16U;
// rand() of the original's C library.
constexpr std::uint32_t RandRange = 0x8000;
constexpr std::int32_t NoAddress = std::numeric_limits<std::int32_t>::min();
constexpr std::uint32_t ByteBits = 8;
constexpr std::uint32_t ShiftMask = 31;

// The sub-ops whose effect the runner keeps: sounds, sub-effect scripts, random numbers and the hit
// checks, which branches read.
constexpr std::string_view PlaySound = "play_sound";
constexpr std::string_view SetSubEffectScripts = "set_sub_effect_scripts";
constexpr std::string_view RandomInRange = "random_in_range";
constexpr std::string_view IsTargetUnhit = "is_target_unhit";
constexpr std::string_view HitTarget = "hit_target";
constexpr std::string_view CheckTargetCollision = "check_target_collision";
// The hit checks that register a hit, and where their `hit` argument is.
constexpr std::array<std::string_view, 4> RegisteringHits = {"check_collision", "apply_box_hit",
                                                             "apply_radius_hit", "apply_line_hit"};
constexpr std::size_t RegisteringHitArg = 1;
constexpr std::size_t TargetCollisionHitArg = 2;

// BTL_EFE_COMPARISONS: the variable, widened with its sign, against the register; the branch is
// taken when the comparison returns 0 (BTL_compareEqual, BTL_compareNotEqual, BTL_compareLess,
// BTL_compareLessOrEqual, BTL_compareGreater, BTL_compareGreaterOrEqual).
bool holds(EffectComparison comparison, std::int32_t variable, std::int32_t value) {
  switch (comparison) {
  case EffectComparison::Equal:
    return variable == value;
  case EffectComparison::NotEqual:
    return variable != value;
  case EffectComparison::Less:
    return variable < value;
  case EffectComparison::LessOrEqual:
    return variable <= value;
  case EffectComparison::Greater:
    return variable > value;
  case EffectComparison::GreaterOrEqual:
    return variable >= value;
  }
  return false;
}

// The processor's divide: by zero it answers -1 for a dividend from 0 up and 1 below it, with the
// dividend as the remainder; the lowest number by -1 stays itself, remainder 0.
std::int32_t quotient(std::int32_t dividend, std::int32_t divisor) {
  if (divisor == 0) {
    return dividend >= 0 ? -1 : 1;
  }
  return static_cast<std::int32_t>(static_cast<std::int64_t>(dividend) / divisor);
}
std::int32_t remainder(std::int32_t dividend, std::int32_t divisor) {
  if (divisor == 0) {
    return dividend;
  }
  return static_cast<std::int32_t>(static_cast<std::int64_t>(dividend) % divisor);
}

// variable <op>= register for one row of BTL_EFE_VARIABLE_OPERATORS (BTL_setInt8Variable,
// BTL_addInt8Variable, BTL_subtractInt8Variable, BTL_multiplyInt8Variable, BTL_divideInt8Variable,
// BTL_moduloInt8Variable, BTL_shiftLeftInt8Variable, BTL_shiftRightInt8Variable; the same eight
// for 16 bits: BTL_setInt16Variable, BTL_addInt16Variable, BTL_subtractInt16Variable,
// BTL_multiplyInt16Variable, BTL_divideInt16Variable, BTL_moduloInt16Variable,
// BTL_shiftLeftInt16Variable, BTL_shiftRightInt16Variable; and for 32: BTL_setInt32Variable,
// BTL_addInt32Variable, BTL_subtractInt32Variable, BTL_multiplyInt32Variable,
// BTL_divideInt32Variable, BTL_moduloInt32Variable, BTL_shiftLeftInt32Variable,
// BTL_shiftRightInt32Variable). The variable comes widened with its sign and the caller keeps its
// low bytes. The 16-bit operators cut the register to 16 bits first, which only a divide or a
// remainder can tell (a register of 0x10000 divides by zero); the 8-bit ones use it whole. A shift
// takes the register's low five bits.
std::int32_t apply(EffectOperator op, EffectWidth width, std::int32_t variable,
                   std::int32_t value) {
  if (width == EffectWidth::Int16) {
    value = static_cast<std::int16_t>(value);
  }
  const auto bits = static_cast<std::uint32_t>(variable);
  const auto other = static_cast<std::uint32_t>(value);
  switch (op) {
  case EffectOperator::Set:
    return value;
  case EffectOperator::Add:
    return static_cast<std::int32_t>(bits + other);
  case EffectOperator::Subtract:
    return static_cast<std::int32_t>(bits - other);
  case EffectOperator::Multiply:
    return static_cast<std::int32_t>(bits * other);
  case EffectOperator::Divide:
    return quotient(variable, value);
  case EffectOperator::Modulo:
    return remainder(variable, value);
  case EffectOperator::ShiftLeft:
    return static_cast<std::int32_t>(bits << (other & ShiftMask));
  case EffectOperator::ShiftRight:
    return variable >> (other & ShiftMask);
  }
  return variable;
}

// rng.c: customRandom: a value from min to max, both included.
std::int32_t randomInRange(std::minstd_rand& random, std::int32_t min, std::int32_t max) {
  if (max == min) {
    return min;
  }
  if (max < min) {
    std::swap(min, max);
  }
  const auto span = static_cast<std::uint64_t>(static_cast<std::int64_t>(max) - min) + 1;
  return static_cast<std::int32_t>(min + static_cast<std::int64_t>(random() % span));
}

} // namespace

// The held sounds start free (battle_effect.c: BTL_clearEFESoundChannels, which the effect engine's
// setup calls).
EffectRunner::EffectRunner(EffectSounds& sounds, std::uint32_t seed)
    : sounds_(&sounds), random_(seed), held_(HeldSoundCount) {}

std::int32_t EffectRunner::customRandom(std::int32_t min, std::int32_t max) {
  return randomInRange(random_, min, max);
}

std::uint32_t EffectRunner::randomDraw() {
  if (rolls_ != nullptr) {
    return rolls_->next() % RandRange;
  }
  return random_() % RandRange;
}

std::size_t EffectRunner::load(assets::EffectScript script) {
  Slot slot;
  slot.data = script.data;
  slot.subEffects.resize(script.subEffectCount);
  slot.uvAnims.resize(script.modelObjects);
  slot.objectColors.resize(script.modelObjects);
  slot.script = std::move(script);
  slots_.push_back(std::move(slot));
  const std::size_t index = slots_.size() - 1;
  if (slots_[index].subEffects.empty()) {
    // Nothing for its scripts to run as (the original would write past its sub-effects): it never
    // starts.
    slots_[index].failed = true;
    return index;
  }
  // battle_effect.c: BTL_setupLoadedEFEFile runs the init script from sub-effect 0, with no
  // instance, a few instructions a frame while the slot loads; it has no sounds to time.
  subEffect_ = 0;
  current_.reset();
  previous_.reset();
  activeSection_.reset();
  startSource_.reset();
  startTarget_.reset();
  run(index, slots_[index].script.initScript);
  return index;
}

std::optional<std::size_t> EffectRunner::start(std::size_t slot, std::optional<FighterId> source,
                                               std::optional<FighterId> target) {
  if (slot >= slots_.size() || slots_[slot].failed) {
    return std::nullopt;
  }
  Slot& effect = slots_[slot];
  effect.hitPending = false;
  effect.hitSinceStart = false;
  effect.hitTargets.clear();
  subEffect_ = 0;
  current_.reset();
  previous_.reset();
  activeSection_.reset();
  startSource_ = source;
  startTarget_ = target;
  run(slot, effect.script.startScript);
  return activeSection_;
}

void EffectRunner::stop(std::size_t slot, std::size_t section) {
  if (slot >= slots_.size() || section >= slots_[slot].subEffects.size()) {
    return;
  }
  SubEffect& subEffect = slots_[slot].subEffects[section];
  subEffect.tick.reset();
  setFrame(slots_[slot], subEffect.instance, FreeFrame);
}

void EffectRunner::tick() {
  fixedCamera_ = false;
  mapHidden_ = false;
  for (std::size_t slot = 0; slot < slots_.size(); ++slot) {
    for (std::size_t index = 0; index < slots_[slot].subEffects.size(); ++index) {
      const SubEffect& subEffect = slots_[slot].subEffects[index];
      if (!subEffect.tick) {
        continue;
      }
      subEffect_ = index;
      current_ = subEffect.instance;
      previous_ = subEffect.parent;
      setFrame(slots_[slot], current_, frameOf(slots_[slot], current_) + 1);
      run(slot, *subEffect.tick);
    }
    tickUvAnims(slots_[slot]);
  }
  // The flight probe draws nothing: its emitters, clouds and flashes would only spend its numbers.
  if (!touchBodies_) {
    tickFx();
  }
  for (std::optional<HeldSound>& held : held_) {
    if (held &&
        frameOf(slots_[held->slot], slots_[held->slot].subEffects[held->subEffect].instance) < 0) {
      sounds_->stop(held->voice);
      held.reset();
    }
  }
}

void EffectRunner::noteHit(std::size_t slot) {
  if (slot < slots_.size()) {
    slots_[slot].hitPending = true;
  }
}

// BTL_stopEFESounds only ever looks at the first entry (it never moves its pointer); the runner
// stops them all, so no held sound outlives the battle.
void EffectRunner::stopAll() {
  for (std::optional<HeldSound>& held : held_) {
    if (held) {
      sounds_->stop(held->voice);
      held.reset();
    }
  }
}

// battle_effect.c: BTL_renderEFEEngine: every sub-effect with a tick script runs its render script.
void EffectRunner::render() {
  draws_.clear();
  for (std::size_t slot = 0; slot < slots_.size(); ++slot) {
    for (std::size_t index = 0; index < slots_[slot].subEffects.size(); ++index) {
      const SubEffect& subEffect = slots_[slot].subEffects[index];
      if (!subEffect.tick || !subEffect.render) {
        continue;
      }
      subEffect_ = index;
      current_ = subEffect.instance;
      previous_ = subEffect.parent;
      run(slot, *subEffect.render);
    }
  }
  renderFx();
}

bool EffectRunner::running(std::size_t slot) const {
  return slot < slots_.size() &&
         std::ranges::any_of(
             slots_[slot].subEffects,
             [](const SubEffect& subEffect) { return subEffect.tick.has_value(); });
}

const EffectPalettes& EffectRunner::palettes(std::size_t slot) const {
  return slots_.at(slot).palettes;
}

// battle_effect.c: BTL_runEFEScript.
void EffectRunner::run(std::size_t slot, std::uint32_t script) {
  slot_ = slot;
  Slot& target = slots_[slot];
  stack_.clear();
  calls_.clear();
  aborted_ = false;
  std::optional<std::size_t> at = target.script.instructionAt(script);
  for (std::size_t steps = 0; at && !aborted_ && steps < MaxStepsPerRun; ++steps) {
    at = step(target, *at);
  }
  if (subEffect_ < target.subEffects.size()) {
    SubEffect& subEffect = target.subEffects[subEffect_];
    if (subEffect.instance && frameOf(target, subEffect.instance) == FreeFrame) {
      subEffect.tick.reset();
    }
  }
}

// One instruction (battle_effect.c: BTL_dispatchEFEOpcode: the handler of BTL_jtbl_80073604 its
// opcode, the instruction's low byte, names). Returns the next one, or nothing when the script
// stops.
std::optional<std::size_t> EffectRunner::step(Slot& slot, std::size_t at) {
  const EffectInstruction& in = slot.script.instructions[at];
  const std::size_t next = at + 1;
  const auto pop = [this] {
    if (stack_.empty()) {
      return std::int32_t{0};
    }
    const std::int32_t value = stack_.back();
    stack_.pop_back();
    return value;
  };
  const auto jump = [&slot](std::int32_t target) {
    return slot.script.instructionAt(static_cast<std::uint32_t>(target));
  };
  switch (in.opcode) {
  case EffectOpcode::LoadImmediate:
    register_ = in.value;
    return next;
  case EffectOpcode::LoadVariable:
    register_ = read(slot, address(in.variable), in.width);
    return next;
  case EffectOpcode::LoadRandom:
    register_ = static_cast<std::int32_t>(randomDraw());
    return next;
  case EffectOpcode::LoadIndexed: {
    const std::int32_t index = read(slot, address(in.variable), in.indexWidth);
    register_ = read(slot, in.target + (index * static_cast<std::int32_t>(in.width)), in.width);
    return next;
  }
  case EffectOpcode::ApplyOperator: {
    const std::int32_t where = address(in.variable);
    write(slot, where, in.width, apply(in.op, in.width, read(slot, where, in.width), register_));
    return next;
  }
  case EffectOpcode::BranchIf:
    if (holds(in.comparison, read(slot, address(in.variable), in.width), register_)) {
      return jump(in.target);
    }
    return next;
  case EffectOpcode::Jump:
    return jump(in.target);
  case EffectOpcode::PushImmediate:
    stack_.push_back(in.value);
    return next;
  case EffectOpcode::PushVariable:
    stack_.push_back(read(slot, address(in.variable), in.width));
    return next;
  case EffectOpcode::PushAddress:
    stack_.push_back(address(in.variable));
    return next;
  case EffectOpcode::Call:
    calls_.push_back({.returnTo = next, .subEffect = subEffect_});
    return jump(in.target);
  case EffectOpcode::SubOp:
    subOp(slot, in);
    return next;
  case EffectOpcode::Return:
    return returnFrom();
  case EffectOpcode::PopToVariable:
    write(slot, address(in.variable), EffectWidth::Int32, pop());
    return next;
  case EffectOpcode::Spawn:
    return spawn(slot, in, next);
  case EffectOpcode::Stop:
  case EffectOpcode::Stop6:
  case EffectOpcode::Stop8:
    return std::nullopt;
  }
  return std::nullopt;
}

// battle_effect.c: BTL_returnFromEFESubroutine: back to the caller and its sub-effect, or the end
// of the script when nothing called it.
std::optional<std::size_t> EffectRunner::returnFrom() {
  if (calls_.empty()) {
    return std::nullopt;
  }
  const CallFrame frame = calls_.back();
  calls_.pop_back();
  subEffect_ = frame.subEffect;
  const SubEffect& subEffect = slots_[slot_].subEffects[subEffect_];
  current_ = subEffect.instance;
  previous_ = subEffect.parent;
  return frame.returnTo;
}

// battle_effect.c: BTL_spawnEFESubEffect: the first free instance of the table and the first idle
// sub-effect from the running one on; the script goes on as that sub-effect. With either missing it
// returns as from a call.
std::optional<std::size_t> EffectRunner::spawn(Slot& slot, const EffectInstruction& in,
                                               std::size_t next) {
  std::optional<std::uint32_t> instance;
  std::size_t index = 0;
  for (; std::cmp_less(index, in.count); ++index) {
    const auto at =
        static_cast<std::uint32_t>(in.target + (static_cast<std::int32_t>(index) * in.stride));
    if (frameOf(slot, at) == FreeFrame) {
      instance = at;
      break;
    }
  }
  if (!instance) {
    return returnFrom();
  }
  if (!activeSection_) {
    activeSection_ = index;
  }
  std::size_t free = subEffect_;
  while (free < slot.subEffects.size() && slot.subEffects[free].tick) {
    ++free;
  }
  if (free >= slot.subEffects.size()) {
    return returnFrom();
  }
  const std::optional<FighterId> source = slot.subEffects[subEffect_].source;
  const std::optional<FighterId> target = slot.subEffects[subEffect_].target;
  subEffect_ = free;
  previous_ = current_;
  current_ = instance;
  SubEffect& spawned = slot.subEffects[free];
  spawned.instance = current_;
  spawned.parent = previous_;
  // The first spawn of a start takes the attacker and its target; the others their parent's.
  spawned.source = previous_ ? source : startSource_;
  spawned.target = previous_ ? target : startTarget_;
  targetCursor_ = 0;
  return next;
}

void EffectRunner::subOp(Slot& slot, const EffectInstruction& in) {
  std::vector<std::int32_t> args(in.argCount);
  for (std::size_t k = args.size(); k-- > 0;) {
    if (!stack_.empty()) {
      args[k] = stack_.back();
      stack_.pop_back();
    }
  }
  if (transformOp(slot, in.sub, args) || vectorOp(slot, in.sub, args) ||
      drawOp(slot, in.sub, args) || primOp(slot, in.sub, args) || quadOp(slot, in.sub, args) ||
      fighterOp(slot, in.sub, args) || (touchBodies_ && touchOp(slot, in.sub, args)) ||
      (!touchBodies_ && fxOp(slot, in.sub, args))) {
    return;
  }
  if (in.sub == PlaySound && !args.empty()) {
    playSound(args[0]);
  } else if (in.sub == SetSubEffectScripts && args.size() == 2) {
    // battle_effect.c: BTL_initializeSubEffectInstructions.
    setFrame(slot, current_, 0);
    SubEffect& subEffect = slot.subEffects[subEffect_];
    subEffect.tick = static_cast<std::uint32_t>(args[0]);
    subEffect.render = static_cast<std::uint32_t>(args[1]);
  } else if (in.sub == RandomInRange && args.size() == 3) {
    write(slot, args[0], EffectWidth::Int32, randomInRange(random_, args[1], args[2]));
  } else if (in.sub == IsTargetUnhit && !args.empty()) {
    // battle_effect.c: BTL_isTargetUnhit reads the target's isHit, which hit_target sets.
    const std::optional<FighterId> target = context(slot).target;
    const bool marked = target && std::ranges::contains(slot.hitTargets, *target);
    write(slot, args[0], EffectWidth::Int32, slot.hitSinceStart || marked ? 0 : 1);
  } else if (in.sub == HitTarget) {
    // battle_effect.c: BTL_addAttackObjectToTarget marks the target hit; the damage stays with
    // the battle, which already lands the move's hit.
    const std::optional<FighterId> target = context(slot).target;
    if (target && !std::ranges::contains(slot.hitTargets, *target)) {
      slot.hitTargets.push_back(*target);
      if (touchBodies_) {
        // The hit is registered at the instance.
        touched_.push_back({.fighter = *target, .at = instancePosition(slot), .onBody = false});
      }
    }
  } else if (in.sub == CheckTargetCollision && args.size() > TargetCollisionHitArg) {
    writeHit(slot, args[TargetCollisionHitArg], false);
  } else if (std::ranges::contains(RegisteringHits, in.sub) && args.size() > RegisteringHitArg) {
    writeHit(slot, args[RegisteringHitArg], true);
  }
}

// battle_effect.c: BTL_playEFESound.
void EffectRunner::playSound(std::int32_t sound) {
  if (sound < 0) {
    return;
  }
  if (!std::ranges::contains(HeldSounds, sound)) {
    sounds_->play(slot_, sound);
    return;
  }
  const auto free = std::ranges::find_if(held_, [](const auto& held) { return !held.has_value(); });
  if (free == held_.end()) {
    return;
  }
  if (const std::optional<std::int64_t> voice = sounds_->playHeld(slot_, sound)) {
    *free = HeldSound{.voice = *voice, .slot = slot_, .subEffect = subEffect_};
  }
}

// A hit check answers 1 once the battle hit with the slot's attack; a registering check takes the
// hit, as the original's isHit flag does.
void EffectRunner::writeHit(Slot& slot, std::int32_t address, bool consume) {
  const bool hit = slot.hitPending;
  if (consume && hit) {
    slot.hitPending = false;
    slot.hitSinceStart = true;
  }
  write(slot, address, EffectWidth::Int32, hit ? 1 : 0);
}

std::int32_t EffectRunner::address(const EffectVariable& variable) const {
  if (variable.base) {
    return variable.offset;
  }
  if (!current_) {
    return NoAddress;
  }
  return static_cast<std::int32_t>(*current_) + variable.offset;
}

std::int32_t EffectRunner::read(const Slot& slot, std::int32_t address, EffectWidth width) {
  const auto size = static_cast<std::size_t>(width);
  if (address < 0 || static_cast<std::size_t>(address) + size > slot.data.size()) {
    return 0;
  }
  std::uint32_t bits = 0;
  for (std::size_t i = size; i-- > 0;) {
    bits = (bits << ByteBits) |
           std::to_integer<std::uint32_t>(slot.data[static_cast<std::size_t>(address) + i]);
  }
  const std::uint32_t shift = static_cast<std::uint32_t>(sizeof(std::uint32_t) - size) * ByteBits;
  return static_cast<std::int32_t>(bits << shift) >> shift;
}

void EffectRunner::write(Slot& slot, std::int32_t address, EffectWidth width, std::int32_t value) {
  const auto size = static_cast<std::size_t>(width);
  if (address < 0 || static_cast<std::size_t>(address) + size > slot.data.size()) {
    return;
  }
  auto bits = static_cast<std::uint32_t>(value);
  for (std::size_t i = 0; i < size; ++i) {
    slot.data[static_cast<std::size_t>(address) + i] = static_cast<std::byte>(bits);
    bits >>= ByteBits;
  }
}

std::int32_t EffectRunner::frameOf(const Slot& slot, std::optional<std::uint32_t> instance) {
  if (!instance) {
    return FreeFrame;
  }
  return read(slot, static_cast<std::int32_t>(*instance), EffectWidth::Int32);
}

void EffectRunner::setFrame(Slot& slot, std::optional<std::uint32_t> instance, std::int32_t frame) {
  if (instance) {
    write(slot, static_cast<std::int32_t>(*instance), EffectWidth::Int32, frame);
  }
}

} // namespace dwe::game::battle
