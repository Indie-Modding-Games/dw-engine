#include "saves.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/platform/platform.hpp>

#include <chrono>
#include <cstdlib>
#include <format>
#include <string>

#include "common.hpp"
#include "content_mount.hpp"
#include "messages.hpp"

namespace dwe::app {

namespace {

// The saves folder in the game's user data folder (common.hpp).
constexpr std::string_view SavesFolderName = "saves";

std::string describeSlot(const game::save::SlotInfo& info) {
    using game::save::SlotInfo;
    const std::string name = game::save::slotFileName(info.slot);
    if (info.state == SlotInfo::State::Damaged) {
        return info.problem;
    }
    if (!info.header) {
        return std::format(messages::EmptySlot, name);
    }
    const game::save::SaveHeader& header = *info.header;
    const std::chrono::hh_mm_ss played{std::chrono::seconds{header.playSeconds}};
    return std::format(messages::SavedSlot, name, header.label, played.hours().count(), played.minutes().count(),
                       header.rulesetId, header.updated, header.gameVersion);
}

} // namespace

core::Result<std::filesystem::path> savesFolder(const Options& options) {
    if (options.saves) {
        return *options.saves;
    }
    return platform::userDataFolder(UserDataOrganization, UserDataApplication)
        .transform([](const std::filesystem::path& data) { return data / SavesFolderName; });
}

int listSaves(const Options& options) {
    const auto content = mountGameContent(options);
    if (!content) {
        core::logError(messages::CannotMountContent, content.error().message);
        return EXIT_FAILURE;
    }
    const auto settings = game::save::loadSaveSettings((*content)->files());
    if (!settings) {
        core::logError(messages::CannotReadSaveSettings, settings.error().message);
        return EXIT_FAILURE;
    }
    const auto folder = savesFolder(options);
    if (!folder) {
        core::logError("{}", folder.error().message);
        return EXIT_FAILURE;
    }
    game::save::DiskSaveStorage storage{*folder};
    const game::save::SaveSlots slots{storage, *settings};
    core::logInfo(messages::SavesFolder, assets::pathText(*folder), settings->manualSlots, settings->autosaveSlots);
    for (const game::save::SlotInfo& info : slots.list()) {
        core::logInfo(messages::SlotLine, describeSlot(info),
                      info.hasBackup ? messages::HasBackup : std::string_view{});
    }
    return EXIT_SUCCESS;
}

} // namespace dwe::app
