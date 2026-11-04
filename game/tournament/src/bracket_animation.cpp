#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/tournament/bracket_animation.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

namespace dwe::game::tournament {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view StadiumText = "data/stadium";
constexpr std::string_view BracketKey = "bracket";
constexpr std::string_view RowYKey = "row_y";
constexpr std::string_view HopYKey = "hop_y";
constexpr std::string_view PathsKey = "paths";
constexpr std::string_view PathXKey = "x";
constexpr std::string_view PathFlipKey = "flip";
constexpr std::string_view SpritePalettesKey = "sprite_palettes";
constexpr std::string_view ProjectileIconsKey = "projectile_icons";
constexpr std::string_view ProjectilePalettesKey = "projectile_palettes";
constexpr std::size_t Steps = 8;

// STD_updateBracket's frame counts and numbers.
constexpr std::int32_t BlinkFrames = 10;      // the idle frames swap every 10
constexpr std::int32_t WaitFrames = 41;       // a pair waits before it climbs and before it shoots
constexpr std::int32_t ClimbSoundFrame = 5;   // playSound(8, 5) as it climbs
constexpr std::int32_t ShootPauseFrames = 21; // the shooting frame shows this long
constexpr std::int32_t HitFrames = 31;        // the hit's reaction
constexpr std::int32_t TurnFrame = 20;        // the winner turns back here
constexpr std::int32_t HopSoundFrame = 20;    // playSound(8, 4) as the loser lands
constexpr std::int32_t ChampionFrames = 61;   // STD_addBracket(2): the champion at the top
constexpr std::int32_t FadeStep = 3;
constexpr std::int32_t FullFade = 0x80;
constexpr std::int32_t FaceShoot = 2;
constexpr std::int32_t FaceBeaten = 3;
// Matches 1 to 4 are the first round, 5 and 6 the second and 7 the final.
constexpr std::int32_t SecondRoundMatch = 5;
constexpr std::int32_t FinalMatch = 7;
constexpr std::size_t ChampionStep = 7;
constexpr std::size_t RunnerUpStep = 4;
// The shots (STD_addBracketProjectile, STD_tickBracketProjectile, STD_renderBracketProjectile):
// where they start from their shooter, their speed by round and how near they hit.
constexpr std::int32_t ShotRightStart = 16;
constexpr std::int32_t ShotLeftStart = -8;
constexpr std::int32_t ShotLowered = 8;
constexpr std::int32_t ShotFirstRoundSpeed = 1;
constexpr std::int32_t ShotSecondRoundSpeed = 3;
constexpr std::int32_t ShotRightReach = 9;
constexpr std::int32_t ShotLeftReach = 0x11;
// STD_tickBracketHitFlash: the flash shows every fifth frame of the hit until frame 16.
constexpr std::int32_t FlashPeriod = 5;
constexpr std::int32_t FlashFrames = 0x10;
// STD_tickBracketIntro: the card and the shade fade from frame 101, 8 a frame.
constexpr std::int32_t IntroFadeFrame = 0x65;
constexpr std::int32_t IntroFadeStep = 8;
constexpr std::int32_t IntroShadeFloor = 8;
// STD_renderBracketDigimon: 16 x 16 cells, a species' four in a 32 x 32 block, 64 species a page;
// the page's last row and column are 15 wide.
constexpr std::int32_t Cell = 16;
constexpr std::int32_t Block = 32;
constexpr std::int32_t BlocksPerRow = 8;
constexpr std::int32_t SpeciesPerPage = 64;
constexpr std::int32_t PageSide = 256;
constexpr std::int32_t ShotCell = 8;
constexpr std::int32_t ShotRow = 0xC0;
// The sounds of bank 8.
constexpr std::int32_t ClimbSound = 5;
constexpr std::int32_t ShootSound = 1;
constexpr std::int32_t KnockSound = 3;
constexpr std::int32_t LandSound = 4;

std::vector<std::int32_t> integers(YamlReader& reader, const YamlNode& node,
                                   std::string_view what) {
  std::vector<std::int32_t> values;
  for (const YamlNode& value : reader.sequence(node, what)) {
    values.push_back(reader.integer<std::int32_t>(value));
  }
  return values;
}

std::int32_t at(const std::vector<std::int32_t>& values, std::int32_t index) {
  return index >= 0 && std::cmp_less(index, values.size()) ? values[static_cast<std::size_t>(index)]
                                                           : 0;
}

// The original keeps u and v in a byte.
std::int32_t wrap(std::int32_t value) {
  return ((value % PageSide) + PageSide) % PageSide;
}

} // namespace

core::Result<std::optional<BracketLayout>> parseBracketLayout(const core::YamlNode& stadium) {
  const YamlNode* node = stadium.find(BracketKey);
  if (node == nullptr) {
    return std::optional<BracketLayout>{};
  }
  YamlReader reader;
  BracketLayout layout;
  layout.rowY = integers(reader, reader.field(*node, RowYKey), RowYKey);
  layout.hopY = integers(reader, reader.field(*node, HopYKey), HopYKey);
  const auto paths = reader.sequence(reader.field(*node, PathsKey), PathsKey, Entrants);
  for (std::size_t slot = 0; slot < paths.size() && slot < Entrants; ++slot) {
    const auto xs = reader.sequence(reader.field(paths[slot], PathXKey), PathXKey, Steps);
    const auto flips = reader.sequence(reader.field(paths[slot], PathFlipKey), PathFlipKey, Steps);
    for (std::size_t step = 0; step < Steps && step < xs.size() && step < flips.size(); ++step) {
      layout.paths.at(slot).x.at(step) = reader.integer<std::int32_t>(xs[step]);
      layout.paths.at(slot).flip.at(step) = reader.integer<std::int32_t>(flips[step]) != 0;
    }
  }
  layout.spritePalettes =
      integers(reader, reader.field(*node, SpritePalettesKey), SpritePalettesKey);
  layout.projectileIcons =
      integers(reader, reader.field(*node, ProjectileIconsKey), ProjectileIconsKey);
  layout.projectilePalettes =
      integers(reader, reader.field(*node, ProjectilePalettesKey), ProjectilePalettesKey);
  if (reader.ok() && (layout.rowY.size() < Steps || layout.hopY.empty())) {
    reader.fail(*node, "the bracket has too few rows");
  }
  if (!reader.ok()) {
    return std::unexpected{reader.error(StadiumText)};
  }
  return std::optional<BracketLayout>{std::move(layout)};
}

BracketCell bracketSpriteCell(const BracketLayout& layout, std::int32_t species,
                              std::int32_t face) {
  // u = ((tile - 1) % 32) * 32 and v = ((tile - 1) / 8) * 32, both bytes; the page is tile / 64.
  const std::int32_t index = species - 1;
  BracketCell cell{.page = species / SpeciesPerPage,
                   .u = wrap(((index % (BlocksPerRow * 4)) * Block) + ((face & 1) * Cell)),
                   .v = wrap(((index / BlocksPerRow) * Block) + ((face / 2) * Cell)),
                   .width = Cell,
                   .height = Cell,
                   .palette = at(layout.spritePalettes, species)};
  if (cell.u == PageSide - Cell) {
    cell.width = Cell - 1;
  }
  if (cell.v == PageSide - Cell) {
    cell.height = Cell - 1;
  }
  return cell;
}

BracketCell bracketShotCell(const BracketLayout& layout, std::int32_t species) {
  const std::int32_t icon = at(layout.projectileIcons, species);
  return {.page = 1,
          .u = wrap((icon * ShotCell) - ShotCell),
          .v = ShotRow,
          .width = ShotCell,
          .height = ShotCell,
          .palette = at(layout.projectilePalettes, icon - 1)};
}

BracketAnimation::BracketAnimation(BracketLayout layout, const Bracket& bracket)
    : layout_{std::move(layout)}, species_{bracket.entrants}, player_{bracket.playerSlot},
      round1_{bracket.firstRound}, round2_{bracket.secondRound} {
  // STD_initializeBracket: each slot at the foot of its path, facing as its path says.
  for (std::size_t slot = 0; slot < Entrants; ++slot) {
    slots_.at(slot) = Slot{.x = layout_.paths.at(slot).x[0],
                           .y = rowY(0),
                           .step = 1,
                           .face = 0,
                           .flipped = layout_.paths.at(slot).flip[0],
                           .state = 0,
                           .timer = 0,
                           .counter = 0,
                           .hit = false,
                           .loser = false};
  }
}

void BracketAnimation::startIntro() {
  intro_ = true;
  introFrames_ = 0;
  fade_ = FullFade;
  card_ = FullFade;
  shade_ = FullFade;
}

void BracketAnimation::show(BracketShow /*show*/) {
  // STD_addBracket: the slots go on from where they were; the champion's climb is their state 7.
  intro_ = false;
  finished_ = false;
  fade_ = 0;
}

std::int32_t BracketAnimation::rowY(std::size_t step) const {
  return step < layout_.rowY.size() ? layout_.rowY[step] : 0;
}

void BracketAnimation::blink(Slot& slot) const {
  if (slot.timer % BlinkFrames == 0) {
    slot.face = (slot.face + 1) & 1;
  }
}

std::array<std::size_t, 2> BracketAnimation::pair() const {
  if (match_ < SecondRoundMatch) {
    const auto first = static_cast<std::size_t>((match_ - 1) * 2);
    return {first, first + 1};
  }
  if (match_ < FinalMatch) {
    const auto pair = static_cast<std::size_t>((match_ - SecondRoundMatch) * 2);
    return {round1_.at(pair), round1_.at(pair + 1)};
  }
  return {round2_[0], round2_[1]};
}

void BracketAnimation::tick() {
  if (intro_) {
    // STD_tickBracketIntro.
    introFrames_ = std::min(introFrames_ + 1, IntroFrames);
    if (introFrames_ >= IntroFadeFrame) {
      fade_ = std::max(fade_ - IntroFadeStep, 0);
      if (shade_ > IntroShadeFloor) {
        shade_ -= IntroFadeStep;
      }
      card_ = std::max(card_ - IntroFadeStep, 0);
    }
    return;
  }
  tickShots();
  // STD_tickBracketHitFlash runs before the board's update: it goes at frame 16 of the hit and
  // shows on every fifth.
  for (std::size_t slot = 0; slot < Entrants; ++slot) {
    const std::int32_t timer = slots_.at(slot).timer;
    if (flashes_.at(slot) && timer >= FlashFrames) {
      flashes_.at(slot) = false;
    }
    flashShown_.at(slot) = flashes_.at(slot) && timer % FlashPeriod == FlashPeriod - 1;
  }
  update();
}

void BracketAnimation::tickShots() {
  const std::int32_t speed = match_ < SecondRoundMatch ? ShotFirstRoundSpeed : ShotSecondRoundSpeed;
  for (std::size_t slot = 0; slot < Entrants; ++slot) {
    Shot& shot = shots_.at(slot);
    if (!shot.active) {
      continue;
    }
    const bool right = slots_.at(slot).flipped;
    shot.x += right ? speed : -speed;
    Slot& target = slots_.at(shot.target);
    if ((right && target.x - shot.x < ShotRightReach) ||
        (!right && shot.x - target.x < ShotLeftReach)) {
      target.hit = true;
      shot.active = false;
    }
  }
}

void BracketAnimation::shoot(std::size_t slot, std::size_t target, bool winner) {
  // STD_addBracketProjectile: the winner's shot marks its target as the loser.
  const Slot& shooter = slots_.at(slot);
  Shot& shot = shots_.at(slot);
  shot = Shot{.active = true, .x = 0, .y = shooter.y, .target = target, .doubled = winner};
  if (shooter.flipped) {
    shot.x = shooter.x + ShotRightStart;
  } else {
    shot.x = shooter.x + ShotLeftStart;
    if (!winner) {
      shot.y = shooter.y + ShotLowered;
    }
  }
  if (winner) {
    slots_.at(target).loser = true;
  }
}

void BracketAnimation::update() {
  const std::array<std::size_t, 2> fighters = pair();
  const bool partnerMatch = fighters[0] == player_ || fighters[1] == player_;
  for (std::size_t slot = 0; slot < Entrants; ++slot) {
    if (slot == fighters[0] || slot == fighters[1]) {
      step(slot, fighters, partnerMatch);
    }
  }
  if (partnerMatch && fade_ == FullFade && slots_.at(player_).state == 2) {
    const std::int32_t next = match_ != FinalMatch ? 6 : 7;
    for (const std::size_t slot : fighters) {
      slots_.at(slot).state = next;
      if (match_ == FinalMatch) {
        slots_.at(slot).timer = 0;
      }
    }
    finished_ = true;
  }
}

void BracketAnimation::step(std::size_t index, std::array<std::size_t, 2> fighters,
                            bool partnerMatch) {
  Slot& slot = slots_.at(index);
  const BracketLayout::Path& path = layout_.paths.at(index);
  const auto pathX = [&path](std::size_t step) { return step < Steps ? path.x.at(step) : 0; };
  const auto pathFlip = [&path](std::size_t step) { return step < Steps && path.flip.at(step); };
  switch (slot.state) {
  case 0:
    ++slot.timer;
    ++slot.counter;
    blink(slot);
    if (slot.counter >= WaitFrames) {
      slot.counter = 0;
      ++slot.state;
    }
    break;
  case 1:
    ++slot.timer;
    ++slot.counter;
    if (slot.counter == ClimbSoundFrame) {
      sounds_.push_back(ClimbSound);
    }
    if (slot.y != rowY(slot.step)) {
      --slot.y;
    }
    blink(slot);
    if (slot.y == rowY(slot.step) && slot.x == pathX(slot.step)) {
      slot.counter = 0;
      ++slot.state;
    }
    break;
  case 2:
    ++slot.timer;
    ++slot.counter;
    blink(slot);
    if (!partnerMatch) {
      if (slot.counter >= WaitFrames) {
        slot.face = FaceShoot;
        sounds_.push_back(ShootSound);
        slot.timer = 0;
        slot.counter = 0;
        slot.hit = false;
        const bool winner =
            match_ < SecondRoundMatch
                ? index == round1_.at(static_cast<std::size_t>(match_ - 1))
                : match_ < FinalMatch &&
                      index == round2_.at(static_cast<std::size_t>(match_ - SecondRoundMatch));
        shoot(index, index == fighters[0] ? fighters[1] : fighters[0], winner);
        ++slot.state;
      }
    } else {
      fade_ = std::min(fade_ + FadeStep, FullFade);
    }
    break;
  case 3:
    if (slot.face == FaceShoot) {
      ++slot.counter;
    } else {
      ++slot.timer;
      blink(slot);
    }
    if (slot.counter >= ShootPauseFrames) {
      slot.face = 0;
      slot.timer = 0;
      slot.counter = 0;
    }
    if (slot.hit) {
      slot.counter = 0;
      slot.timer = 0;
      ++slot.state;
      if (slot.loser) {
        // STD_addBracketHitFlash.
        flashes_.at(index) = true;
      } else {
        slot.flipped = !slot.flipped;
      }
    }
    break;
  case 4:
    ++slot.timer;
    if (slot.loser) {
      slot.face = FaceBeaten;
      if (slot.timer >= HitFrames) {
        slot.timer = 0;
        ++slot.state;
        sounds_.push_back(KnockSound);
      }
    } else {
      if (slot.timer == TurnFrame) {
        slot.flipped = !slot.flipped;
      }
      if (slot.timer >= HitFrames) {
        ++slot.step;
        slot.state += 2;
        if (slot.face == FaceShoot) {
          slot.face = 0;
        }
      }
    }
    break;
  case 5:
    if (std::cmp_less(slot.timer, layout_.hopY.size())) {
      slot.y = rowY(slot.step) + layout_.hopY.at(static_cast<std::size_t>(slot.timer++));
      if (slot.timer == HopSoundFrame) {
        sounds_.push_back(LandSound);
      }
    } else {
      ++slot.state;
    }
    break;
  case 6:
    if (partnerMatch) {
      if (result_ == 1) {
        if (index == player_) {
          slot.x = pathX(slot.step + 1);
          slot.face = 0;
          slot.flipped = pathFlip(slot.step + 1);
          slot.step += 2;
          slot.counter = 0;
          slot.timer = 0;
          slot.state = 0;
          if (match_ < SecondRoundMatch) {
            round1_.at(player_ / 2) = player_;
          }
          if (match_ < FinalMatch) {
            round2_.at(player_ / 4) = player_;
          }
          ++match_;
        } else {
          slot.y = rowY(slot.step - 1);
          slot.face = FaceBeaten;
        }
      } else if (index == player_) {
        slot.y = rowY(slot.step - 1);
        slot.face = FaceBeaten;
      } else {
        slot.x = pathX(slot.step - 1);
        slot.face = 0;
      }
    } else if (!slot.loser) {
      ++slot.timer;
      blink(slot);
      if (slots_.at(shots_.at(index).target).state == 6) {
        if (slot.x > pathX(slot.step)) {
          --slot.x;
        }
        if (slot.x < pathX(slot.step)) {
          ++slot.x;
        }
        if (slot.y == rowY(slot.step) && slot.x == pathX(slot.step)) {
          slot.flipped = pathFlip(slot.step);
          ++slot.step;
          slot.counter = 0;
          slot.timer = 0;
          slot.face = 0;
          slot.state = 0;
          ++match_;
        }
      }
    }
    break;
  case 7:
    ++slot.timer;
    if (index == player_) {
      slot.x = pathX(ChampionStep);
      slot.y = rowY(ChampionStep);
      slot.face = 0;
      slot.flipped = pathFlip(ChampionStep);
    } else {
      slot.x = pathX(RunnerUpStep);
      slot.y = rowY(RunnerUpStep);
      slot.flipped = pathFlip(RunnerUpStep);
      slot.face = FaceBeaten;
    }
    if (slot.timer >= ChampionFrames) {
      finished_ = true;
    }
    break;
  default:
    break;
  }
}

BracketAnimation::View BracketAnimation::view() const {
  View view;
  for (std::size_t slot = 0; slot < Entrants; ++slot) {
    const Slot& state = slots_.at(slot);
    view.slots.at(slot) = {.species = species_.at(slot),
                           .x = state.x,
                           .y = state.y,
                           .face = state.face,
                           .flipped = state.flipped};
    const Shot& shot = shots_.at(slot);
    if (shot.active && !intro_) {
      view.shots.push_back({.species = species_.at(slot),
                            .x = shot.x,
                            .y = shot.y,
                            .flipped = state.flipped,
                            .doubled = shot.doubled});
    }
    if (flashShown_.at(slot) && !intro_) {
      view.flashes.push_back(slot);
    }
  }
  view.fade = fade_;
  view.card = intro_ ? card_ : 0;
  view.shade = intro_ ? shade_ : 0;
  view.intro = intro_;
  return view;
}

std::vector<std::int32_t> BracketAnimation::takeSounds() {
  return std::exchange(sounds_, {});
}

} // namespace dwe::game::tournament
