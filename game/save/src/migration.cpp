#include <dwe/game/save/migration.hpp>

#include <format>
#include <utility>

#include "messages.hpp"

namespace dwe::game::save {

MigrationChain::MigrationChain(std::int32_t oldest) : oldest_{oldest} {}

MigrationChain& MigrationChain::add(MigrationStep step) {
  steps_.push_back(std::move(step));
  return *this;
}

std::int32_t MigrationChain::current() const {
  return oldest_ + static_cast<std::int32_t>(steps_.size());
}

core::Result<core::YamlNode> MigrationChain::migrate(core::YamlNode state, std::int32_t version,
                                                     std::string_view what) const {
  if (version > current()) {
    return core::fail(std::format(messages::VersionTooNew, what, version, current()));
  }
  if (version < oldest_) {
    return core::fail(std::format(messages::VersionTooOld, what, version, oldest_));
  }
  for (std::int32_t from = version; from < current(); ++from) {
    auto next = steps_[static_cast<std::size_t>(from - oldest_)](std::move(state));
    if (!next) {
      return core::fail(
          std::format(messages::MigrationFailed, what, from, from + 1, next.error().message));
    }
    state = std::move(*next);
  }
  return state;
}

core::YamlNode withoutKey(const core::YamlNode& mapping, std::string_view key) {
  if (!mapping.isMapping()) {
    return mapping;
  }
  core::YamlNode result = core::YamlNode::mapping();
  for (const core::YamlEntry& entry : mapping.entries()) {
    if (entry.key != key) {
      result.set(entry.key, entry.value);
    }
  }
  return result;
}

core::YamlNode renamedKey(const core::YamlNode& mapping, std::string_view from,
                          std::string_view to) {
  if (!mapping.isMapping() || mapping.find(from) == nullptr) {
    return mapping;
  }
  core::YamlNode result = core::YamlNode::mapping();
  for (const core::YamlEntry& entry : mapping.entries()) {
    if (entry.key == from) {
      result.set(std::string{to}, entry.value);
    } else if (entry.key != to) {
      result.set(entry.key, entry.value);
    }
  }
  return result;
}

} // namespace dwe::game::save
