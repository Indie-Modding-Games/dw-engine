#pragma once

#include <dwe/game/tournament/match_show.hpp>

#include <array>
#include <cstdint>

// The cup won (std_setup.c: STD_tournamentMain after the final): once the board's champion climb
// is over the screen fades to black, the partner stands on the podium (std_hud.c:
// STD_loadChampionModels, STD_initializeChampionScene; std_effect.c: STD_addPodiumRenderer), the
// screen fades back in and the champion scene plays (std_effect.c: STD_tickChampionScene): the
// title's letters spin in one after another, then the podium lifts the partner while the camera
// circles it, the partner strikes its pose and the camera climbs. Pure state, one tick per frame
// of the original (20 a second), in the original's axes and units (y down, angles 4096 a turn).
namespace dwe::game::tournament {

class ChampionShow {
public:
  // `partner`: the partner's size (its height and radius set the camera).
  explicit ChampionShow(ShowBody partner);

  // One frame; `poseEnded`: the partner's pose (PoseClip) has played through once (anim.c:
  // animFlag 1 off).
  void tick(bool poseEnded);
  // STD_CHAMPION_SCENE_DONE: the tournament may close.
  [[nodiscard]] bool done() const { return done_; }

  // How black the screen is, 0 to 1 (fadeToBlack(10) before the podium, fadeFromBlack(15) after).
  [[nodiscard]] double blackout() const;
  // The board is gone and the podium shows with the partner on it (STD_removeBracket(2),
  // STD_addPodiumRenderer): from the frame the screen is black on.
  [[nodiscard]] bool podiumShown() const { return phase_ != Phase::FadeOut; }

  // The partner on the podium (ENTITY_TABLE[1]): at the arena's centre, facing 0x200, its feet
  // this high (negative is up), playing this clip (bumped `starts` when it starts again).
  struct Partner {
    std::int32_t y = 0;
    std::int32_t yaw = 0;
    std::uint32_t clip = 0;
    std::uint32_t starts = 0;
  };
  [[nodiscard]] Partner partner() const { return partner_; }
  // Where the podium (STDDAT\T_DAI.TMD, B_DAI on the other arena) stands: its y; x and z are 0.
  [[nodiscard]] std::int32_t podiumY() const { return podiumY_; }
  [[nodiscard]] ShowView view() const { return view_; }
  // The title's letters (STDDAT\CHAMP.TMD, nine objects), the ones shown so far.
  [[nodiscard]] LetterScene letters() const;

  // STD_initializeChampionScene, STD_tickChampionScene: the stance and the closing pose.
  static constexpr std::uint32_t StanceClip = 0x21;
  static constexpr std::uint32_t PoseClip = 0x2a;
  static constexpr std::int32_t Letters = 9;
  // STD_tournamentMain: the frames of each step before the scene.
  static constexpr std::int32_t FadeOutFrames = 0xb;
  static constexpr std::int32_t SettleFrames = 2;
  static constexpr std::int32_t FadeInFrames = 0x10;

private:
  enum class Phase : std::uint8_t { FadeOut, Settle, FadeIn, Scene };
  void tickScene(bool poseEnded);
  void play(std::uint32_t clip);

  ShowBody body_;
  Phase phase_ = Phase::FadeOut;
  std::int32_t phaseFrame_ = 0;
  // STD_CHAMPION_SCENE_STATE and STD_WIN_LOSS_DRAW_TIMER.
  std::int32_t state_ = 0;
  std::int32_t timer_ = 0;
  bool done_ = false;
  Partner partner_;
  std::int32_t podiumY_ = 0;
  ShowView view_;
  std::array<bool, Letters> shown_{};
  std::array<std::int32_t, Letters> spin_{};
  std::int32_t lettersY_ = 0;
  // STD_CHAMPION_ORBIT_ANGLES: the camera's way round the partner as the podium lifts it.
  std::array<std::int32_t, 79> orbit_{};
};

} // namespace dwe::game::tournament
