#include <dwe/game/save/sections.hpp>

#include <algorithm>
#include <format>
#include <utility>

#include "messages.hpp"

namespace dwe::game::save {

core::Result<void> SectionSerializer::readMissing() {
    return core::fail(std::string{messages::SectionMissing});
}

const SaveSections::Entry* SaveSections::find(std::string_view name) const {
    const auto found = std::ranges::find(entries_, name, &Entry::name);
    return found == entries_.end() ? nullptr : &*found;
}

core::Result<void> SaveSections::add(std::string name, SectionSerializer& serializer, MigrationChain migrations) {
    if (!isValidName(name)) {
        return core::fail(std::format(messages::BadSectionName, name));
    }
    if (find(name) != nullptr) {
        return core::fail(std::format(messages::SectionRegisteredTwice, name));
    }
    entries_.push_back({.name = std::move(name), .serializer = &serializer, .migrations = std::move(migrations)});
    return {};
}

core::Result<void> SaveSections::write(SaveFile& file) const {
    for (const Entry& entry : entries_) {
        auto state = entry.serializer->write();
        if (!state) {
            return core::fail(std::format(messages::InSection, entry.name, state.error().message));
        }
        file.sections.insert_or_assign(entry.name,
                                       SavedSection{.version = entry.migrations.current(), .state = std::move(*state)});
    }
    return {};
}

core::Result<SaveFile> SaveSections::migrate(SaveFile file) const {
    for (auto& [name, section] : file.sections) {
        const Entry* entry = find(name);
        if (entry == nullptr) {
            continue;
        }
        auto state = entry->migrations.migrate(std::move(section.state), section.version,
                                               std::format(messages::SectionWhat, name));
        if (!state) {
            return std::unexpected{state.error()};
        }
        section = {.version = entry->migrations.current(), .state = std::move(*state)};
    }
    return file;
}

core::Result<void> SaveSections::read(const SaveFile& file) const {
    auto migrated = migrate(file);
    if (!migrated) {
        return std::unexpected{migrated.error()};
    }
    for (const Entry& entry : entries_) {
        const auto section = migrated->sections.find(entry.name);
        const auto read = section == migrated->sections.end() ? entry.serializer->readMissing()
                                                              : entry.serializer->read(section->second.state);
        if (!read) {
            return core::fail(std::format(messages::InSection, entry.name, read.error().message));
        }
    }
    return {};
}

std::int32_t SaveSections::version(std::string_view name) const {
    const Entry* entry = find(name);
    return entry == nullptr ? 0 : entry->migrations.current();
}

} // namespace dwe::game::save
