#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/game_systems.hpp>

#include <memory>

#include "options.hpp"

namespace dwe::app {

// The game the app plays and the folder it saves to.
struct GameStart {
  // Nothing when the game is not saved: an autotest without --saves, or no user data folder.
  std::unique_ptr<game::save::DiskSaveStorage> storage;
  std::unique_ptr<session::GameSession> session;
  // The session's adapters by type, for the battle and the partner's menus; the session owns them.
  session::GameSystems systems;
};

// Starts the game the command line asks for (--new-game, --load, --clock-speed, --ruleset) on the
// packs, which must outlive it. `keepsSaves` is false for an autotest, which writes no save unless
// --saves names a folder.
core::Result<GameStart> startGame(const assets::VirtualFileSystem& files, const Options& options,
                                  bool keepsSaves);

} // namespace dwe::app
