#include "session_start.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/save/slots.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include "messages.hpp"
#include "saves.hpp"

namespace dwe::app {
namespace {

// The save storage of the game, or nothing when it is not saved.
std::unique_ptr<game::save::DiskSaveStorage> storageFor(const Options& options, bool keepsSaves) {
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

std::chrono::sys_seconds wallClock() {
    return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
}

// A new playthrough's seed when --seed gives none; the log keeps it, so a bug report's playthrough can be played again
// with --seed.
std::uint64_t newSeed() {
    return static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
}

} // namespace

core::Result<GameStart> startGame(const assets::VirtualFileSystem& files, const Options& options, bool keepsSaves) {
    GameStart start{
        .storage = storageFor(options, keepsSaves), .session = nullptr, .systems = session::createGameSystems(files)};
    const std::uint64_t seed = options.seed ? *options.seed : newSeed();
    auto session = session::GameSession::create({.files = &files,
                                                 .rulesetId = options.ruleset,
                                                 .rules = std::nullopt,
                                                 .species = nullptr,
                                                 .seed = seed,
                                                 .storage = start.storage.get(),
                                                 .gameVersion = DWE_GAME_VERSION,
                                                 .wallClock = wallClock,
                                                 .systems = std::move(start.systems.systems)});
    if (!session) {
        return core::fail(core::formatMessage(messages::SessionFailed, session.error().message));
    }
    start.session = std::move(*session);
    if (options.load) {
        if (auto loaded = start.session->load(*options.load); !loaded) {
            return core::fail(core::formatMessage(messages::SessionFailed, loaded.error().message));
        }
        core::logInfo("{}", core::formatMessage(messages::SessionLoaded, game::save::slotFileName(*options.load)));
    } else {
        if (auto started = start.session->startNewGame(); !started) {
            return core::fail(core::formatMessage(messages::SessionFailed, started.error().message));
        }
        core::logInfo("{}", core::formatMessage(messages::SessionStarted, seed));
    }
    if (options.clockSpeed) {
        start.session->clock().setSpeed(*options.clockSpeed);
    }
    return start;
}

} // namespace dwe::app
