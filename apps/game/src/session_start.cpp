#include "session_start.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_scenes.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include "messages.hpp"
#include "saves.hpp"

namespace dwe::app {
namespace {

std::chrono::sys_seconds wallClock() {
  return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
}

// A new playthrough's seed when --seed gives none; the log keeps it, so a bug report's playthrough
// can be played again with --seed.
std::uint64_t newSeed() {
  return static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
}

// Starts the playthrough itself: the slot's save, or a new game (AppFlow saves it to its slot).
core::Result<void> begin(session::GameSession& session, const PlayStart& start,
                         std::uint64_t seed) {
  if (start.load) {
    if (auto loaded = session.load(*start.load, start.backup); !loaded) {
      return loaded;
    }
    core::logInfo("{}", core::formatMessage(messages::SessionLoaded,
                                            start.backup ? game::save::slotBackupName(*start.load)
                                                         : game::save::slotFileName(*start.load)));
    return {};
  }
  if (auto started = session.startNewGame(start.choice.value_or(session::NewGameChoice{}));
      !started) {
    return started;
  }
  core::logInfo("{}", core::formatMessage(messages::SessionStarted, seed));
  return {};
}

// The ruleset a game plays with: a loaded game's is the one its save records, so the systems that
// build their tables once (the partner's species, the evolution's tables) build them for it; a new
// game's is the one the new game's rules screen chose, else the command line's.
std::string rulesetFor(const assets::VirtualFileSystem& files, const Options& options,
                       const PlayStart& play, game::save::SaveStorage* storage) {
  if (play.load && storage != nullptr) {
    if (auto settings = game::save::loadSaveSettings(files)) {
      const game::save::SaveSlots slots{*storage, *settings};
      auto file = play.backup ? slots.loadBackup(*play.load) : slots.load(*play.load);
      if (file && !file->header.rulesetId.empty()) {
        return file->header.rulesetId;
      }
    }
    return options.ruleset;
  }
  return play.rulesetId.value_or(options.ruleset);
}

} // namespace

std::unique_ptr<game::save::DiskSaveStorage> saveStorageFor(const Options& options,
                                                            bool keepsSaves) {
  if (!keepsSaves && !options.saves) {
    return nullptr;
  }
  auto folder = savesFolder(options);
  if (!folder) {
    core::logWarning("{}", core::formatMessage(messages::SessionNoSaves, folder.error().message));
    return nullptr;
  }
  return std::make_unique<game::save::DiskSaveStorage>(std::move(*folder));
}

core::Result<GameStart> startGame(const assets::VirtualFileSystem& files, const Options& options,
                                  bool keepsSaves, const PlayStart& play, audio::Audio* sound) {
  // A game played without saving has no slots: its autosaves and saves are dropped.
  GameStart start{.storage = play.withoutSaving ? nullptr : saveStorageFor(options, keepsSaves),
                  .session = nullptr,
                  .systems = session::createGameSystems(files)};
  start.systems.story->setStoryStage(options.storyStage);
  if (start.systems.scenes != nullptr) {
    start.systems.scenes->setNewGameChain(options.newGameChain);
  }
  const std::uint64_t seed = options.seed ? *options.seed : newSeed();
  auto session = session::GameSession::create(
      {.files = &files,
       .rulesetId = rulesetFor(files, options, play, start.storage.get()),
       .rules = std::nullopt,
       .species = nullptr,
       .seed = seed,
       .storage = start.storage.get(),
       .gameVersion = DWE_GAME_VERSION,
       .wallClock = wallClock,
       .sound = sound,
       .systems = std::move(start.systems.systems)});
  if (!session) {
    return core::fail(core::formatMessage(messages::SessionFailed, session.error().message));
  }
  start.session = std::move(*session);
  if (auto begun = begin(*start.session, play, seed); !begun) {
    return core::fail(core::formatMessage(messages::SessionFailed, begun.error().message));
  }
  if (options.clockSpeed) {
    start.session->clock().setSpeed(*options.clockSpeed);
  }
  if (start.systems.battle != nullptr) {
    start.systems.battle->useOriginalRolls(options.originalRolls);
  }
  return start;
}

} // namespace dwe::app
