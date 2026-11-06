#include <dwe/assets/lua_api.hpp>
#include <dwe/core/message.hpp>

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
    return {};
  }
  if (auto compatible = checkApiCompatibility(*pack.api, provided); !compatible) {
    return core::fail(
        core::formatMessage(messages::PackNeedsOtherApi, pack.id, compatible.error().message));
  }
  return {};
}

} // namespace dwe::assets
