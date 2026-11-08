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

ExtraToilet readToilet(YamlReader& reader, const YamlNode& node) {
  ExtraToilet toilet;
  if (!reader.expectKeys(node, "an extra toilet", {"map", "position", "facing"})) {
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
  return toilet;
}

} // namespace

assets::MapToilet ExtraToilet::points() const {
  // The world's forward of the rotation, back on the original's axes (its z the other way).
  const glm::vec3 forward = assets::forwardOfYaw(assets::yawFromOriginal(facing));
  const glm::ivec2 front{static_cast<std::int32_t>(std::lround(forward.x * ExtraToiletFront)),
                         static_cast<std::int32_t>(std::lround(-forward.z * ExtraToiletFront))};
  return {.index = 0,
          .x1 = position.x + front.x,
          .z1 = position.y + front.y,
          .x2 = position.x,
          .z2 = position.y};
}

std::array<assets::CollisionCell, 9> ExtraToilet::cells() const {
  const assets::CollisionCell middle =
      assets::collisionCellAt(assets::toWorldPosition({position.x, 0, position.y}));
  std::array<assets::CollisionCell, 9> around{};
  std::size_t next = 0;
  for (std::int32_t row = -1; row <= 1; ++row) {
    for (std::int32_t column = -1; column <= 1; ++column) {
      around.at(next++) = {.column = middle.column + column, .row = middle.row + row};
    }
  }
  return around;
}

core::Result<std::vector<ExtraToilet>> parseExtraToilets(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
  }
  YamlReader reader;
  std::vector<ExtraToilet> toilets;
  if (reader.expectKeys(*tree, Context, {"toilets"})) {
    for (const YamlNode& node : reader.sequence(reader.field(*tree, "toilets"), "toilets")) {
      toilets.push_back(readToilet(reader, node));
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
