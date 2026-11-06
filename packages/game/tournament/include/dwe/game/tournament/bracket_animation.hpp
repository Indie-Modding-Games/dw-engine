#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/tournament/tournament.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// The bracket a cup shows before each of the partner's matches (std/std_main.c:
// STD_initializeBracket, STD_updateBracket, STD_addBracketProjectile, STD_tickBracketProjectile,
// STD_tickBracketHitFlash, STD_addBracketIntro): the eight fighters' sprites climb the tree, the
// matches the partner does not play are fought with shots (the winner's is double) and the loser
// hops down, until the partner's match comes up and the board fades. Pure state, one tick per
// frame of the original (20 a second); the app draws it. Positions are the original's screen
// pixels from its centre (y down).
namespace dwe::game::tournament {

// data/stadium's bracket (std_main.c: STD_BRACKET_ROW_Y, STD_BRACKET_HOP_Y, STD_BRACKET_PATHS,
// STD_DIGIMON_SPRITE_CLUT, STD_BRACKET_PROJECTILE_ICONS, STD_BRACKET_PROJECTILE_CLUTS).
struct BracketLayout {
  // The y of each step up the tree (8) and the loser's hop down, frame by frame.
  std::vector<std::int32_t> rowY;
  std::vector<std::int32_t> hopY;
  // By slot: the x of each step and whether the sprite faces right there.
  struct Path {
    std::array<std::int32_t, 8> x{};
    std::array<bool, 8> flip{};
  };
  std::array<Path, Entrants> paths{};
  // By species: the palette of its sprite and its shot's icon (1 = the first); by icon - 1, the
  // shot's palette.
  std::vector<std::int32_t> spritePalettes;
  std::vector<std::int32_t> projectileIcons;
  std::vector<std::int32_t> projectilePalettes;
};

// data/stadium's `bracket`; none in an extraction made before dwx read it.
core::Result<std::optional<BracketLayout>> parseBracketLayout(const core::YamlNode& stadium);

// A species' sprite cell in the bracket's sprite pages (STD_renderBracketDigimon): the page
// (stdtim 000 or 001), the top left corner of the 16 x 16 cell of `face` and its palette.
struct BracketCell {
  std::int32_t page = 0;
  std::int32_t u = 0;
  std::int32_t v = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t palette = 0;
};
[[nodiscard]] BracketCell bracketSpriteCell(const BracketLayout& layout, std::int32_t species,
                                            std::int32_t face);
// A species' shot (STD_renderBracketProjectile): an 8 x 8 cell of the second page.
[[nodiscard]] BracketCell bracketShotCell(const BracketLayout& layout, std::int32_t species);

// What the board plays: before a match (STD_addBracket(1)) or the champion's climb to the top
// after the final (STD_addBracket(2)).
enum class BracketShow : std::uint8_t { Match, Champion };

class BracketAnimation {
public:
  // STD_initializeBracket with the bracket STD_setupParticipants drew.
  BracketAnimation(BracketLayout layout, const Bracket& bracket);

  // The intro (STD_addBracketIntro): the grade's card over a dark board for 100 frames, then both
  // fading for 20 more. Ticks until introDone().
  void startIntro();
  [[nodiscard]] bool introDone() const { return introFrames_ >= IntroFrames; }

  // STD_addBracket: the board shows again and plays until finished().
  void show(BracketShow show);
  // One frame (the shots' and hit flashes' ticks, then STD_updateBracket).
  void tick();
  // STD_isBracketFinished.
  [[nodiscard]] bool finished() const { return finished_; }
  // STD_MATCH_RESULT once the partner's match is over.
  void matchOver(bool won) { result_ = won ? 1 : -1; }

  struct SlotView {
    std::int32_t species = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    // 0 and 1 the idle frames, 2 shooting, 3 beaten.
    std::int32_t face = 0;
    bool flipped = false;
  };
  struct ShotView {
    std::int32_t species = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool flipped = false;
    // The winner's shot is two icons, the second 8 pixels lower.
    bool doubled = false;
  };
  struct View {
    std::array<SlotView, Entrants> slots{};
    std::vector<ShotView> shots;
    // The slots whose hit flash shows this frame (16 x 16 over the slot).
    std::vector<std::size_t> flashes;
    // 0 (lit) to 128 (dark): STD_BRACKET_FADE, the board's and sprites' darkening.
    std::int32_t fade = 0;
    // The intro's card and shade brightness (MAIN_D_80135166, MAIN_D_80135165): 128 full.
    std::int32_t card = 0;
    std::int32_t shade = 0;
    bool intro = false;
  };
  [[nodiscard]] View view() const;
  // The sounds of bank 8 played since the last call (playSound(8, n)).
  std::vector<std::int32_t> takeSounds();

  // The original's frame counts.
  static constexpr std::int32_t IntroFrames = 120;

private:
  struct Slot {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::size_t step = 0;
    std::int32_t face = 0;
    bool flipped = false;
    std::int32_t state = 0;
    std::int32_t timer = 0;
    std::int32_t counter = 0;
    bool hit = false;
    bool loser = false;
  };
  struct Shot {
    bool active = false;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::size_t target = 0;
    bool doubled = false;
  };

  void update();
  void step(std::size_t slot, std::array<std::size_t, 2> pair, bool partnerMatch);
  void shoot(std::size_t slot, std::size_t target, bool winner);
  void tickShots();
  [[nodiscard]] std::array<std::size_t, 2> pair() const;
  [[nodiscard]] std::int32_t rowY(std::size_t step) const;
  void blink(Slot& slot) const;

  BracketLayout layout_;
  std::array<std::int32_t, Entrants> species_{};
  std::size_t player_ = 0;
  std::array<std::size_t, Entrants / 2> round1_{};
  std::array<std::size_t, 2> round2_{};
  std::array<Slot, Entrants> slots_{};
  std::array<Shot, Entrants> shots_{};
  std::array<bool, Entrants> flashes_{};
  std::array<bool, Entrants> flashShown_{};
  std::int32_t match_ = 1;
  std::int32_t result_ = 0;
  std::int32_t fade_ = 0;
  bool finished_ = false;
  bool intro_ = false;
  std::int32_t introFrames_ = IntroFrames;
  std::int32_t card_ = 0;
  std::int32_t shade_ = 0;
  std::vector<std::int32_t> sounds_;
};

} // namespace dwe::game::tournament
