#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/field/extra_toilets.hpp>

#include <cmath>
#include <format>
#include <utility>

namespace dwe::game::field {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view ExtraToiletsId = "field/extra_toilets";
constexpr std::string_view Context = "extra toilets";

ExtraToilet readToilet(YamlReader& reader, const YamlNode& node, const std::string& model) {
  ExtraToilet toilet;
  if (!reader.expectKeys(node, "an extra toilet", {"map", "position", "facing"}, {"model"})) {
    return toilet;
  }
  toilet.map = reader.text(reader.field(node, "map"));
  const auto position = reader.sequence(reader.field(node, "position"), "position", 2);
  if (position.size() == 2) {
    toilet.position = {reader.integer<std::int32_t>(position[0]),
                       reader.integer<std::int32_t>(position[1])};
  }
  const YamlNode& facing = reader.field(node, "facing");
  toilet.facing = reader.integer<std::int32_t>(facing);
  if (toilet.facing < 0 || toilet.facing >= core::OriginalAnglePerTurn) {
    reader.fail(facing, "facing is an original rotation, 0 to 4095");
  }
  const YamlNode* own = node.find("model");
  toilet.model = own != nullptr ? reader.text(*own) : model;
  return toilet;
}

// The cells around the booth whose middles fall in a rectangle of its own axes: from `near` to
// `far` along its front (exclusive, inclusive) and up to `half` to either side.
std::vector<assets::CollisionCell> cellsIn(const ExtraToilet& toilet, float near, float far,
                                           float half) {
  const glm::vec3 middle = toilet.worldPosition();
  const glm::vec3 forward = assets::forwardOfYaw(toilet.yaw());
  const glm::vec3 right{forward.z, 0.0F, -forward.x};
  const assets::CollisionCell centre = assets::collisionCellAt(middle);
  constexpr std::int32_t Reach = 4;
  std::vector<assets::CollisionCell> cells;
  for (std::int32_t row = centre.row - Reach; row <= centre.row + Reach; ++row) {
    for (std::int32_t column = centre.column - Reach; column <= centre.column + Reach; ++column) {
      const assets::CollisionCell cell{.column = column, .row = row};
      glm::vec3 offset = assets::collisionCellCenter(cell) - middle;
      offset.y = 0.0F;
      const float along = glm::dot(offset, forward);
      if (along > near && along <= far && std::abs(glm::dot(offset, right)) <= half) {
        cells.push_back(cell);
      }
    }
  }
  return cells;
}

} // namespace

glm::vec3 ExtraToilet::worldPosition() const {
  return assets::toWorldPosition({position.x, 0, position.y});
}

float ExtraToilet::yaw() const {
  return assets::yawFromOriginal(facing);
}

assets::MapToilet ExtraToilet::points() const {
  // The world's forward of the rotation, back on the original's axes (its z the other way).
  const glm::vec3 forward = assets::forwardOfYaw(yaw());
  const glm::ivec2 front{static_cast<std::int32_t>(std::lround(forward.x * ExtraToiletFront)),
                         static_cast<std::int32_t>(std::lround(-forward.z * ExtraToiletFront))};
  return {.index = 0,
          .x1 = position.x + front.x,
          .z1 = position.y + front.y,
          .x2 = position.x,
          .z2 = position.y};
}

std::vector<assets::CollisionCell> ExtraToilet::footprint() const {
  return cellsIn(*this, -ExtraToiletHalfSize - 1.0F, ExtraToiletHalfSize, ExtraToiletHalfSize);
}

std::vector<assets::CollisionCell> ExtraToilet::frontCells() const {
  return cellsIn(*this, ExtraToiletHalfSize, ExtraToiletHalfSize + ExtraToiletStrip,
                 ExtraToiletHalfSize);
}

core::Result<std::vector<ExtraToilet>> parseExtraToilets(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
  }
  YamlReader reader;
  std::vector<ExtraToilet> toilets;
  if (reader.expectKeys(*tree, Context, {"model", "toilets"})) {
    const std::string model = reader.text(reader.field(*tree, "model"));
    for (const YamlNode& node : reader.sequence(reader.field(*tree, "toilets"), "toilets")) {
      toilets.push_back(readToilet(reader, node, model));
    }
  }
  return reader.finish(std::move(toilets), Context);
}

core::Result<std::vector<ExtraToilet>> loadExtraToilets(const assets::VirtualFileSystem& vfs) {
  auto id = assets::AssetId::parse(ExtraToiletsId);
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto bytes = vfs.read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return parseExtraToilets(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

} // namespace dwe::game::field
