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
#include <dwe/screens/battle_intro.hpp>
#include <dwe/session/battle_effects.hpp>
#include <dwe/session/session_music.hpp>
#include <dwe/session/session_sound.hpp>
#include <dwe/session/tournament_adapter.hpp>
#include <dwe/ui/ui.hpp>

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
#include "tournament_match_screen.hpp"
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
  // The versus' arenas (vs_scene.c: VS_loadArenaAssets(VS_BATTLE_SETUP.stage)), by the series'
  // arena number; the first shows until the series picks one.
  std::vector<session::TournamentAdapter::ArenaStage> arenas;
  // The game clock's rules, for the field the arena stands on.
  ruleset::ClockRules clock;
  // Where the entrance's stats and the time-up box show (the tournament's match document); it may
  // be null.
  ui::Ui* ui = nullptr;
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
// Around each battle it shows what the original does, at its 20 frames a second while the flow
// holds the battle (presenting()): the fighters' entrance (vs_intro.c: VS__runIntro, the
// tournament's STD_runIntro), the "BATTLE" banner (vs_combat.c: VS__combatSetup) and, once the
// battle is over, its end (VS__checkEndCondition, VS__deinitializeCombat): the time-up box, the
// winner's "1P WIN" or "2P WIN" letters, the winner's pose and the arena's songs.
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
  // Loads one more arena's model ahead, hidden, so a series on it does not wait for it; false once
  // every arena's is loaded.
  bool preloadArena();

  void battleStarted(game::battle::Battle& battle) override;
  void battleFrame(const game::battle::Battle& battle,
                   const game::battle::BattleEvents& events) override;
  void battlesOver() override;
  void useArena(std::int32_t arena) override;
  [[nodiscard]] bool presenting() const override;
  void presentFrame(bool confirm) override;
  void showScore(bool shown, bool last, std::optional<std::size_t> winner) override;
  void menuSound(std::int32_t sound) override;
  void addSettings(screens::ScreenSettings& out) const override;

  // Whether the arena shows: then the game draws scene(). Over the score screen only the series'
  // winner's letters show.
  [[nodiscard]] bool showing() const {
    return shown_ != nullptr && (!scoring_ || finalLetters_ || finalDraw_);
  }
  // Whether the score screen shows over a black screen (the arena hidden).
  [[nodiscard]] bool scoring() const { return scoring_; }
  [[nodiscard]] FieldScene& scene() { return *scene_; }
  // Once per game tick: the fighters' clips run on.
  void tick(double seconds);
  // Once per drawn frame, before the scene is drawn: alpha between the last two ticks.
  void update(double frameSeconds, float alpha, const GraphicsOptions& graphics);

  // The id the battle holds the rival under (FieldScene::addStageFighter), past any script id.
  static constexpr std::int32_t RivalFighter = 0x1000;

private:
  // What shows of a battle: its entrance, its banner, the fight, its end and the end's last frames
  // with the HUD gone.
  enum class Phase : std::uint8_t { Intro, Banner, Fight, Ending, Done };
  // The end's steps (VS__deinitializeCombat): the time-up box, the letters with the winner's pose
  // (or the drawn letters) and the frames after it.
  enum class EndStep : std::uint8_t { TimeUp, Letters, Tail };

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
  void startIntro(const game::battle::Battle& battle);
  void introFrame(bool confirm);
  void startBanner();
  void bannerFrame();
  // The battle's fall or its time up: the winner's letters and the end camera, or the drawn
  // letters.
  // The battle ended: the end's steps start (the time-up box or the letters).
  void endOnResult(const game::battle::Battle& battle);
  void startLetters(std::optional<std::size_t> winner);
  void endFrame(bool confirm);
  void timeUpFrame(bool confirm);
  void startWinnerPose();
  // The fighter the end camera hangs on: the one that lost.
  [[nodiscard]] std::size_t loserShown() const;
  void playSong(std::int32_t track);
  void playSound(std::int32_t sound);
  void showLetters(const game::tournament::ShowView& view);
  [[nodiscard]] const session::TournamentAdapter::ArenaStage& arena() const;
  [[nodiscard]] std::string speciesName(const std::string& id) const;

  VersusArenaDesc desc_;
  // The game clock the field stands in, before the field that reads it.
  std::unique_ptr<ruleset::GameClock> clock_;
  std::unique_ptr<FieldScene> scene_;
  std::optional<session::SessionSound> sound_;
  std::optional<session::SessionMusic> music_;
  std::optional<TournamentMatchScreen> matchScreen_;
  std::size_t arenaIndex_ = 0;
  // The arenas preloadArena went through.
  std::size_t preloaded_ = 0;
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

  Phase phase_ = Phase::Done;
  EndStep endStep_ = EndStep::Letters;
  std::int32_t endFrames_ = 0;
  std::optional<game::tournament::MatchIntro> intro_;
  std::optional<screens::BattleIntro> banner_;
  std::array<std::string, 2> names_;
  std::optional<game::tournament::TimeUpNotice> timeUp_;
  std::optional<game::tournament::VersusWinLetters> letters_;
  std::optional<game::tournament::MatchLetters> drawLetters_;
  std::optional<game::tournament::EndCamera> endCamera_;
  // The series' winner's letters over the last score screen.
  std::optional<game::tournament::VersusWinLetters> finalLetters_;
  std::optional<game::tournament::MatchLetters> finalDraw_;
  bool scoring_ = false;
  double letterSeconds_ = 0.0;
  // The battle's end as last seen: who won, the damage each side dealt, where each one stands.
  std::optional<std::size_t> winner_;
  std::array<std::int32_t, 2> lost_{};
  std::array<game::tournament::ArenaPoint, 2> standing_{};
  std::array<game::tournament::ShowBody, 2> bodies_{};
  // The winner's pose clip and how long it runs (it starts again when it ends).
  std::size_t poseLength_ = 0;
  game::tournament::ShowView view_{};
};

} // namespace dwe::app
