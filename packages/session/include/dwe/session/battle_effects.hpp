#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/battle/effect_runner.hpp>
#include <dwe/game/battle/events.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/state.hpp>
#include <dwe/game/story/story_ports.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::session {

// The techniques' effect scripts in a battle (battle_effect.c): each move's effect/<move>/ loads
// the first time a fighter uses it, starts when its attack starts (battle_main.c:
// BTL_setupMoveExecution, BTL_startEFE) from the attacker at its target and runs one step per
// battle frame (BTL_tickEFEEngine, then BTL_renderEFEEngine for what it draws). Its sounds go to
// slot 8 of the sound port; what it draws, placed on the fighters the bodies give, to draws(). An
// attack that hits tells its effect, whose hit checks answer with it; a fighter that is hit loses
// its own effect, as does one that starts another attack (BTL_removeMoveEffect). A move without an
// effect plays nothing.
// A move's effect/<move>/ read from the packs; nothing for a move without one (logged).
std::optional<assets::EffectScript> readMoveEffect(const assets::VirtualFileSystem& files,
                                                   std::string_view move);

class BattleEffects {
public:
  // Both must outlive this.
  BattleEffects(const assets::VirtualFileSystem& files, game::story::SoundPort& sound);

  // One battle frame: its events, then the effects' step.
  void frame(const game::battle::BattleEvents& events, const game::battle::MoveCatalog& moves);
  // The battle ended: every held sound stops (battle_effect.c: BTL_removeEFEEngine).
  void end();
  // The fighters the effects start from and aim at; it must outlive this.
  void setBodies(const game::battle::EffectBodies& bodies) { runner_.setBodies(bodies); }
  // What the battle's camera shows (EffectRunner::setView).
  void setView(const game::battle::EffectView& view) { runner_.setView(view); }
  // The scripts roll with the battle's rolls when those are the original's
  // (game::battle::Battle::originalRolls, EffectRunner::useRolls), and put their attacker
  // particles among the battle's (Battle::hitParticles); both must outlive this.
  void useRolls(ruleset::Random* rolls, game::battle::HitParticles* particles = nullptr) {
    runner_.useRolls(rolls, particles);
  }
  // What the effects drew on the last frame, and the move whose effect/<move>/ a slot is.
  [[nodiscard]] const std::vector<game::battle::EffectDraw>& draws() const {
    return runner_.draws();
  }
  [[nodiscard]] const std::string& moveOf(std::size_t slot) const;
  // Whether a technique hid the map's layer this frame (EffectRunner::mapHidden).
  [[nodiscard]] bool mapHidden() const { return runner_.mapHidden(); }
  // The palette rows a slot's scripts rewrote (EffectRunner::palettes).
  [[nodiscard]] const game::battle::EffectPalettes& palettesOf(std::size_t slot) const {
    return runner_.palettes(slot);
  }

private:
  class Sounds final : public game::battle::EffectSounds {
  public:
    explicit Sounds(BattleEffects& owner) : owner_(&owner) {}
    void play(std::size_t slot, std::int32_t sound) override;
    std::optional<std::int64_t> playHeld(std::size_t slot, std::int32_t sound) override;
    void stop(std::int64_t voice) override;

  private:
    BattleEffects* owner_;
  };

  // The running effect of a fighter: its slot and the section start() gave.
  struct Running {
    std::size_t slot = 0;
    std::optional<std::size_t> section;
  };

  std::optional<std::size_t> slotOf(const std::string& move);
  void startAttack(game::battle::FighterId fighter, std::optional<game::battle::FighterId> target,
                   const std::string& move);
  void removeEffect(game::battle::FighterId fighter);

  const assets::VirtualFileSystem* files_;
  game::story::SoundPort* sound_;
  Sounds sounds_{*this};
  game::battle::EffectRunner runner_{sounds_};
  std::map<std::string, std::size_t, std::less<>> slots_;
  std::map<std::size_t, std::string> moves_;
  std::set<std::string, std::less<>> missing_;
  std::map<game::battle::FighterId, Running> running_;
};

} // namespace dwe::session
