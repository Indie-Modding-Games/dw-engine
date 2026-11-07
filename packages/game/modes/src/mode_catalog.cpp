#include <dwe/assets/asset_id.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/modes/mode_catalog.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "messages.hpp"

namespace dwe::game::modes {

namespace {

constexpr std::string_view ModesFolder = "modes/";
constexpr std::string_view ManifestName = "/mode";

// The folder of a mode's manifest id (modes/<folder>/mode), or nothing for another asset.
std::optional<std::string_view> modeFolder(std::string_view id) {
  if (!id.starts_with(ModesFolder) || !id.ends_with(ManifestName)) {
    return std::nullopt;
  }
  const std::string_view folder =
      id.substr(ModesFolder.size(), id.size() - ModesFolder.size() - ManifestName.size());
  if (folder.empty() || folder.contains('/')) {
    return std::nullopt;
  }
  return folder;
}

bool hasRuleset(const assets::VirtualFileSystem& files, std::string_view ruleset) {
  const auto id = assets::AssetId::parse(std::format("rules/{}/ruleset", ruleset));
  return id && files.resolve(*id) != nullptr;
}

core::Result<GameMode> readMode(const assets::VirtualFileSystem& files, const assets::AssetId& id,
                                std::string_view folder) {
  auto bytes = files.read(id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  auto mode = parseGameMode(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()}, folder);
  if (!mode) {
    return core::fail(std::format("{}: {}", id.str(), mode.error().message));
  }
  for (const std::string& ruleset : mode->rulesets) {
    if (!hasRuleset(files, ruleset)) {
      return core::fail(core::formatMessage(messages::ModeNoRuleset, mode->id, ruleset));
    }
  }
  return mode;
}

} // namespace

ModeCatalog ModeCatalog::load(const assets::VirtualFileSystem& files) {
  std::vector<GameMode> modes;
  std::vector<SkippedMode> skipped;
  for (const assets::AssetId& id : files.ids()) {
    const auto folder = modeFolder(id.str());
    if (!folder) {
      continue;
    }
    if (auto mode = readMode(files, id, *folder)) {
      modes.push_back(std::move(*mode));
    } else {
      core::logError("{}",
                     core::formatMessage(messages::ModeSkipped, *folder, mode.error().message));
      skipped.push_back({.id = std::string{*folder}, .reason = mode.error().message});
    }
  }
  ModeCatalog catalog{std::move(modes), std::move(skipped)};
  std::string names;
  for (const GameMode& mode : catalog.modes()) {
    names += names.empty() ? mode.id : ", " + mode.id;
  }
  core::logInfo("{}", core::formatMessage(messages::ModesFound, catalog.modes().size(), names));
  return catalog;
}

ModeCatalog::ModeCatalog(std::vector<GameMode> modes, std::vector<SkippedMode> skipped)
    : modes_(std::move(modes)), skipped_(std::move(skipped)) {
  std::ranges::sort(modes_, [](const GameMode& a, const GameMode& b) {
    return a.order != b.order ? a.order < b.order : a.id < b.id;
  });
}

const GameMode* ModeCatalog::find(std::string_view id) const {
  const auto found = std::ranges::find(modes_, id, &GameMode::id);
  return found == modes_.end() ? nullptr : &*found;
}

const GameMode* ModeCatalog::defaultMode() const {
  if (const GameMode* original = find(OriginalMode)) {
    return original;
  }
  return modes_.empty() ? nullptr : &modes_.front();
}

} // namespace dwe::game::modes
