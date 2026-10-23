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
#include <utility>

namespace dwe::session {

// The techniques' effect scripts in a battle, for their sounds (battle_effect.c): each move's
// effect/<move>/ loads the first time a fighter uses it, starts when its attack starts
// (battle_main.c: BTL_setupMoveExecution, BTL_startEFE) and runs one step per battle frame
// (BTL_tickEFEEngine). Its sounds go to slot 8 of the sound port. An attack that hits tells its
// effect, whose hit checks answer with it; a fighter that is hit loses its own effect, as does one
// that starts another attack (BTL_removeMoveEffect). A move without an effect plays nothing.
class BattleEffects {
public:
  // Both must outlive this.
  BattleEffects(const assets::VirtualFileSystem& files, game::story::SoundPort& sound);

  // One battle frame: its events, then the effects' step.
  void frame(const game::battle::BattleEvents& events, const game::battle::MoveCatalog& moves);
  // The battle ended: every held sound stops (battle_effect.c: BTL_removeEFEEngine).
  void end();

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
  void startAttack(game::battle::FighterId fighter, const std::string& move);
  void removeEffect(game::battle::FighterId fighter);
  [[nodiscard]] const std::string& moveOf(std::size_t slot) const;

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
