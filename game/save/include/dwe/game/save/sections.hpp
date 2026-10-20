#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/save/migration.hpp>
#include <dwe/game/save/save_file.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::save {

// How a system (partner, inventory, battle...) puts its state in a save and takes it back. The system implements it
// in its own module, so save never knows the system's types (docs/architecture/dependency-rules.md, rule 4).
class SectionSerializer {
public:
    SectionSerializer() = default;
    virtual ~SectionSerializer() = default;
    SectionSerializer(const SectionSerializer&) = delete;
    SectionSerializer& operator=(const SectionSerializer&) = delete;
    SectionSerializer(SectionSerializer&&) = delete;
    SectionSerializer& operator=(SectionSerializer&&) = delete;

    // Writes the system's state for its section, at the section's current version.
    [[nodiscard]] virtual core::Result<core::YamlNode> write() const = 0;

    // Replaces the system's state with the saved one, already migrated to the current version. An error says what
    // is wrong with it.
    virtual core::Result<void> read(const core::YamlNode& state) = 0;

    // Called instead of read when the save has no section for the system, such as a save made before the system
    // existed: it can start from its new-game state. By default it is an error.
    virtual core::Result<void> readMissing();
};

// The sections of this game: each system registers its serializer under a name with its migrations.
class SaveSections {
public:
    // Registers a section. The name follows isValidName and is unique; the section's version is the chain's
    // current version. The serializer must outlive this object.
    core::Result<void> add(std::string name, SectionSerializer& serializer, MigrationChain migrations = {});

    // Writes every registered section into the file at its current version, replacing what the file had for it.
    // Sections no system registered stay untouched.
    [[nodiscard]] core::Result<void> write(SaveFile& file) const;

    // Brings every registered section of the file up to its current version; unknown sections are left as they are.
    // Fails, naming the section, if one is newer than this game or a migration fails.
    [[nodiscard]] core::Result<SaveFile> migrate(SaveFile file) const;

    // Migrates the file and has each registered system read its section (or readMissing). Every section is migrated
    // before any system reads, so a file that cannot be migrated changes nothing; an error from a system's read can
    // leave earlier systems loaded, and then the game should start again from a known state.
    [[nodiscard]] core::Result<void> read(const SaveFile& file) const;

    // The version a registered section is written at; 0 if no section has that name.
    [[nodiscard]] std::int32_t version(std::string_view name) const;

private:
    struct Entry {
        std::string name;
        SectionSerializer* serializer;
        MigrationChain migrations;
    };

    [[nodiscard]] const Entry* find(std::string_view name) const;

    std::vector<Entry> entries_;
};

} // namespace dwe::game::save
