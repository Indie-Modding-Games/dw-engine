#include <dwe/assets/content_id.hpp>
#include <dwe/assets/map_patch.hpp>
#include <dwe/assets/messages.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <system_error>
#include <utility>

#include "map_document_reader.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view MapPrefix = "map/";
constexpr std::string_view DataLeaf = "data";

// The sections of a patch, named as the parts of the data they change.
constexpr std::string_view CameraKey = "camera";
constexpr std::string_view LightsKey = "lights";
constexpr std::string_view WarpsKey = "warps";
constexpr std::string_view NpcsKey = "npcs";
constexpr std::string_view ObjectsKey = "objects";
constexpr std::string_view SpritesKey = "sprites";
constexpr std::string_view InstancesKey = "instances";
constexpr std::string_view CollisionKey = "collision";
// A whole collision grid, and rectangles of cells.
constexpr std::string_view GridKey = "grid";
constexpr std::string_view CellsKey = "cells";
// What names the entry a change is for: its place in the data's list, or an exit's number.
constexpr std::string_view IndexKey = "index";
constexpr std::string_view ExitKey = "exit";
constexpr std::string_view ViewpointKey = "viewpoint";
constexpr std::string_view ReferenceKey = "reference";
constexpr std::string_view ProjectionKey = "projection";
constexpr std::string_view ColumnKey = "column";
constexpr std::string_view RowKey = "row";
constexpr std::string_view WidthKey = "width";
constexpr std::string_view HeightKey = "height";
constexpr std::string_view ValueKey = "value";

// A change to one entry of one of the data's lists: the entry's number and the patch's mapping,
// whose other keys replace the entry's.
struct EntryChange {
  std::int32_t number = 0;
  const YamlNode* fields = nullptr;
};

// A rectangle of collision cells set to one value.
struct CellChange {
  CollisionCell first;
  std::int32_t width = 1;
  std::int32_t height = 1;
  std::uint8_t value = 0;
};

// A patch as read, pointing into its tree.
struct MapPatch {
  const YamlNode* camera = nullptr;
  std::vector<EntryChange> lights;
  std::vector<EntryChange> warps;
  std::vector<EntryChange> npcs;
  const YamlNode* objects = nullptr;
  std::vector<EntryChange> sprites;
  std::vector<EntryChange> instances;
  const YamlNode* grid = nullptr;
  std::vector<CellChange> cells;
};

std::vector<EntryChange> readChanges(YamlReader& reader, const YamlNode& parent,
                                     std::string_view section, std::string_view key) {
  std::vector<EntryChange> changes;
  const YamlNode* list = parent.isMapping() ? parent.find(section) : nullptr;
  if (list == nullptr) {
    return changes;
  }
  for (const YamlNode& item : reader.sequence(*list, section)) {
    const YamlNode* number = item.isMapping() ? item.find(key) : nullptr;
    if (number == nullptr) {
      reader.fail(item, messages::formatMessage(messages::MapPatchEntry, section, key));
      break;
    }
    const auto value = reader.integer<std::int32_t>(*number);
    if (!reader.ok()) {
      break;
    }
    if (value < 0) {
      reader.fail(*number, messages::formatMessage(messages::MapPatchNegative, key, value));
      break;
    }
    if (std::ranges::contains(changes, value, &EntryChange::number)) {
      reader.fail(item, messages::formatMessage(messages::MapPatchTwice, key, value, section));
      break;
    }
    changes.push_back({.number = value, .fields = &item});
  }
  return changes;
}

std::vector<CellChange> readCells(YamlReader& reader, const YamlNode& node) {
  std::vector<CellChange> cells;
  for (const YamlNode& item : reader.sequence(node, CellsKey)) {
    if (!reader.expectKeys(item, "a rectangle of cells", {ColumnKey, RowKey, ValueKey},
                           {WidthKey, HeightKey})) {
      break;
    }
    const CellChange cell{
        .first = {.column = reader.integer<std::int32_t>(reader.field(item, ColumnKey)),
                  .row = reader.integer<std::int32_t>(reader.field(item, RowKey))},
        .width = reader.optionalInteger<std::int32_t>(item, WidthKey).value_or(1),
        .height = reader.optionalInteger<std::int32_t>(item, HeightKey).value_or(1),
        .value = reader.integer<std::uint8_t>(reader.field(item, ValueKey))};
    if (!reader.ok()) {
      break;
    }
    if (cell.width < 1 || cell.height < 1) {
      reader.fail(item,
                  messages::formatMessage(messages::MapPatchCellSize, cell.width, cell.height));
      break;
    }
    const CollisionCell last{.column = cell.first.column + cell.width - 1,
                             .row = cell.first.row + cell.height - 1};
    if (!MapCollision::contains(cell.first) || !MapCollision::contains(last)) {
      reader.fail(item, messages::formatMessage(messages::MapPatchCellsOutside, cell.first.column,
                                                cell.first.row, cell.width, cell.height,
                                                MapCollision::Size, MapCollision::Size));
      break;
    }
    cells.push_back(cell);
  }
  return cells;
}

MapPatch readPatch(YamlReader& reader, const YamlNode& root) {
  MapPatch patch;
  if (!reader.expectKeys(root, "a map patch", {},
                         {CameraKey, LightsKey, WarpsKey, NpcsKey, ObjectsKey, CollisionKey})) {
    return patch;
  }
  if (const YamlNode* camera = root.find(CameraKey);
      camera != nullptr && reader.expectKeys(*camera, "the camera of a patch", {},
                                             {ViewpointKey, ReferenceKey, ProjectionKey})) {
    patch.camera = camera;
  }
  patch.lights = readChanges(reader, root, LightsKey, IndexKey);
  patch.warps = readChanges(reader, root, WarpsKey, ExitKey);
  patch.npcs = readChanges(reader, root, NpcsKey, IndexKey);
  if (const YamlNode* objects = root.find(ObjectsKey);
      objects != nullptr &&
      reader.expectKeys(*objects, "the objects of a patch", {}, {SpritesKey, InstancesKey})) {
    patch.objects = objects;
    patch.sprites = readChanges(reader, *objects, SpritesKey, IndexKey);
    patch.instances = readChanges(reader, *objects, InstancesKey, IndexKey);
  }
  if (const YamlNode* collision = root.find(CollisionKey);
      collision != nullptr &&
      reader.expectKeys(*collision, "the collision of a patch", {}, {GridKey, CellsKey})) {
    if (const YamlNode* grid = collision->find(GridKey)) {
      readMapCollision(reader, *grid);
      patch.grid = grid;
    }
    if (const YamlNode* cells = collision->find(CellsKey)) {
      patch.cells = readCells(reader, *cells);
    }
  }
  return patch;
}

// The place of an exit in the data's warps, by the number at its `exit`.
std::optional<std::size_t> exitPlace(const YamlNode& warps, std::int32_t exit) {
  const std::span<const YamlNode> items = warps.items();
  for (std::size_t i = 0; i < items.size(); ++i) {
    const YamlNode* number = items[i].isMapping() ? items[i].find(ExitKey) : nullptr;
    if (number == nullptr || !number->isScalar()) {
      continue;
    }
    const std::string& text = number->text();
    std::int32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc{} && end == text.data() + text.size() && value == exit) {
      return i;
    }
  }
  return std::nullopt;
}

// The place an index names in a list of that size, or nothing past its end.
std::optional<std::size_t> listPlace(std::size_t size, std::int32_t index) {
  if (index < 0 || std::cmp_greater_equal(index, size)) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(index);
}

// The mapping with the fields of a change put over its own, all but the key that names it.
YamlNode withFields(YamlNode entry, const YamlNode& fields, std::string_view key) {
  for (const core::YamlEntry& field : fields.entries()) {
    if (field.key != key) {
      entry.set(field.key, field.value);
    }
  }
  return entry;
}

// Applies the changes of one section to the list of that name in `parent`.
void applyChanges(YamlReader& reader, YamlNode& parent, std::string_view section,
                  std::span<const EntryChange> changes, std::string_view key) {
  const YamlNode* found = parent.find(section);
  if (changes.empty() || found == nullptr || !found->isSequence()) {
    return;
  }
  std::vector<YamlNode> items{found->items().begin(), found->items().end()};
  for (const EntryChange& change : changes) {
    const std::optional<std::size_t> place =
        key == ExitKey ? exitPlace(*found, change.number) : listPlace(items.size(), change.number);
    if (!place) {
      reader.fail(*change.fields,
                  messages::formatMessage(messages::MapPatchNoEntry, section, key, change.number));
      return;
    }
    items[*place] = withFields(std::move(items[*place]), *change.fields, key);
  }
  YamlNode list = YamlNode::sequence();
  for (YamlNode& item : items) {
    list.append(std::move(item));
  }
  parent.set(std::string{section}, std::move(list));
}

// Puts a patch over the tree of a map's data, which the data's reader reads afterwards.
void applyPatch(YamlReader& reader, const MapPatch& patch, YamlNode& data) {
  if (patch.camera != nullptr) {
    if (const YamlNode* camera = data.find(CameraKey)) {
      data.set(std::string{CameraKey}, withFields(*camera, *patch.camera, {}));
    }
  }
  applyChanges(reader, data, LightsKey, patch.lights, IndexKey);
  applyChanges(reader, data, WarpsKey, patch.warps, ExitKey);
  applyChanges(reader, data, NpcsKey, patch.npcs, IndexKey);
  if (patch.objects != nullptr) {
    const YamlNode* objects = data.find(ObjectsKey);
    if (objects == nullptr || !objects->isMapping()) {
      reader.fail(*patch.objects, messages::MapPatchNoObjects);
      return;
    }
    YamlNode changed = *objects;
    applyChanges(reader, changed, SpritesKey, patch.sprites, IndexKey);
    applyChanges(reader, changed, InstancesKey, patch.instances, IndexKey);
    data.set(std::string{ObjectsKey}, std::move(changed));
  }
  if (patch.grid != nullptr) {
    data.set(std::string{CollisionKey}, *patch.grid);
  }
}

void setCells(MapCollision& collision, const CellChange& change) {
  for (std::int32_t row = 0; row < change.height; ++row) {
    for (std::int32_t column = 0; column < change.width; ++column) {
      collision.setValue({.column = change.first.column + column, .row = change.first.row + row},
                         change.value);
    }
  }
}

} // namespace

core::Result<AssetId> mapPatchId(std::string_view mapId) {
  return AssetId::parse(std::format("{}{}/{}", MapPrefix, contentAssetPath(mapId), MapPatchLeaf));
}

std::optional<std::string> mapIdOfAsset(const AssetId& id) {
  std::string_view text = id.str();
  if (!text.starts_with(MapPrefix)) {
    return std::nullopt;
  }
  text.remove_prefix(MapPrefix.size());
  const std::size_t leaf = text.rfind('/');
  if (leaf == std::string_view::npos ||
      (text.substr(leaf + 1) != DataLeaf && text.substr(leaf + 1) != MapPatchLeaf)) {
    return std::nullopt;
  }
  std::string map{text.substr(0, leaf)};
  // A pack's map is a folder per part (ContentId::assetPath): map/<pack>/<name>/data.
  if (const std::size_t slash = map.find('/'); slash != std::string::npos) {
    if (map.find('/', slash + 1) != std::string::npos) {
      return std::nullopt;
    }
    map[slash] = ':';
  }
  return map;
}

bool isMapPatchId(const AssetId& id) {
  return id.str().ends_with(MapPatchLeaf) && mapIdOfAsset(id).has_value();
}

core::Result<void> checkMapPatch(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return core::fail(std::format("{}: {}", messages::MapPatchContext, tree.error().message));
  }
  YamlReader reader;
  readPatch(reader, *tree);
  if (!reader.ok()) {
    return std::unexpected{reader.error(messages::MapPatchContext)};
  }
  return {};
}

core::Result<MapDocument> patchMapDocument(std::string_view data,
                                           std::span<const MapPatchText> patches) {
  auto tree = core::parseYaml(data);
  if (!tree) {
    return core::fail(std::format("map data: {}", tree.error().message));
  }
  // The data on its own first, so that a problem of the data is never blamed on a patch.
  auto document = readMapDocument(*tree);
  if (!document) {
    return document;
  }
  std::vector<CellChange> cells;
  std::vector<MapExitPatch> patchedExits;
  for (const MapPatchText& text : patches) {
    const auto failed = [&](const core::Error& error) {
      return core::fail(
          messages::formatMessage(messages::MapPatchFailed, text.file, error.message));
    };
    auto patchTree = core::parseYaml(text.text);
    if (!patchTree) {
      return failed(patchTree.error());
    }
    YamlReader reader;
    const MapPatch patch = readPatch(reader, *patchTree);
    if (reader.ok()) {
      applyPatch(reader, patch, *tree);
    }
    if (!reader.ok()) {
      return failed(reader.error(messages::MapPatchContext));
    }
    // Read after each patch: the values it gave are checked, and a problem is its own.
    document = readMapDocument(*tree);
    if (!document) {
      return failed(document.error());
    }
    if (patch.grid != nullptr) {
      cells.clear();
    }
    cells.insert(cells.end(), patch.cells.begin(), patch.cells.end());
    for (const EntryChange& change : patch.warps) {
      std::erase_if(patchedExits,
                    [&](const MapExitPatch& known) { return known.exit == change.number; });
      patchedExits.push_back(
          {.exit = change.number, .pack = text.pack, .baseRights = text.baseRights});
    }
  }
  for (const CellChange& change : cells) {
    setCells(document->collision, change);
  }
  std::ranges::sort(patchedExits, {}, &MapExitPatch::exit);
  document->patchedExits = std::move(patchedExits);
  for (const MapPatchText& text : patches) {
    document->patches.push_back(text.pack);
  }
  return document;
}

std::vector<AssetSource> mapPatchSources(const VirtualFileSystem& vfs, std::string_view mapId) {
  const auto dataId = mapDataId(mapId);
  const auto patchId = mapPatchId(mapId);
  const AssetSource* data = dataId ? vfs.resolve(*dataId) : nullptr;
  if (data == nullptr || !patchId) {
    return {};
  }
  const std::span<const MountedPack> packs = vfs.packs();
  const auto placeOf = [&](const AssetSource& source) {
    return std::ranges::find(packs, source.packId,
                             [](const MountedPack& pack) { return pack.manifest.id; }) -
           packs.begin();
  };
  const auto floor = placeOf(*data);
  std::vector<AssetSource> found;
  for (const AssetSource& source : vfs.sources(*patchId)) {
    if (placeOf(source) >= floor) {
      found.push_back(source);
    }
  }
  return found;
}

core::Result<std::vector<MapPatchText>> readMapPatches(const VirtualFileSystem& vfs,
                                                       std::string_view mapId) {
  std::vector<MapPatchText> patches;
  for (const AssetSource& source : mapPatchSources(vfs, mapId)) {
    auto bytes = vfs.read(source);
    if (!bytes) {
      return std::unexpected{bytes.error()};
    }
    const MountedPack& pack = vfs.packOf(source);
    patches.push_back(
        {.pack = pack.manifest.id,
         .baseRights = hasBaseRights(pack.origin, pack.manifest.layer),
         .file = pathText(source.file),
         .text = std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()}});
  }
  return patches;
}

} // namespace dwe::assets
