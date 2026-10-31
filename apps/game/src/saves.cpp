#include "saves.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/platform/platform.hpp>
#include <dwe/session/slot_names.hpp>

#include <cstdlib>
#include <string>

#include "common.hpp"
#include "content_mount.hpp"
#include "messages.hpp"
#include "slot_description.hpp"

namespace dwe::app {

namespace {

// The saves folder in the game's user data folder (common.hpp).
constexpr std::string_view SavesFolderName = "saves";

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
  game::save::SaveSlots slots{storage, *settings};
  slots.setSummaryReader(session::summaryFromSections);
  core::logInfo(messages::SavesFolder, assets::pathText(*folder), settings->manualSlots,
                settings->autosaveSlots);
  for (const game::save::SlotInfo& info : slots.list()) {
    core::logInfo(messages::SlotLine, describeSlot(info),
                  info.hasBackup ? messages::HasBackup : std::string_view{});
  }
  return EXIT_SUCCESS;
}

} // namespace dwe::app
