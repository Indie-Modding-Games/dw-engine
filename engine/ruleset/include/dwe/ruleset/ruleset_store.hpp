#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace dwe::ruleset {

// The active ruleset of a running game, which can change while it runs (R3.4): reloaded from the packs, switched to
// another one or tuned value by value. A change that fails to load or to pass the checks is refused and the current
// ruleset stays. Every change that is accepted bumps the revision, so each system keeps the revision it read its
// section at and reads it again when the number moves.
class RulesetStore {
public:
    // Loads the ruleset `id` from the packs, with the checks of the game's systems. The packs must outlive the store.
    static core::Result<RulesetStore> load(const assets::VirtualFileSystem& vfs, std::string_view id,
                                           SectionChecks checks = {});

    // Takes a ruleset already loaded from the packs, such as one a simulation changed with its --set values, once it
    // passes the checks. The packs must outlive the store.
    static core::Result<RulesetStore> adopt(const assets::VirtualFileSystem& vfs, Ruleset ruleset,
                                            SectionChecks checks = {});

    [[nodiscard]] const Ruleset& current() const { return current_; }

    // 1 after the load, and one more for each accepted change.
    [[nodiscard]] std::uint64_t revision() const { return revision_; }

    // Reads the current ruleset again from the packs, after its files were edited. Tuned values are dropped: the files
    // are the truth.
    core::Result<void> reload();

    // Switches to another ruleset of the packs.
    core::Result<void> select(std::string_view id);

    // Sets the value at a path of a section to a YAML text ("90", "[2, 3]", "original") and checks the section again,
    // for the tuning panel. On an error nothing changes.
    core::Result<void> setValue(std::string_view section, std::string_view path, std::string_view yaml);

private:
    RulesetStore(const assets::VirtualFileSystem& vfs, Ruleset ruleset, SectionChecks checks)
        : vfs_(&vfs), current_(std::move(ruleset)), checks_(std::move(checks)) {}

    // Makes `ruleset` the current one.
    void replace(Ruleset ruleset);

    const assets::VirtualFileSystem* vfs_;
    Ruleset current_;
    SectionChecks checks_;
    std::uint64_t revision_ = 1;
};

} // namespace dwe::ruleset
