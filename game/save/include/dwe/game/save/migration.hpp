#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace dwe::game::save {

// One step of a migration: turns saved state of version N into saved state of version N + 1.
using MigrationStep = std::function<core::Result<core::YamlNode>(core::YamlNode state)>;

// The versions of one kind of saved state (a section, or the file format itself) and how to bring an old one up to
// the current version. The current version is the oldest one plus the number of steps, so a new version cannot be
// declared without the step that reaches it.
class MigrationChain {
public:
    // A chain at version 1, without steps.
    MigrationChain() = default;

    // A chain whose oldest readable version is `oldest`: an older one is an error. A system that drops very old
    // migrations starts its chain at the first version it still reads.
    explicit MigrationChain(std::int32_t oldest);

    // Adds the step from the current version to the next one, which becomes the current version.
    MigrationChain& add(MigrationStep step);

    // The version saved state is written at.
    [[nodiscard]] std::int32_t current() const;

    // The oldest version that can be migrated.
    [[nodiscard]] std::int32_t oldest() const { return oldest_; }

    // Brings state of `version` up to the current version, one step at a time. A version newer than the current one
    // or older than the oldest is an error; `what` names the state in errors ("section 'partner'").
    [[nodiscard]] core::Result<core::YamlNode> migrate(core::YamlNode state, std::int32_t version,
                                                       std::string_view what) const;

private:
    std::int32_t oldest_ = 1;
    std::vector<MigrationStep> steps_;
};

// For migration steps: a YamlNode mapping can set keys but not remove them.

// A copy of a mapping without one key; the other keys keep their order.
core::YamlNode withoutKey(const core::YamlNode& mapping, std::string_view key);

// A copy of a mapping with a key renamed in its place; the mapping as it is if it does not have the key.
core::YamlNode renamedKey(const core::YamlNode& mapping, std::string_view from, std::string_view to);

} // namespace dwe::game::save
