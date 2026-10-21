#include "game_table_document.hpp"

#include <dwe/assets/messages.hpp>
#include <dwe/assets/virtual_file_system.hpp>

namespace dwe::assets {

core::Result<core::YamlNode> parseTableDocument(std::string_view yaml, std::string_view what) {
    auto tree = core::parseYaml(yaml);
    if (!tree) {
        return core::fail(messages::formatMessage(messages::TableParse, what, tree.error().message));
    }
    return tree;
}

core::Result<core::YamlNode> readTableDocument(const VirtualFileSystem& vfs, const AssetId& id, std::string_view what,
                                               const TableComposition& composition) {
    auto text = readComposedTable(vfs, id, composition);
    if (!text) {
        return std::unexpected{text.error()};
    }
    return parseTableDocument(*text, what);
}

} // namespace dwe::assets
