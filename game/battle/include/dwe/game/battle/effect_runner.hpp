#pragma once

#include <dwe/assets/effect_script.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

namespace dwe::game::battle {

// Where the effect scripts' sounds go: bank 8 of the sound player (battle_effect.c:
// BTL_playEFESound). A held sound keeps its voice so the runner can stop it when its sub-effect
// ends.
class EffectSounds {
public:
  EffectSounds() = default;
  EffectSounds(const EffectSounds&) = delete;
  EffectSounds& operator=(const EffectSounds&) = delete;
  EffectSounds(EffectSounds&&) = delete;
  EffectSounds& operator=(EffectSounds&&) = delete;
  virtual ~EffectSounds() = default;

  // The slot is the effect's (EffectRunner::load) that asks for the sound.
  virtual void play(std::size_t slot, std::int32_t sound) = 0;
  // The voice the sound plays on, or nothing when it did not play.
  virtual std::optional<std::int64_t> playHeld(std::size_t slot, std::int32_t sound) = 0;
  virtual void stop(std::int64_t voice) = 0;
};

// The battle's effect scripts, run as the original runs them once per battle frame
// (battle_effect.c: BTL_startEFE, BTL_tickEFEEngine, BTL_runEFEScript) over each slot's own copy of
// its data. Only the sounds have an effect outside the scripts: the drawing sub-ops are skipped
// (their arguments still leave the stack), and the hit checks answer from the battle's own hits
// (noteHit) instead of the original's collision boxes. Frames, spawns, branches and calls follow
// the original, so each sound plays on the frame the original plays it.
class EffectRunner {
public:
  explicit EffectRunner(EffectSounds& sounds, std::uint32_t seed = 0);

  // Loads a technique's effect for the battle (battle_effect.c: BTL_loadMoveEFE) and returns its
  // slot.
  std::size_t load(assets::EffectScript script);
  [[nodiscard]] std::size_t slotCount() const { return slots_.size(); }

  // Runs a slot's start script (BTL_startEFE). Returns the first instance it spawned, which
  // stop() takes, or nothing.
  std::optional<std::size_t> start(std::size_t slot);
  // Ends a running effect early (battle_main.c: BTL_removeMoveEffect, BTL_stopEFESubEffect).
  void stop(std::size_t slot, std::size_t section);
  // One battle frame: every running sub-effect's tick script, then the held sounds of the
  // sub-effects that ended (BTL_tickEFEEngine).
  void tick();
  // The battle hit with the attack the slot plays: the slot's next hit check reports it.
  void noteHit(std::size_t slot);
  // Stops every held sound (battle_effect.c: BTL_removeEFEEngine, BTL_stopEFESounds).
  void stopAll();

  // Whether a sub-effect of the slot still runs.
  [[nodiscard]] bool running(std::size_t slot) const;

private:
  // A sub-effect of a slot (battle_effect.c: EfeSubEffect): the script it runs each frame and the
  // instance it runs on, as offsets into the slot.
  struct SubEffect {
    std::optional<std::uint32_t> tick;
    std::optional<std::uint32_t> render;
    std::optional<std::uint32_t> instance;
    std::optional<std::uint32_t> parent;
  };

  struct Slot {
    assets::EffectScript script;
    std::vector<std::byte> data;
    std::vector<SubEffect> subEffects;
    bool hitPending = false;
    bool hitSinceStart = false;
  };

  struct CallFrame {
    std::size_t returnTo = 0; // an instruction index
    std::size_t subEffect = 0;
  };

  // A held sound (battle_effect.c: EFE_SOUND_DATA): its voice and the sub-effect that owns it.
  struct HeldSound {
    std::int64_t voice = 0;
    std::size_t slot = 0;
    std::size_t subEffect = 0;
  };

  void run(std::size_t slot, std::uint32_t script);
  std::optional<std::size_t> step(Slot& slot, std::size_t at);
  std::optional<std::size_t> returnFrom();
  std::optional<std::size_t> spawn(Slot& slot, const assets::EffectInstruction& in,
                                   std::size_t next);
  void subOp(Slot& slot, const assets::EffectInstruction& in);
  void playSound(std::int32_t sound);
  static void writeHit(Slot& slot, std::int32_t address, bool consume);

  [[nodiscard]] std::int32_t address(const assets::EffectVariable& variable) const;
  [[nodiscard]] static std::int32_t read(const Slot& slot, std::int32_t address,
                                         assets::EffectWidth width);
  static void write(Slot& slot, std::int32_t address, assets::EffectWidth width,
                    std::int32_t value);
  [[nodiscard]] static std::int32_t frameOf(const Slot& slot,
                                            std::optional<std::uint32_t> instance);
  static void setFrame(Slot& slot, std::optional<std::uint32_t> instance, std::int32_t frame);

  EffectSounds* sounds_;
  std::minstd_rand random_;
  std::vector<Slot> slots_;
  std::vector<std::optional<HeldSound>> held_;

  // The interpreter's registers (battle_effect.c: EFE_SCRIPT_*), valid while a script runs.
  std::size_t slot_ = 0;
  std::size_t subEffect_ = 0;
  std::optional<std::uint32_t> current_;
  std::optional<std::uint32_t> previous_;
  std::optional<std::size_t> activeSection_;
  std::int32_t register_ = 0;
  std::vector<std::int32_t> stack_;
  std::vector<CallFrame> calls_;
};

} // namespace dwe::game::battle
