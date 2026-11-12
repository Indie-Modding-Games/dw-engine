#include "tournament_bracket_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/session/session_music.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace dwe::app {

namespace {

constexpr std::string_view Revision = "tournament_bracket.revision";
constexpr std::string_view Prefix = "tournament_bracket.";
// playSound(8, n): the board's sounds are in the map's slot, where loadMapSounds(0x10) put them.
constexpr std::int64_t BoardSoundSlot = 8;
// The engine's texts of the board (localization/<language>/tournament.yaml), shown by skins that
// name the round.
constexpr std::string_view RoundKey = "tournament.bracket.round";
constexpr std::string_view FinalKey = "tournament.bracket.final";
constexpr std::string_view ChampionKey = "tournament.bracket.champion";
// The cup's name by its grade, for skins that write it on the intro's card.
constexpr std::string_view CupKey = "tournament.bracket.cup";
constexpr std::size_t FinalRound = 2;

std::string setting(std::string_view name) {
  return std::format("{}{}", Prefix, name);
}

std::string rectOf(const game::tournament::BracketCell& cell) {
  return std::format("{} {} {} {}", cell.u, cell.v, cell.width, cell.height);
}

const char* phaseName(std::uint8_t phase) {
  constexpr std::array<const char*, 4> Names{"hidden", "intro", "board", "tail"};
  return phase < Names.size() ? Names.at(phase) : "?";
}

} // namespace

bool TournamentBracketScreen::open() {
  animation_.reset();
  const session::TournamentAdapter* tournament = desc_.tournament;
  const game::tournament::BracketLayout* layout =
      tournament != nullptr ? tournament->bracketLayout() : nullptr;
  const game::tournament::Bracket* bracket =
      tournament != nullptr ? tournament->bracket() : nullptr;
  if (layout == nullptr || tournament->bracketRules() == nullptr || bracket == nullptr ||
      desc_.ui == nullptr) {
    core::logWarning("[port:tournament] STD_initializeBracket (std_main.c): no bracket tables in "
                     "the extraction or no bracket rules; the cup is fought without the board");
    return false;
  }
  rules_ = *tournament->bracketRules();
  card_ = tournament->cardImage();
  grade_ = tournament->grade(tournament->cup());
  species_ = bracket->entrants;
  round_ = 0;
  show_ = game::tournament::BracketShow::Match;
  musicPlayed_ = false;
  animation_.emplace(*layout, *bracket);
  // STD_tournamentMain: loadMapSounds(0x10), then STD_addBracketIntro.
  if (desc_.session != nullptr) {
    desc_.session->useSceneSounds(rules_->soundBank);
  }
  playMusic(rules_->introTrack);
  animation_->startIntro();
  phase_ = Phase::Intro;
  seconds_ = 0.0;
  screens::publish(*desc_.ui, settings(), Revision);
  desc_.ui->show(TournamentBracketDocument);
  core::logInfo("[port:tournament] STD_addBracketIntro (std_main.c:3672): cup {} card '{}'",
                tournament->cup(), card_);
  return true;
}

void TournamentBracketScreen::show(game::tournament::BracketShow show) {
  if (!animation_) {
    return;
  }
  show_ = show;
  round_ = desc_.tournament != nullptr ? desc_.tournament->round() : 0;
  // A match gave slot 8 to the battle: loadMapSounds(0x10) again.
  if (desc_.session != nullptr) {
    desc_.session->useSceneSounds(rules_->soundBank);
  }
  playMusic(show == game::tournament::BracketShow::Champion ? rules_->championTrack
                                                            : rules_->matchTrack);
  animation_->show(show);
  phase_ = Phase::Board;
  tailFrames_ = 0;
  seconds_ = 0.0;
  screens::publish(*desc_.ui, settings(), Revision);
  desc_.ui->show(TournamentBracketDocument);
}

void TournamentBracketScreen::matchOver(bool won) {
  if (animation_) {
    animation_->matchOver(won);
  }
}

void TournamentBracketScreen::close() {
  if (!animation_) {
    return;
  }
  if (playing()) {
    hide();
  }
  animation_.reset();
  if (desc_.session == nullptr) {
    return;
  }
  // STD_deinitializeTournament: the map's sounds; after the champion's climb its music still
  // plays, and the map's comes back.
  desc_.session->useSceneSounds(std::nullopt);
  session::SessionMusic* music = desc_.session->music();
  if (music != nullptr && musicPlayed_ && show_ == game::tournament::BracketShow::Champion) {
    music->replayActive();
  }
}

void TournamentBracketScreen::tick(double seconds) {
  if (phase_ == Phase::Hidden || !animation_) {
    return;
  }
  const double frameSeconds = 1.0 / static_cast<double>(rules_->framesPerSecond);
  seconds_ += seconds;
  bool changed = false;
  while (seconds_ >= frameSeconds && phase_ != Phase::Hidden) {
    seconds_ -= frameSeconds;
    frame();
    changed = true;
  }
  if (changed && phase_ != Phase::Hidden) {
    screens::publish(*desc_.ui, settings(), Revision);
  }
}

void TournamentBracketScreen::frame() {
  switch (phase_) {
  case Phase::Hidden:
    return;
  case Phase::Intro:
    animation_->tick();
    sounds();
    // STD_tickFrames(0x78), STD_removeBracketIntro.
    if (animation_->introDone()) {
      hide();
    }
    return;
  case Phase::Board:
    animation_->tick();
    sounds();
    if (!animation_->finished()) {
      return;
    }
    if (show_ == game::tournament::BracketShow::Champion) {
      // fadeToBlack(10) and 11 frames before STD_removeBracket(2).
      phase_ = Phase::Tail;
      tailFrames_ = 0;
      return;
    }
    // STD_removeBracket(1): the music stops for the match.
    stopMusic();
    hide();
    return;
  case Phase::Tail:
    if (++tailFrames_ >= rules_->championTailFrames) {
      hide();
    }
    return;
  }
}

void TournamentBracketScreen::hide() {
  phase_ = Phase::Hidden;
  screens::publish(*desc_.ui, settings(), Revision);
  desc_.ui->hide(TournamentBracketDocument);
}

void TournamentBracketScreen::playMusic(std::int32_t track) {
  session::SessionMusic* music = desc_.session != nullptr ? desc_.session->music() : nullptr;
  if (music == nullptr) {
    return;
  }
  // stopBGM(), playMusic(0x1d, track).
  music->playSong({.font = rules_->musicFont, .track = track});
  musicPlayed_ = true;
}

void TournamentBracketScreen::stopMusic() const {
  session::SessionMusic* music = desc_.session != nullptr ? desc_.session->music() : nullptr;
  if (music != nullptr) {
    music->stopSong();
  }
}

void TournamentBracketScreen::sounds() {
  game::story::SoundPort* port = desc_.session != nullptr ? desc_.session->sound() : nullptr;
  for (const std::int32_t sound : animation_->takeSounds()) {
    if (port != nullptr) {
      port->playSound(BoardSoundSlot, sound);
    }
  }
}

std::string TournamentBracketScreen::pageSource(std::int32_t page, std::int32_t palette) const {
  const std::size_t index = std::clamp<std::size_t>(static_cast<std::size_t>(std::max(page, 0)), 0,
                                                    rules_->pages.size() - 1);
  return std::format("/{}?palette={}", rules_->pages.at(index), palette);
}

std::string TournamentBracketScreen::speciesName(std::int32_t number) const {
  if (desc_.tournament == nullptr) {
    return {};
  }
  const std::string id = desc_.tournament->speciesId(number);
  if (id.empty()) {
    return {};
  }
  const screens::NameKeys names{desc_.species, nullptr, nullptr};
  return localization::tr(names.species(id));
}

std::string TournamentBracketScreen::label() const {
  if (phase_ == Phase::Intro) {
    return {};
  }
  if (show_ == game::tournament::BracketShow::Champion) {
    return localization::tr(ChampionKey);
  }
  if (round_ >= FinalRound) {
    return localization::tr(FinalKey);
  }
  const std::array<localization::TextArgument, 1> arguments{
      {{.name = "round", .value = std::to_string(round_ + 1)}}};
  return localization::tr(RoundKey, arguments);
}

screens::ScreenSettings TournamentBracketScreen::settings() const {
  screens::ScreenSettings out{
      {.name = setting("phase"), .value = std::string{phaseName(std::to_underlying(phase_))}},
  };
  if (phase_ == Phase::Hidden || !animation_) {
    return out;
  }
  const game::tournament::BracketAnimation::View view = animation_->view();
  const game::tournament::BracketLayout& layout = *desc_.tournament->bracketLayout();
  const auto number = [](auto value) { return static_cast<double>(value); };
  out.push_back({.name = setting("fade"), .value = number(view.fade)});
  out.push_back({.name = setting("card"), .value = number(view.card)});
  out.push_back({.name = setting("shade"), .value = number(view.shade)});
  out.push_back({.name = setting("intro"), .value = view.intro});
  // fadeToBlack(10) after the champion's climb.
  const double blackout =
      phase_ == Phase::Tail && rules_->championFadeFrames > 0
          ? std::min(1.0, number(tailFrames_) / number(rules_->championFadeFrames))
          : 0.0;
  out.push_back({.name = setting("blackout"), .value = blackout});
  out.push_back({.name = setting("board"), .value = std::format("/{}", rules_->board)});
  out.push_back({.name = setting("card_src"),
                 .value = card_.empty() ? std::string{} : std::format("/{}", card_)});
  out.push_back({.name = setting("label"), .value = label()});
  const std::array<localization::TextArgument, 1> cup{{{.name = "grade", .value = grade_}}};
  out.push_back({.name = setting("cup"),
                 .value = grade_.empty() ? std::string{} : localization::tr(CupKey, cup)});
  for (std::size_t slot = 0; slot < view.slots.size(); ++slot) {
    const auto& shown = view.slots.at(slot);
    const game::tournament::BracketCell cell =
        game::tournament::bracketSpriteCell(layout, shown.species, shown.face);
    const std::string base = std::format("slot.{}.", slot);
    out.push_back({.name = setting(base + "x"), .value = number(shown.x)});
    out.push_back({.name = setting(base + "y"), .value = number(shown.y)});
    out.push_back({.name = setting(base + "src"), .value = pageSource(cell.page, cell.palette)});
    out.push_back({.name = setting(base + "rect"), .value = rectOf(cell)});
    out.push_back({.name = setting(base + "flip"), .value = shown.flipped});
    out.push_back({.name = setting(base + "name"), .value = speciesName(species_.at(slot))});
  }
  out.push_back({.name = setting("shots"), .value = number(view.shots.size())});
  for (std::size_t shot = 0; shot < view.shots.size(); ++shot) {
    const auto& shown = view.shots[shot];
    const game::tournament::BracketCell cell =
        game::tournament::bracketShotCell(layout, shown.species);
    const std::string base = std::format("shot.{}.", shot);
    out.push_back({.name = setting(base + "x"), .value = number(shown.x)});
    out.push_back({.name = setting(base + "y"), .value = number(shown.y)});
    out.push_back({.name = setting(base + "src"), .value = pageSource(cell.page, cell.palette)});
    out.push_back({.name = setting(base + "rect"), .value = rectOf(cell)});
    out.push_back({.name = setting(base + "flip"), .value = shown.flipped});
    out.push_back({.name = setting(base + "doubled"), .value = shown.doubled});
  }
  out.push_back({.name = setting("flashes"), .value = number(view.flashes.size())});
  out.push_back({.name = setting("flash_src"),
                 .value = pageSource(rules_->flash.page, rules_->flash.palette)});
  out.push_back({.name = setting("flash_rect"), .value = rectOf(rules_->flash)});
  for (std::size_t flash = 0; flash < view.flashes.size(); ++flash) {
    const auto& slot = view.slots.at(view.flashes[flash]);
    out.push_back({.name = setting(std::format("flash.{}.x", flash)), .value = number(slot.x)});
    out.push_back({.name = setting(std::format("flash.{}.y", flash)), .value = number(slot.y)});
  }
  return out;
}

} // namespace dwe::app
