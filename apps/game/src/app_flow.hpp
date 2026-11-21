#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/session/game_session.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace dwe::app {

// What a game starts from: a new game, or the save in a slot.
struct PlayStart {
  std::optional<game::save::SlotId> load;
  // Loads the slot's backup, the save before its last one, instead of its damaged save.
  bool backup = false;
  // The empty slot a new game is saved to once it started, as the original writes the card's file
  // when a game begins.
  std::optional<game::save::SlotId> saveTo;
  // What a new game's opening asked (main.c: newGameScene); nothing when it did not run.
  std::optional<session::NewGameChoice> choice;
  // Play without saving (the title screen's slot list): the game gets no save slots, so it writes
  // nothing, no autosave either, as its question warns.
  bool withoutSaving = false;
  // The ruleset a new game plays with, as the new game's rules screen chose it (the original, or
  // the original with the hidden content); nothing: the command line's --ruleset. A loaded game
  // plays with the ruleset its save records.
  std::optional<std::string> rulesetId = std::nullopt;
  // The game mode a new game plays, as the new game's mode list chose it (packages/game/modes);
  // nothing: the original's story. A loaded game plays the mode its save records.
  std::optional<std::string> modeId = std::nullopt;
  // A cooperative guest starts from the host's shared world in memory; this text is never written
  // into the guest's local save slots.
  std::optional<std::string> networkSaveText = std::nullopt;
  // The host's own save, loaded by the co-op room's start: played with the guest in it and saved as
  // the single-player game it is (docs/architecture/multiplayer.md, "Saves in co-op").
  bool coopHost = false;

  // A co-op guest's game: the host's world, played without saving anything.
  [[nodiscard]] bool coopGuest() const { return networkSaveText.has_value(); }
  // A game the co-op room started. Only these join the room: any other game stays single-player,
  // even with a room still open.
  [[nodiscard]] bool coop() const { return coopHost || coopGuest(); }

  friend bool operator==(const PlayStart&, const PlayStart&) = default;
};

// The app's states (docs/architecture/save-and-load.md, 3.2): the title screen, with no game,
// and a game being played. A game is built whole and torn down whole, so a second game never sees
// what the first left behind (queued scripts, a battle, the clock). Play::saveTo(slot) saves a
// game that started.
template <class Play>
class AppFlow {
public:
  using Starter = std::function<core::Result<std::unique_ptr<Play>>(const PlayStart&)>;

  explicit AppFlow(Starter starter) : starter_(std::move(starter)) {}

  // The game being played; nothing at the title screen.
  [[nodiscard]] Play* playing() const { return play_.get(); }

  // Leaves the game being played, if any, and starts another. The new game is installed only once
  // it started and was saved to its slot, so a load or a save that fails leaves the app at the
  // title screen and a game that failed to start leaves no slot written.
  core::Result<void> play(const PlayStart& start) {
    // The old game goes first: two games would share the field's GPU memory, the HUD and the
    // tuning panel.
    toTitle();
    saveFailed_ = false;
    auto started = starter_(start);
    if (!started) {
      return std::unexpected{started.error()};
    }
    // A co-op guest's game is never written, not even to a slot it was handed.
    if (start.saveTo && !start.coopGuest()) {
      if (auto saved = (*started)->saveTo(*start.saveTo); !saved) {
        saveFailed_ = true;
        return saved;
      }
    }
    play_ = std::move(*started);
    return {};
  }

  // Whether the last play() failed at saving the new game to its slot, not at starting it.
  [[nodiscard]] bool saveFailed() const { return saveFailed_; }

  // Ends the game being played without saving it.
  void toTitle() {
    if (play_ && beforeEnd_) {
      beforeEnd_(*play_);
    }
    play_.reset();
  }

  void setBeforeEnd(std::function<void(Play&)> hook) { beforeEnd_ = std::move(hook); }

private:
  Starter starter_;
  std::function<void(Play&)> beforeEnd_;
  std::unique_ptr<Play> play_;
  bool saveFailed_ = false;
};

} // namespace dwe::app
