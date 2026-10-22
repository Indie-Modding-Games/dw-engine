#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/game_systems.hpp>

#include <memory>
#include <optional>

#include "app_flow.hpp"
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

// The folder the game saves to, or nothing when it is not saved. `keepsSaves` is false for an
// autotest, which writes no save unless --saves names a folder.
std::unique_ptr<game::save::DiskSaveStorage> saveStorageFor(const Options& options,
                                                            bool keepsSaves);

// Starts the game `play` asks for with the command line's --clock-speed, --ruleset and --seed on
// the packs, which must outlive it.
core::Result<GameStart> startGame(const assets::VirtualFileSystem& files, const Options& options,
                                  bool keepsSaves, const PlayStart& play);

} // namespace dwe::app
