#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/flights.hpp>
#include <dwe/game/tournament/match_show.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/session/battle_effects.hpp>
#include <dwe/session/session_sound.hpp>
#include <dwe/session/tournament_adapter.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "battle_effect_bodies.hpp"
#include "battle_scene_view.hpp"
#include "field_scene.hpp"
#include "fighter_figure.hpp"
#include "graphics_options.hpp"
#include "versus_flow.hpp"

namespace dwe::app {

struct VersusArenaDesc {
  // All must outlive the arena.
  render::Renderer* renderer = nullptr;
  const assets::VirtualFileSystem* files = nullptr;
  // The species table, for the fighters' sizes; it may be null.
  const assets::DigimonTable* species = nullptr;
  // The sound the techniques and the fighters' clips play through; it may be null (silent).
  audio::Audio* audio = nullptr;
  // The versus' arena (vs_scene.c: VS_loadArenaAssets(VS_BATTLE_SETUP.stage)).
  session::TournamentAdapter::ArenaStage arena;
  // The game clock's rules, for the field the arena stands on.
  ruleset::ClockRules clock;
};

// The versus' battles as the original shows them (vs_scene.c: VS_initializeVS, VS_loadVSAssets,
// VS_loadFighterEntities; vs_combat.c: VS__combatMain): the arena on its own stage with the host's
// fighter as the partner (ENTITY_TABLE[1]) and the guest's as the rival (ENTITY_TABLE[2]), each
// moving, turning and animating as the battle has it, the techniques' effects, the statuses and
// hit sparks over them, and the arena's camera following them (the tournament's: the versus
// overlay runs the same camera code). It draws on a field scene of its own, the one the battles of
// the story are drawn on (field_scene.hpp, battle_scene_view.hpp), and shows the battle the flow
// plays: it decides nothing of it.
//
// It also readies each battle before its first tick as the battles of the story are (the clips of
// its fighters' models, the flights of its techniques), for this machine's flow and for the other
// player's of --versus-loopback: prepare() does only what both machines' content gives.
class VersusArena final : public VersusBattleView {
public:
  static core::Result<std::unique_ptr<VersusArena>> create(VersusArenaDesc desc);
  ~VersusArena() override;
  VersusArena(const VersusArena&) = delete;
  VersusArena& operator=(const VersusArena&) = delete;
  VersusArena(VersusArena&&) = delete;
  VersusArena& operator=(VersusArena&&) = delete;

  // The fighters' models and the battle's clips and flights, without showing it.
  void prepare(game::battle::Battle& battle);

  void battleStarted(game::battle::Battle& battle) override;
  void battleFrame(const game::battle::Battle& battle,
                   const game::battle::BattleEvents& events) override;
  void battlesOver() override;

  // Whether a battle shows: then the game draws scene().
  [[nodiscard]] bool showing() const { return shown_ != nullptr; }
  [[nodiscard]] FieldScene& scene() { return *scene_; }
  // Once per game tick: the fighters' clips run on.
  void tick(double seconds);
  // Once per drawn frame, before the scene is drawn: alpha between the last two ticks.
  void update(double frameSeconds, float alpha, const GraphicsOptions& graphics);

  // The id the battle holds the rival under (FieldScene::addStageFighter), past any script id.
  static constexpr std::int32_t RivalFighter = 0x1000;

private:
  // What a battle needs that its fighters' models give (battle_screen.cpp does the same): kept
  // while the battle lives, by its address.
  struct Rig {
    const game::battle::Battle* battle = nullptr;
    std::unique_ptr<BattleEffectBodies> bodies;
    std::unique_ptr<game::battle::ScriptedFlights> flights;
  };

  VersusArena(VersusArenaDesc desc, std::unique_ptr<ruleset::GameClock> clock,
              std::unique_ptr<FieldScene> scene);
  // Draws the battle's two species; false, logged, when a model cannot be drawn.
  bool showFighters(const game::battle::BattleState& state);
  [[nodiscard]] Rig* rigOf(const game::battle::Battle& battle);
  void noteMoveStarts(const game::battle::Battle& battle);
  void showStage(const game::tournament::ShowView& view);

  VersusArenaDesc desc_;
  // The game clock the field stands in, before the field that reads it.
  std::unique_ptr<ruleset::GameClock> clock_;
  std::unique_ptr<FieldScene> scene_;
  std::optional<session::SessionSound> sound_;
  ShotColumns shotColumns_;
  // The fighter k > 0 is the stage fighter enemyNpcs_[k - 1].
  std::vector<std::int32_t> enemyNpcs_{RivalFighter};
  // The species drawn: the partner's and the rival's.
  std::array<std::string, 2> drawn_;
  std::vector<Rig> rigs_;
  // The battle on show, its techniques' effects and the frames they played.
  const game::battle::Battle* shown_ = nullptr;
  std::optional<session::BattleEffects> effects_;
  std::uint64_t effectFrames_ = 0;
  std::vector<FighterFigure> figures_;
  // The arena's camera (vs_combat.c: VS_tickFrame's camera, the tournament's STD_tickBattle) with
  // its own rolls: what one machine shows never touches the battle's.
  std::optional<game::tournament::MatchCamera> camera_;
  ruleset::Random cameraRolls_{1};
  std::array<std::optional<std::int32_t>, 2> attackFrames_{};
  // The battle frame last shown: a frame the other machine held back shows nothing new.
  std::optional<std::int64_t> lastFrame_;
};

} // namespace dwe::app
