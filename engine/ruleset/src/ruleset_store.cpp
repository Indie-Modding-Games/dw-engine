#include <dwe/ruleset/messages.hpp>
#include <dwe/ruleset/ruleset_store.hpp>

#include <utility>

namespace dwe::ruleset {

core::Result<RulesetStore> RulesetStore::load(const assets::VirtualFileSystem& vfs, std::string_view id,
                                              SectionChecks checks) {
    auto ruleset = loadRuleset(vfs, id, checks);
    if (!ruleset) {
        return std::unexpected{ruleset.error()};
    }
    return RulesetStore{vfs, std::move(*ruleset), std::move(checks)};
}

core::Result<RulesetStore> RulesetStore::adopt(const assets::VirtualFileSystem& vfs, Ruleset ruleset,
                                               SectionChecks checks) {
    if (auto checked = checkRuleset(ruleset, checks); !checked) {
        return std::unexpected{checked.error()};
    }
    return RulesetStore{vfs, std::move(ruleset), std::move(checks)};
}

core::Result<void> RulesetStore::reload() {
    return select(std::string{current_.id()});
}

core::Result<void> RulesetStore::select(std::string_view id) {
    auto ruleset = loadRuleset(*vfs_, id, checks_);
    if (!ruleset) {
        return std::unexpected{ruleset.error()};
    }
    replace(std::move(*ruleset));
    return {};
}

core::Result<void> RulesetStore::setValue(std::string_view section, std::string_view path, std::string_view yaml) {
    Ruleset tuned = current_;
    if (auto set = tuned.setValue(section, path, yaml); !set) {
        return set;
    }
    if (auto checked = checkRuleset(tuned, checks_); !checked) {
        return core::fail(messages::formatMessage(messages::TunedValue, section, path, yaml, checked.error().message));
    }
    replace(std::move(tuned));
    return {};
}

void RulesetStore::replace(Ruleset ruleset) {
    current_ = std::move(ruleset);
    ++revision_;
}

} // namespace dwe::ruleset
