#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

namespace dwe::screens {

// The settings of a battle's opening on the battle document (ui/battle/document), which redraws it
// on IntroRevisionSetting: the step ("anger", "banner" or "burst"), the anger marks over the
// enemies and the pieces of the "BATTLE" banner.
inline constexpr std::string_view IntroRevisionSetting = "battle.intro.revision";
inline constexpr std::string_view IntroStepSetting = "battle.intro.step";
inline constexpr std::string_view IntroAngerList = "battle.intro.anger";
inline constexpr std::string_view IntroAngerScaleSetting = "battle.intro.anger.scale";
inline constexpr std::string_view IntroFlashSetting = "battle.intro.flash";
inline constexpr std::string_view IntroPiecesSetting = "battle.intro.pieces";

// The steps of the opening: the anger marks while the original loads the battle (btl.c:
// handleBattleStart, setFleeBubble), the banner's pieces flying together and flashing
// (battle_hud.c: BTL_renderBattleStartText), and the pieces bursting away
// (BTL_renderBattleStartTextBurst). The battle starts once they are gone (battle_setup.c:
// BTL_handleBattleIntro).
enum class IntroStep : std::uint8_t { Anger, Banner, Burst, Done };

// The banner's pieces of the original (battle_effect.c: BTL_BATTLE_START_TEXT_POSITIONS): 8 x 12
// cells of a plane that spell "BATTLE".
inline constexpr std::size_t BannerPieceCount = 155;

// A piece of the banner as it shows: a rectangle on the original's 320 x 240 screen, which of the
// three textures it wears, and the trail behind it while it flies, on its left or its right.
struct BannerPiece {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
  std::uint8_t texture = 0;
  float trail = 0.0F;
  bool trailLeft = false;

  friend bool operator==(const BannerPiece&, const BannerPiece&) = default;
};

// An enemy's anger mark on the screen, where the game projects it; nothing hides it.
using AngerAnchor = std::optional<WindowFraction>;

// The opening of a battle, frame by frame at the original's battle rate (20 frames a second): the
// anger marks over the enemies, then the "BATTLE" banner, then its burst. The randomness is the
// original's (randomLimit), from a seed, so that a route sees the same opening every time.
class BattleIntro {
public:
  explicit BattleIntro(std::uint32_t seed);

  // One frame of the original's battle.
  void step();

  [[nodiscard]] IntroStep current() const { return step_; }
  [[nodiscard]] bool done() const { return step_ == IntroStep::Done; }

  // How high over its feet an enemy's anger mark stands, in world units, and how big it is in
  // percent of its full size (btl.c: renderFleeBubble): it rises and grows for 5 frames, then
  // pulses.
  [[nodiscard]] float angerLift() const;
  [[nodiscard]] std::int32_t angerScale() const;

  // The banner as it shows this frame, and the palette its pieces flash in once they are all in
  // place (0 to 2; BTL_renderBattleStartText cycles three palettes).
  [[nodiscard]] std::vector<BannerPiece> pieces() const;
  [[nodiscard]] std::int32_t flash() const { return flash_; }

private:
  // A piece on the banner's plane (BTL_BATTLE_START_TEXT_PIECES): where it is, where it flies to,
  // its speed, its trail, its texture, and for the burst its turn and its drift.
  struct Piece {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t targetX = 0;
    std::int32_t speed = 0;
    std::int32_t trail = 0;
    std::uint8_t texture = 0;
    std::array<std::int32_t, 3> angles{};
    std::int32_t driftX = 0;
    std::int32_t driftY = 0;
  };

  [[nodiscard]] std::int32_t randomLimit(std::int32_t limit);
  void startBanner();
  void stepBanner();
  void stepBurst();
  [[nodiscard]] BannerPiece shownPiece(const Piece& piece) const;

  std::minstd_rand random_;
  IntroStep step_ = IntroStep::Anger;
  std::int32_t frame_ = 0;
  std::int32_t flash_ = 0;
  std::int32_t flashFrames_ = 0;
  bool gathered_ = false;
  std::array<Piece, BannerPieceCount> pieces_{};
};

// What the battle document shows of the opening: its step, the anger marks over the enemies (in
// percent of the window, like the field HUD's bubble) and the banner. The pieces go packed in one
// setting, seven numbers each (x, y, width, height, texture, trail, 1 for a trail on the left),
// since they all change every frame.
struct BattleIntroView {
  IntroStep step = IntroStep::Done;
  std::vector<AngerAnchor> anger;
  std::int32_t angerScale = 0;
  std::int32_t flash = 0;
  std::vector<BannerPiece> pieces;
};

[[nodiscard]] BattleIntroView battleIntroViewOf(const BattleIntro& intro,
                                                std::vector<AngerAnchor> anger);

// The view as the settings the battle document reads; the anger list has `enemies` entries.
[[nodiscard]] ScreenSettings settingsOf(const BattleIntroView& view, std::size_t enemies);

} // namespace dwe::screens
