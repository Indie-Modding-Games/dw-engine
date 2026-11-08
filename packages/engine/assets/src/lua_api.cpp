#include <dwe/assets/lua_api.hpp>
#include <dwe/core/message.hpp>

#include <algorithm>

#include "messages.hpp"

namespace dwe::assets {

core::Result<void> checkApiCompatibility(const ApiRequirement& required, const Version& provided) {
  const bool sameMajor = required.major == provided.major;
  const bool recentEnough = !required.minimumMinor || *required.minimumMinor <= provided.minor;
  if (!sameMajor || !recentEnough) {
    return core::fail(core::formatMessage(messages::IncompatibleApi, formatApiRequirement(required),
                                          formatVersion(provided)));
  }
  return {};
}

core::Result<void> checkApiCompatibility(std::string_view required, const Version& provided) {
  auto requirement = parseApiRequirement(required);
  if (!requirement) {
    return std::unexpected{requirement.error()};
  }
  return checkApiCompatibility(*requirement, provided);
}

core::Result<void> checkPackApi(const PackManifest& pack, const Version& provided) {
  if (!pack.api) {
    // The engine's own packs follow the engine; a mod says which API it was written for (#115).
    if (pack.layer == Layer::Mods || pack.layer == Layer::User) {
      return core::fail(core::formatMessage(messages::ModWithoutApi, pack.id, layerName(pack.layer),
                                            SupportedLuaApi.major, SupportedLuaApi.major,
                                            SupportedLuaApi.minor));
    }
    return {};
  }
  if (auto compatible = checkApiCompatibility(*pack.api, provided); !compatible) {
    return core::fail(
        core::formatMessage(messages::PackNeedsOtherApi, pack.id, compatible.error().message));
  }
  return {};
}

std::vector<std::string> packsWithLua(const VirtualFileSystem& files) {
  std::vector<std::string> packs;
  for (const AssetId& id : files.ids()) {
    for (const AssetSource& source : files.sources(id)) {
      if (source.file.extension() == ".lua" &&
          std::ranges::find(packs, source.packId) == packs.end()) {
        packs.push_back(source.packId);
      }
    }
  }
  return packs;
}

bool mayLeaveOutApi(const MountedPack& pack, std::span<const std::string> scripted) {
  if (pack.manifest.api) {
    return false;
  }
  return pack.origin == PackOrigin::Engine ||
         std::ranges::find(scripted, pack.manifest.id) == scripted.end();
}

} // namespace dwe::assets
