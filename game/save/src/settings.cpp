#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/save/settings.hpp>

#include <format>
#include <utility>

#include "keys.hpp"
#include "messages.hpp"

namespace dwe::game::save {

namespace {

using assets::YamlReader;
using core::YamlNode;

std::int32_t readSlotCount(YamlReader& reader, const YamlNode& node, std::int32_t least, std::string_view outOfRange) {
    const auto count = reader.integer<std::int32_t>(node);
    if (reader.ok() && (count < least || count > MaxSlots)) {
        reader.fail(node, outOfRange);
    }
    return count;
}

} // namespace

core::Result<SaveSettings> parseSaveSettings(std::string_view yaml) {
    auto tree = core::parseYaml(yaml);
    if (!tree) {
        return core::fail(std::format(messages::InSettings, tree.error().message));
    }
    YamlReader reader;
    if (!reader.expectKeys(*tree, messages::SettingsWhat, {settings_key::ManualSlots, settings_key::AutosaveSlots})) {
        return std::unexpected{reader.error(messages::SettingsContext)};
    }
    const SaveSettings settings{.manualSlots = readSlotCount(reader, reader.field(*tree, settings_key::ManualSlots), 1,
                                                             std::format(messages::ManualSlotsRange, MaxSlots)),
                                .autosaveSlots = readSlotCount(reader, reader.field(*tree, settings_key::AutosaveSlots),
                                                               0, std::format(messages::AutosaveSlotsRange, MaxSlots))};
    return reader.finish(settings, messages::SettingsContext);
}

core::Result<SaveSettings> loadSaveSettings(const assets::VirtualFileSystem& vfs) {
    auto id = assets::AssetId::parse(SettingsAssetId);
    if (!id) {
        return std::unexpected{id.error()};
    }
    auto bytes = vfs.read(*id);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    return parseSaveSettings(std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

} // namespace dwe::game::save
