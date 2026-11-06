#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/effect/efe_yaml.hpp>

#include <yaml/yaml_values.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace dwe::formats::effect {

namespace {

using core::YamlNode;
using yaml::null;
using yaml::number;
using yaml::quoted;

constexpr std::size_t TileMapColumns = 11;
constexpr std::size_t TileMapRows = 9;
constexpr std::size_t RibbonPointCount = 10;
constexpr std::uint32_t ListEntrySize = 10;
constexpr std::uint8_t FillByte = 0xCD;

std::string hex(std::int64_t offset) {
  return offset < 0 ? std::format("-{:#06x}", -offset) : std::format("{:#06x}", offset);
}

std::string_view kindName(EfeArgKind kind) {
  using enum EfeArgKind;
  switch (kind) {
  case Value:
  case Unused:
  case Code:
    return "address";
  case Int32:
    return "int32";
  case Vector:
    return "vector";
  case Color:
    return "color";
  case BoneOffsets:
    return "bone_offsets";
  case UvKeyframes:
    return "uv_keyframes";
  case ParentField:
    return "parent_field";
  case Int32Array:
    return "int32_array";
  case ClutColors:
    return "clut_colors";
  case ClutRow:
    return "clut_row";
  case TileMap:
    return "tile_map";
  case TileOffsets:
    return "tile_offsets";
  case ParallaxSprites:
    return "parallax_sprites";
  case RibbonPoints:
    return "ribbon_points";
  case QuadCorner:
    return "quad_corner";
  }
  return "address";
}

class Writer {
public:
  Writer(std::span<const std::byte> payload, const EfeHeader& header, const EfeScripts& scripts,
         const EfeAnalysis& analysis, const EfeYamlIds& ids)
      : reader_(payload), header_(&header), scripts_(&scripts), analysis_(&analysis), ids_(&ids) {}

  YamlNode write() {
    YamlNode root = YamlNode::mapping();
    YamlNode start = YamlNode::sequence();
    YamlNode init = YamlNode::sequence();
    for (std::size_t i = 0; i < scripts_->instructions.size(); ++i) {
      const EfeInstruction& in = scripts_->instructions[i];
      (std::cmp_less(in.offset, header_->initScript) ? start : init).append(instruction(i));
    }
    root.set("start_script", std::move(start));
    root.set("init_script", std::move(init));
    if (!scripts_->padding.empty()) {
      YamlNode padding = YamlNode::sequence();
      for (const std::uint32_t at : scripts_->padding) {
        padding.append(quoted(hex(at)));
      }
      root.set("padding", std::move(padding));
    }
    YamlNode data = YamlNode::sequence();
    for (const EfeDataItem& item : analysis_->data) {
      data.append(dataItem(item));
    }
    root.set("data", std::move(data));
    return root;
  }

private:
  [[nodiscard]] bool isLabel(std::uint32_t offset) const {
    return std::cmp_equal(offset, header_->startScript) ||
           std::cmp_equal(offset, header_->initScript) ||
           std::ranges::binary_search(analysis_->labels, offset);
  }

  [[nodiscard]] std::string labelName(std::int32_t offset) const {
    if (offset == header_->startScript) {
      return "start";
    }
    if (offset == header_->initScript) {
      return "init";
    }
    return std::format("l_{:04x}", offset);
  }

  static std::string variableText(const EfeVariable& variable) {
    const std::string_view space = variable.space == EfeSpace::Base ? "base" : "instance";
    return variable.offset < 0 ? std::format("{}-{:#06x}", space, -variable.offset)
                               : std::format("{}+{:#06x}", space, variable.offset);
  }

  static void setVariable(YamlNode& node, std::string_view key, const EfeVariable& variable) {
    node.set(std::string{key}, quoted(variableText(variable)));
    if (variable.flag != 0 && variable.flag != EfeBaseFlag) {
      node.set(std::format("{}_flag", key), quoted(std::format("{:#06x}", variable.flag)));
    }
  }

  // What a sub-op argument or a pop received: the immediate, the variable or the address that was
  // pushed.
  [[nodiscard]] YamlNode pushed(std::optional<std::size_t> push, EfeArgKind kind) const {
    if (!push) {
      return null();
    }
    const EfeInstruction& in = scripts_->instructions[*push];
    switch (in.opcode) {
    case EfeOpcode::PushImmediate:
      return number(in.value);
    case EfeOpcode::PushVariable:
      return quoted(variableText(in.variable));
    default:
      if (kind == EfeArgKind::Code && in.variable.space == EfeSpace::Base &&
          isLabel(static_cast<std::uint32_t>(in.variable.offset))) {
        return quoted(labelName(in.variable.offset));
      }
      return quoted("&" + variableText(in.variable));
    }
  }

  [[nodiscard]] YamlNode instruction(std::size_t index) const {
    const EfeInstruction& in = scripts_->instructions[index];
    YamlNode node = YamlNode::mapping();
    if (isLabel(in.offset)) {
      node.set("label", quoted(labelName(static_cast<std::int32_t>(in.offset))));
    }
    node.set("at", quoted(hex(in.offset)));
    node.set("op", YamlNode::scalar(std::string{efeOpcodeName(in.opcode)}));
    switch (in.opcode) {
    case EfeOpcode::LoadImmediate:
    case EfeOpcode::PushImmediate:
      node.set("value", number(in.value));
      break;
    case EfeOpcode::LoadVariable:
    case EfeOpcode::PushVariable:
      setVariable(node, "variable", in.variable);
      node.set("width", number(static_cast<int>(in.width)));
      break;
    case EfeOpcode::PushAddress:
      // BTL_pushEFEVariableAddress does not read the variant: the tool wrote the width of what the
      // address points at there (4, 2, 1), or left it fill.
      setVariable(node, "address", in.variable);
      if (in.variant() == 1 || in.variant() == 2 || in.variant() == 4) {
        node.set("declared_width", number(in.variant()));
      } else if (in.variant() != FillByte) {
        node.set("variant", quoted(std::format("{:#04x}", in.variant())));
      }
      break;
    case EfeOpcode::PopToVariable:
      setVariable(node, "variable", in.variable);
      node.set("value", pushed(analysis_->arguments[index].front(), EfeArgKind::Value));
      break;
    case EfeOpcode::LoadIndexed:
      node.set("table", quoted(std::format("base+{:#06x}", in.target)));
      node.set("width", number(static_cast<int>(in.width)));
      setVariable(node, "index", in.variable);
      node.set("index_width", number(static_cast<int>(in.indexWidth)));
      break;
    case EfeOpcode::ApplyOperator:
      setVariable(node, "variable", in.variable);
      node.set("width", number(static_cast<int>(in.width)));
      node.set("operator", YamlNode::scalar(std::string{efeOperatorName(in.op)}));
      break;
    case EfeOpcode::BranchIf:
      setVariable(node, "variable", in.variable);
      node.set("width", number(static_cast<int>(in.width)));
      node.set("comparison", YamlNode::scalar(std::string{efeComparisonName(in.comparison)}));
      node.set("target", quoted(labelName(in.target)));
      break;
    case EfeOpcode::Jump:
    case EfeOpcode::Call:
      node.set("target", quoted(labelName(in.target)));
      break;
    case EfeOpcode::SubOp: {
      const EfeSubOpSpec& spec = efeSubOp(in.subOp);
      node.set("sub", YamlNode::scalar(std::string{spec.name}));
      if (!spec.args.empty()) {
        YamlNode args = YamlNode::mapping();
        for (std::size_t k = 0; k < spec.args.size(); ++k) {
          args.set(std::string{spec.args[k].name},
                   pushed(analysis_->arguments[index][k], spec.args[k].kind));
        }
        node.set("args", std::move(args));
      }
      break;
    }
    case EfeOpcode::Spawn:
      node.set("count", number(in.count));
      node.set("table", quoted(std::format("base+{:#06x}", in.target)));
      node.set("stride", number(in.stride));
      node.set("ordinal", number(in.ordinal));
      break;
    case EfeOpcode::LoadRandom:
    case EfeOpcode::Stop:
    case EfeOpcode::Stop6:
    case EfeOpcode::Stop8:
    case EfeOpcode::Return:
      break;
    }
    for (const EfeIgnoredOperand& ignored : efeIgnoredOperands(in.opcode)) {
      if (!in.isUsualIgnored(ignored)) {
        node.set(std::string{ignored.name},
                 quoted(std::format("{:#06x}", in.ignoredValue(ignored.index))));
      }
    }
    return node;
  }

  // The analysis keeps every item inside the payload, so the reads below cannot miss.
  [[nodiscard]] std::uint8_t byteAt(std::uint32_t at) const {
    return reader_.u8(at).value_or(FillByte);
  }
  [[nodiscard]] std::uint16_t halfAt(std::uint32_t at) const { return reader_.u16(at).value_or(0); }
  [[nodiscard]] std::int32_t wordAt(std::uint32_t at) const { return reader_.s32(at).value_or(0); }

  [[nodiscard]] bool isFill(std::uint32_t at, std::uint32_t size) const {
    for (std::uint32_t i = 0; i < size; ++i) {
      if (byteAt(at + i) != FillByte) {
        return false;
      }
    }
    return true;
  }

  // A signed value of 1, 2 or 4 bytes, or null when its bytes are fill: a variable that starts
  // without a value.
  [[nodiscard]] YamlNode valueAt(std::uint32_t at, std::uint32_t size) const {
    if (isFill(at, size)) {
      return null();
    }
    switch (size) {
    case 1:
      return number(static_cast<std::int8_t>(byteAt(at)));
    case 2:
      return number(static_cast<std::int16_t>(halfAt(at)));
    default:
      return number(wordAt(at));
    }
  }

  [[nodiscard]] YamlNode values(std::uint32_t at, std::size_t count, std::uint32_t size) const {
    YamlNode list = YamlNode::sequence();
    for (std::size_t i = 0; i < count; ++i) {
      list.append(valueAt(at + (static_cast<std::uint32_t>(i) * size), size));
    }
    return list;
  }

  [[nodiscard]] YamlNode digimonOf(std::int16_t type) const {
    if (type >= 0 && std::cmp_less(type, ids_->digimon.size())) {
      return quoted(ids_->digimon[static_cast<std::size_t>(type)]);
    }
    return number(type);
  }

  [[nodiscard]] YamlNode boneOffsets(const EfeDataItem& item) const {
    YamlNode list = YamlNode::sequence();
    for (std::uint32_t at = item.offset; at + ListEntrySize <= item.offset + item.size;
         at += ListEntrySize) {
      YamlNode entry = YamlNode::mapping();
      entry.set("digimon", digimonOf(static_cast<std::int16_t>(halfAt(at))));
      entry.set("bone", valueAt(at + 2, 2));
      entry.set("offset", values(at + 4, 3, 2));
      list.append(std::move(entry));
    }
    return list;
  }

  [[nodiscard]] YamlNode parallaxSprites(const EfeDataItem& item) const {
    YamlNode list = YamlNode::sequence();
    for (std::uint32_t at = item.offset; at + ListEntrySize <= item.offset + item.size;
         at += ListEntrySize) {
      YamlNode entry = YamlNode::mapping();
      entry.set("size", valueAt(at, 2));
      entry.set("x", valueAt(at + 2, 2));
      entry.set("y", valueAt(at + 4, 2));
      entry.set("depth", valueAt(at + 6, 2));
      entry.set("u", valueAt(at + 8, 2));
      list.append(std::move(entry));
    }
    return list;
  }

  [[nodiscard]] YamlNode keyframes(const EfeDataItem& item) const {
    YamlNode list = YamlNode::sequence();
    for (std::uint32_t at = item.offset + 4; at + 4 <= item.offset + item.size; at += 4) {
      YamlNode entry = YamlNode::mapping();
      entry.set("frames", valueAt(at, 2));
      entry.set("du", valueAt(at + 2, 1));
      entry.set("dv", valueAt(at + 3, 1));
      list.append(std::move(entry));
    }
    return list;
  }

  [[nodiscard]] YamlNode rows(std::uint32_t at, std::size_t rowCount, std::size_t rowLength,
                              std::uint32_t size) const {
    YamlNode list = YamlNode::sequence();
    for (std::size_t row = 0; row < rowCount; ++row) {
      list.append(
          values(at + (static_cast<std::uint32_t>(row * rowLength) * size), rowLength, size));
    }
    return list;
  }

  [[nodiscard]] YamlNode bytesText(std::uint32_t at, std::uint32_t size) const {
    std::string text;
    for (std::uint32_t i = 0; i < size; ++i) {
      text += std::format("{:02x}", byteAt(at + i));
    }
    return quoted(text);
  }

  void setArgumentValues(YamlNode& node, const EfeDataItem& item) const {
    using enum EfeArgKind;
    const std::uint32_t at = item.offset;
    switch (item.argKind) {
    case Int32:
      node.set("value", valueAt(at, 4));
      break;
    case Vector:
    case Color:
    case Int32Array:
      node.set("values", values(at, item.size / 4, 4));
      break;
    case QuadCorner:
      if (item.size > 0) {
        node.set("values", values(at, item.size / 4, 4));
      }
      break;
    case BoneOffsets:
      node.set("entries", boneOffsets(item));
      break;
    case ParallaxSprites:
      node.set("entries", parallaxSprites(item));
      break;
    case UvKeyframes:
      node.set("keyframes", keyframes(item));
      break;
    case ParentField:
      if (item.size == 1 || item.size == 2) {
        node.set("value", valueAt(at, item.size));
      } else if (item.size > 0) {
        node.set("values", values(at, item.size / 4, 4));
      }
      break;
    case ClutColors:
      node.set("colors", rows(at, item.size / 8, 4, 2));
      break;
    case ClutRow: {
      if (item.size == 0) {
        break; // the colour index is not a number the scripts push or bound, so the row is not
               // known
      }
      if (item.count > 0) {
        // The index is computed: the row starts at any of these colours, so all the rows span is
        // listed.
        node.set("row_starts", number(item.count));
      }
      YamlNode colors = YamlNode::sequence();
      for (std::size_t i = 0; i < item.size / 2; ++i) {
        colors.append(
            quoted(std::format("{:#06x}", halfAt(at + (static_cast<std::uint32_t>(i) * 2)))));
      }
      node.set("colors", std::move(colors));
      break;
    }
    case TileMap: {
      YamlNode list = YamlNode::sequence();
      for (std::size_t row = 0; row < TileMapRows; ++row) {
        YamlNode tiles = YamlNode::sequence();
        for (std::size_t column = 0; column < TileMapColumns; ++column) {
          tiles.append(
              number(byteAt(at + static_cast<std::uint32_t>((row * TileMapColumns) + column))));
        }
        list.append(std::move(tiles));
      }
      node.set("tiles", std::move(list));
      break;
    }
    case TileOffsets:
      node.set("values", values(at, item.size / 2, 2));
      break;
    case RibbonPoints:
      node.set("points", rows(at, RibbonPointCount, 4, 2));
      break;
    case Value:
    case Unused:
    case Code:
      break;
    }
  }

  [[nodiscard]] YamlNode dataItem(const EfeDataItem& item) const {
    YamlNode node = YamlNode::mapping();
    node.set("at", quoted(hex(item.offset)));
    switch (item.kind) {
    case EfeDataKind::Instances: {
      node.set("kind", YamlNode::scalar("instances"));
      node.set("count", number(item.count));
      node.set("stride", number(item.stride));
      bool free = true;
      for (std::int16_t i = 0; i < item.count; ++i) {
        const std::uint32_t at = item.offset + static_cast<std::uint32_t>(i * item.stride);
        free =
            free && wordAt(at) == -1 && isFill(at + 4, static_cast<std::uint32_t>(item.stride) - 4);
      }
      node.set("free", yaml::boolean(free));
      if (!free) {
        node.set("bytes", bytesText(item.offset, item.size));
      }
      break;
    }
    case EfeDataKind::Variable:
      node.set("kind", YamlNode::scalar("variable"));
      node.set("width", number(static_cast<int>(item.width)));
      node.set("value", valueAt(item.offset, item.size));
      break;
    case EfeDataKind::Table: {
      const auto width = static_cast<std::uint32_t>(item.width);
      node.set("kind", YamlNode::scalar("table"));
      node.set("width", number(width));
      node.set("size", number(item.size));
      node.set("values", values(item.offset, item.size / width, width));
      if (item.size % width != 0) {
        node.set("tail", bytesText(item.offset + (item.size / width * width), item.size % width));
      }
      break;
    }
    case EfeDataKind::Argument:
      node.set("kind", YamlNode::scalar(std::string{kindName(item.argKind)}));
      node.set("size", number(item.size));
      setArgumentValues(node, item);
      break;
    case EfeDataKind::Fill:
      node.set("kind", YamlNode::scalar("fill"));
      node.set("size", number(item.size));
      break;
    case EfeDataKind::Unused:
      node.set("kind", YamlNode::scalar("unused"));
      node.set("size", number(item.size));
      node.set("bytes", bytesText(item.offset, item.size));
      node.set("reason", quoted(std::string{EfeUnusedReason}));
      break;
    case EfeDataKind::Unreferenced:
      node.set("kind", YamlNode::scalar("unreferenced"));
      node.set("size", number(item.size));
      node.set("bytes", bytesText(item.offset, item.size));
      break;
    }
    return node;
  }

  ByteReader reader_;
  const EfeHeader* header_;
  const EfeScripts* scripts_;
  const EfeAnalysis* analysis_;
  const EfeYamlIds* ids_;
};

} // namespace

YamlNode efeHeaderToYaml(const EfeHeader& header) {
  YamlNode node = YamlNode::mapping();
  const std::array<std::pair<std::string_view, std::int32_t>, 10> offsets = {{
      {"start_script", header.startScript},
      {"init_script", header.initScript},
      {"uv_anims", header.uvAnims},
      {"uv_anims_end", header.uvAnimsEnd},
      {"sub_effects", header.subEffects},
      {"tmd_start", header.tmdStart},
      {"tmd_end", header.tmdEnd},
      {"tim_start", header.timStart},
      {"tim_end", header.timEnd},
      {"data_end", header.dataEnd},
  }};
  for (const auto& [name, offset] : offsets) {
    node.set(std::string{name}, quoted(hex(offset)));
  }
  node.set("spawn_count", number(header.spawnCount));
  node.set("sub_effect_count", number(header.numSubEffects));
  node.set("effect_id", quoted(std::format("{:#06x}", header.effectId)));
  return node;
}

YamlNode efeScriptToYaml(std::span<const std::byte> payload, const EfeHeader& header,
                         const EfeScripts& scripts, const EfeAnalysis& analysis,
                         const EfeYamlIds& ids) {
  return Writer{payload, header, scripts, analysis, ids}.write();
}

} // namespace dwe::formats::effect
