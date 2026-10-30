#include <dwe/assets/effect_script.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view StartLabel = "start";
constexpr std::string_view InitLabel = "init";
constexpr std::string_view LabelPrefix = "l_";
constexpr std::string_view BasePrefix = "base";
constexpr std::string_view InstancePrefix = "instance";
constexpr std::string_view NullText = "null";
constexpr int HexBase = 16;
constexpr int DecimalBase = 10;
constexpr std::size_t HexDigitsPerByte = 2;
constexpr std::byte Fill{0xCD};

// The names script.yaml gives the opcodes, operators and comparisons, in their order.
constexpr auto OpcodeNames = std::to_array<std::string_view>(
    {"load_immediate", "load_variable", "load_random", "load_indexed", "apply_operator", "stop",
     "stop_6", "branch_if", "stop_8", "jump", "push_immediate", "push_variable", "push_address",
     "call", "sub", "return", "pop_to_variable", "spawn"});
constexpr auto OperatorNames = std::to_array<std::string_view>(
    {"set", "add", "subtract", "multiply", "divide", "modulo", "shift_left", "shift_right"});
constexpr auto ComparisonNames = std::to_array<std::string_view>(
    {"equal", "not_equal", "less", "less_or_equal", "greater", "greater_or_equal"});

template <typename T, std::size_t N>
std::optional<T> named(const std::array<std::string_view, N>& names, std::string_view name) {
  const auto* found = std::ranges::find(names, name);
  if (found == names.end()) {
    return std::nullopt;
  }
  return static_cast<T>(found - names.begin());
}
constexpr std::uint32_t ByteBits = 8;
constexpr std::uint32_t ByteMask = 0xFF;
// A free instance starts with a frame of -1 (battle_effect.c: EfeInstance.frame; spawn looks for
// one).
constexpr std::int64_t FreeInstanceFrame = -1;
constexpr std::size_t Int32Size = 4;
constexpr std::size_t Int16Size = 2;
// battle_effect.c: EfeTechBoneOffset is 10 bytes; a negative type ends a list.
constexpr std::size_t BoneOffsetEntrySize = 10;
constexpr std::int64_t BoneOffsetsEnd = -1;
// EfeUvKeyframe.
constexpr std::size_t UvKeyframeSize = 4;
constexpr std::size_t ParallaxSpriteSize = 10;
// The negative size that ends a list of parallax sprites.
constexpr std::int64_t ListEnd = -1;

template <typename T>
std::optional<T> parseNumber(std::string_view text, int base) {
  T value{};
  const auto [last, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (error != std::errc{} || last != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// "0x0724" or a plain number.
std::optional<std::int64_t> parseInteger(std::string_view text) {
  bool negative = false;
  if (text.starts_with('-')) {
    negative = true;
    text.remove_prefix(1);
  }
  std::optional<std::int64_t> value;
  if (text.starts_with("0x")) {
    value = parseNumber<std::int64_t>(text.substr(2), HexBase);
  } else {
    value = parseNumber<std::int64_t>(text, DecimalBase);
  }
  if (value && negative) {
    *value = -*value;
  }
  return value;
}

class Reader {
public:
  core::Result<EffectScript> read(const YamlNode& effect, const YamlNode& root) {
    if (const YamlNode* move = effect.find("move"); move != nullptr && move->isScalar()) {
      program_.move = move->text();
    }
    if (const YamlNode* header = effect.find("header"); header != nullptr) {
      if (auto count = integerOf(*header, "sub_effect_count")) {
        program_.subEffectCount = static_cast<std::uint16_t>(*count);
      }
    }
    if (const YamlNode* objects = effect.find("uv_anim_count"); objects != nullptr) {
      program_.modelObjects = static_cast<std::uint16_t>(parseInteger(objects->text()).value_or(0));
    }
    const YamlNode* start = root.find("start_script");
    const YamlNode* init = root.find("init_script");
    if (start == nullptr || !start->isSequence() || init == nullptr || !init->isSequence()) {
      return core::fail("an effect script needs start_script and init_script lists");
    }
    for (const YamlNode* list : {start, init}) {
      for (const YamlNode& node : list->items()) {
        if (auto labelled = collectLabel(node); !labelled) {
          return std::unexpected{labelled.error()};
        }
      }
    }
    const auto startAt = labels_.find(std::string{StartLabel});
    const auto initAt = labels_.find(std::string{InitLabel});
    if (startAt == labels_.end() || initAt == labels_.end()) {
      return core::fail("an effect script needs the labels start and init");
    }
    program_.startScript = startAt->second;
    program_.initScript = initAt->second;
    for (const YamlNode* list : {start, init}) {
      for (const YamlNode& node : list->items()) {
        auto in = instruction(node);
        if (!in) {
          return std::unexpected{in.error()};
        }
        program_.instructions.push_back(std::move(*in));
      }
    }
    std::ranges::sort(program_.instructions, {}, &EffectInstruction::offset);
    program_.data.assign(program_.startScript, Fill);
    if (const YamlNode* data = root.find("data"); data != nullptr && data->isSequence()) {
      for (const YamlNode& item : data->items()) {
        if (auto written = dataItem(item); !written) {
          return std::unexpected{written.error()};
        }
      }
    }
    return std::move(program_);
  }

private:
  static core::Result<std::string> textOf(const YamlNode& node, std::string_view key) {
    const YamlNode* value = node.find(key);
    if (value == nullptr || !value->isScalar()) {
      return core::fail(std::format("line {}: no {}", node.line(), key));
    }
    return value->text();
  }

  static core::Result<std::int64_t> integerOf(const YamlNode& node, std::string_view key) {
    auto text = textOf(node, key);
    if (!text) {
      return std::unexpected{text.error()};
    }
    const std::optional<std::int64_t> value = parseInteger(*text);
    if (!value) {
      return core::fail(std::format("line {}: {} is not a number: {}", node.line(), key, *text));
    }
    return *value;
  }

  core::Result<void> collectLabel(const YamlNode& node) {
    const YamlNode* label = node.find("label");
    if (label == nullptr) {
      return {};
    }
    auto at = integerOf(node, "at");
    if (!at) {
      return std::unexpected{at.error()};
    }
    labels_[label->text()] = static_cast<std::uint32_t>(*at);
    return {};
  }

  [[nodiscard]] core::Result<std::int32_t> target(const YamlNode& node,
                                                  std::string_view key) const {
    auto text = textOf(node, key);
    if (!text) {
      return std::unexpected{text.error()};
    }
    if (const auto found = labels_.find(*text); found != labels_.end()) {
      return static_cast<std::int32_t>(found->second);
    }
    if (text->starts_with(LabelPrefix)) {
      if (const auto value = parseNumber<std::int32_t>(
              std::string_view{*text}.substr(LabelPrefix.size()), HexBase)) {
        return *value;
      }
    }
    if (text->starts_with(BasePrefix)) {
      if (const auto value = parseInteger(std::string_view{*text}.substr(BasePrefix.size() + 1))) {
        return static_cast<std::int32_t>(*value);
      }
    }
    return core::fail(std::format("line {}: {} is not a label: {}", node.line(), key, *text));
  }

  static core::Result<EffectVariable> variable(const YamlNode& node, std::string_view key) {
    auto text = textOf(node, key);
    if (!text) {
      return std::unexpected{text.error()};
    }
    std::string_view rest = *text;
    EffectVariable result;
    if (rest.starts_with(BasePrefix)) {
      result.base = true;
      rest.remove_prefix(BasePrefix.size());
    } else if (rest.starts_with(InstancePrefix)) {
      rest.remove_prefix(InstancePrefix.size());
    } else {
      return core::fail(std::format("line {}: {} is not a variable: {}", node.line(), key, *text));
    }
    const bool negative = rest.starts_with('-');
    if (rest.empty() || (rest.front() != '+' && !negative)) {
      return core::fail(std::format("line {}: {} is not a variable: {}", node.line(), key, *text));
    }
    const std::optional<std::int64_t> offset = parseInteger(rest.substr(1));
    if (!offset) {
      return core::fail(std::format("line {}: {} is not a variable: {}", node.line(), key, *text));
    }
    result.offset = static_cast<std::int16_t>(negative ? -*offset : *offset);
    return result;
  }

  static core::Result<EffectWidth> widthOf(const YamlNode& node, std::string_view key) {
    auto width = integerOf(node, key);
    if (!width) {
      return std::unexpected{width.error()};
    }
    switch (*width) {
    case static_cast<std::int64_t>(EffectWidth::Int8):
      return EffectWidth::Int8;
    case static_cast<std::int64_t>(EffectWidth::Int16):
      return EffectWidth::Int16;
    case static_cast<std::int64_t>(EffectWidth::Int32):
      return EffectWidth::Int32;
    default:
      return core::fail(std::format("line {}: {} is not 1, 2 or 4", node.line(), key));
    }
  }

  [[nodiscard]] core::Result<EffectInstruction> instruction(const YamlNode& node) const {
    EffectInstruction in;
    auto at = integerOf(node, "at");
    auto name = textOf(node, "op");
    if (!at || !name) {
      return std::unexpected{!at ? at.error() : name.error()};
    }
    in.offset = static_cast<std::uint32_t>(*at);
    const std::optional<EffectOpcode> opcode = named<EffectOpcode>(OpcodeNames, *name);
    if (!opcode) {
      return core::fail(std::format("line {}: {} is not an op", node.line(), *name));
    }
    in.opcode = *opcode;
    auto filled = fill(node, in);
    if (!filled) {
      return std::unexpected{filled.error()};
    }
    return in;
  }

  // The operands each op's YAML gives (dwx: efeScriptToYaml).
  core::Result<void> fill(const YamlNode& node, EffectInstruction& in) const {
    switch (in.opcode) {
    case EffectOpcode::LoadImmediate:
    case EffectOpcode::PushImmediate: {
      auto value = integerOf(node, "value");
      if (!value) {
        return std::unexpected{value.error()};
      }
      in.value = static_cast<std::int16_t>(*value);
      return {};
    }
    case EffectOpcode::LoadVariable:
    case EffectOpcode::PushVariable:
    case EffectOpcode::ApplyOperator:
    case EffectOpcode::BranchIf:
      return variableOperands(node, in);
    case EffectOpcode::PushAddress:
    case EffectOpcode::PopToVariable: {
      auto var = variable(node, in.opcode == EffectOpcode::PushAddress ? "address" : "variable");
      if (!var) {
        return std::unexpected{var.error()};
      }
      in.variable = *var;
      return {};
    }
    case EffectOpcode::LoadIndexed: {
      auto table = target(node, "table");
      auto width = widthOf(node, "width");
      auto index = variable(node, "index");
      auto indexWidth = widthOf(node, "index_width");
      if (!table || !width || !index || !indexWidth) {
        return core::fail(std::format("line {}: load_indexed needs table, width, index and "
                                      "index_width",
                                      node.line()));
      }
      in.target = *table;
      in.width = *width;
      in.variable = *index;
      in.indexWidth = *indexWidth;
      return {};
    }
    case EffectOpcode::Jump:
    case EffectOpcode::Call: {
      auto to = target(node, "target");
      if (!to) {
        return std::unexpected{to.error()};
      }
      in.target = *to;
      return {};
    }
    case EffectOpcode::SubOp: {
      auto sub = textOf(node, "sub");
      if (!sub) {
        return std::unexpected{sub.error()};
      }
      in.sub = *sub;
      if (const YamlNode* args = node.find("args"); args != nullptr && args->isMapping()) {
        in.argCount = static_cast<std::uint8_t>(args->entries().size());
      }
      return {};
    }
    case EffectOpcode::Spawn: {
      auto count = integerOf(node, "count");
      auto table = target(node, "table");
      auto stride = integerOf(node, "stride");
      auto ordinal = integerOf(node, "ordinal");
      if (!count || !table || !stride || !ordinal) {
        return core::fail(
            std::format("line {}: spawn needs count, table, stride and ordinal", node.line()));
      }
      in.count = static_cast<std::int16_t>(*count);
      in.target = *table;
      in.stride = static_cast<std::int16_t>(*stride);
      in.ordinal = static_cast<std::uint8_t>(*ordinal);
      return {};
    }
    case EffectOpcode::LoadRandom:
    case EffectOpcode::Stop:
    case EffectOpcode::Stop6:
    case EffectOpcode::Stop8:
    case EffectOpcode::Return:
      return {};
    }
    return {};
  }

  core::Result<void> variableOperands(const YamlNode& node, EffectInstruction& in) const {
    auto var = variable(node, "variable");
    auto width = widthOf(node, "width");
    if (!var || !width) {
      return std::unexpected{!var ? var.error() : width.error()};
    }
    in.variable = *var;
    in.width = *width;
    if (in.opcode == EffectOpcode::ApplyOperator) {
      auto name = textOf(node, "operator");
      if (!name) {
        return std::unexpected{name.error()};
      }
      if (const auto op = named<EffectOperator>(OperatorNames, *name)) {
        in.op = *op;
        return {};
      }
      return core::fail(std::format("line {}: {} is not an operator", node.line(), *name));
    }
    if (in.opcode == EffectOpcode::BranchIf) {
      auto name = textOf(node, "comparison");
      auto to = target(node, "target");
      if (!name || !to) {
        return std::unexpected{!name ? name.error() : to.error()};
      }
      in.target = *to;
      if (const auto comparison = named<EffectComparison>(ComparisonNames, *name)) {
        in.comparison = *comparison;
        return {};
      }
      return core::fail(std::format("line {}: {} is not a comparison", node.line(), *name));
    }
    return {};
  }

  core::Result<void> write(const YamlNode& node, std::uint32_t at, std::int64_t value,
                           std::size_t size) {
    if (std::size_t{at} + size > program_.data.size()) {
      return core::fail(
          std::format("line {}: data at {:#06x} runs past the data area", node.line(), at));
    }
    auto bits = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < size; ++i) {
      program_.data[at + i] = static_cast<std::byte>(bits & ByteMask);
      bits >>= ByteBits;
    }
    return {};
  }

  // A value, or nothing when the YAML says null (fill).
  core::Result<void> writeValue(const YamlNode& node, const YamlNode& value, std::uint32_t at,
                                std::size_t size) {
    if (!value.isScalar() || value.text() == NullText) {
      return {};
    }
    const std::optional<std::int64_t> number = parseInteger(value.text());
    if (!number) {
      return core::fail(std::format("line {}: {} is not a number", value.line(), value.text()));
    }
    return write(node, at, *number, size);
  }

  core::Result<void> writeValues(const YamlNode& node, std::uint32_t at, std::size_t size) {
    const YamlNode* values = node.find("values");
    if (values == nullptr || !values->isSequence()) {
      return {};
    }
    for (const YamlNode& value : values->items()) {
      if (auto written = writeValue(node, value, at, size); !written) {
        return written;
      }
      at += static_cast<std::uint32_t>(size);
    }
    return {};
  }

  core::Result<void> writeBytes(const YamlNode& node, std::string_view key, std::uint32_t at) {
    const YamlNode* bytes = node.find(key);
    if (bytes == nullptr) {
      return {};
    }
    const std::string& text = bytes->text();
    for (std::size_t i = 0; i + 1 < text.size(); i += HexDigitsPerByte) {
      const auto byte =
          parseNumber<std::uint8_t>(std::string_view{text}.substr(i, HexDigitsPerByte), HexBase);
      if (!byte) {
        return core::fail(std::format("line {}: {} is not hex bytes", node.line(), key));
      }
      if (auto written =
              write(node, at + static_cast<std::uint32_t>(i / HexDigitsPerByte), *byte, 1);
          !written) {
        return written;
      }
    }
    return {};
  }

  core::Result<void> dataItem(const YamlNode& node) {
    auto at = integerOf(node, "at");
    auto kind = textOf(node, "kind");
    if (!at || !kind) {
      return std::unexpected{!at ? at.error() : kind.error()};
    }
    const auto offset = static_cast<std::uint32_t>(*at);
    if (*kind == "instances") {
      return instances(node, offset);
    }
    if (*kind == "variable" || *kind == "table") {
      auto width = integerOf(node, "width");
      if (!width) {
        return std::unexpected{width.error()};
      }
      const auto size = static_cast<std::size_t>(*width);
      if (*kind == "variable") {
        const YamlNode* value = node.find("value");
        return value == nullptr ? core::Result<void>{} : writeValue(node, *value, offset, size);
      }
      if (auto written = writeValues(node, offset, size); !written) {
        return written;
      }
      const YamlNode* values = node.find("values");
      const std::size_t count = values != nullptr ? values->items().size() : 0;
      return writeBytes(node, "tail", offset + static_cast<std::uint32_t>(count * size));
    }
    if (*kind == "int32" || *kind == "parent_field" || *kind == "quad_corner") {
      auto size = integerOf(node, "size");
      const YamlNode* value = node.find("value");
      if (value != nullptr) {
        return writeValue(node, *value, offset, size ? static_cast<std::size_t>(*size) : Int32Size);
      }
      return writeValues(node, offset, Int32Size);
    }
    if (*kind == "vector" || *kind == "color" || *kind == "int32_array") {
      return writeValues(node, offset, Int32Size);
    }
    if (*kind == "unused" || *kind == "unreferenced") {
      return writeBytes(node, "bytes", offset);
    }
    if (*kind == "bone_offsets") {
      return boneOffsets(node, offset);
    }
    if (*kind == "tile_offsets") {
      return writeValues(node, offset, Int16Size);
    }
    if (*kind == "tile_map") {
      return tileMap(node, offset);
    }
    if (*kind == "parallax_sprites") {
      return parallaxSprites(node, offset);
    }
    if (*kind == "uv_keyframes") {
      return uvKeyframes(node, offset);
    }
    return {};
  }

  // EfeTechBoneOffset: s16 type, s16 bone, s16 x, y, z; the type stays a name.
  core::Result<void> boneOffsets(const YamlNode& node, std::uint32_t at) {
    std::vector<EffectBoneOffset> list;
    if (const YamlNode* entries = node.find("entries"); entries != nullptr) {
      for (const YamlNode& entry : entries->items()) {
        EffectBoneOffset item;
        auto species = textOf(entry, "digimon");
        auto bone = integerOf(entry, "bone");
        const YamlNode* offset = entry.find("offset");
        if (!species || !bone || offset == nullptr ||
            offset->items().size() != item.offset.size()) {
          return core::fail(
              std::format("line {}: a bone offset needs a species, bone and offset", entry.line()));
        }
        item.species = *species;
        item.bone = static_cast<std::int16_t>(*bone);
        const std::uint32_t entryAt =
            at + static_cast<std::uint32_t>(list.size() * BoneOffsetEntrySize);
        if (auto written = write(entry, entryAt + Int16Size, item.bone, Int16Size); !written) {
          return written;
        }
        for (std::size_t axis = 0; axis < item.offset.size(); ++axis) {
          const auto value = parseInteger(offset->items()[axis].text());
          if (!value) {
            return core::fail(std::format("line {}: {} is not a number", entry.line(),
                                          offset->items()[axis].text()));
          }
          item.offset.at(axis) = static_cast<std::int16_t>(*value);
          const auto axisAt = entryAt + static_cast<std::uint32_t>((2 + axis) * Int16Size);
          if (auto written = write(entry, axisAt, *value, Int16Size); !written) {
            return written;
          }
        }
        list.push_back(std::move(item));
      }
    }
    // The end of the list: a negative type.
    if (auto written =
            write(node, at + static_cast<std::uint32_t>(list.size() * BoneOffsetEntrySize),
                  BoneOffsetsEnd, Int16Size);
        !written) {
      return written;
    }
    program_.boneOffsets[at] = std::move(list);
    return {};
  }

  // BTL_renderScrollingBackground's tiles: rows of s8 indices into its tile offsets.
  core::Result<void> tileMap(const YamlNode& node, std::uint32_t at) {
    const YamlNode* rows = node.find("tiles");
    if (rows == nullptr || !rows->isSequence()) {
      return {};
    }
    for (const YamlNode& row : rows->items()) {
      for (const YamlNode& tile : row.items()) {
        if (auto written = writeValue(node, tile, at, 1); !written) {
          return written;
        }
        ++at;
      }
    }
    return {};
  }

  // EfeParallaxSprite: s16 size, x, y, depth, u; a negative size ends the list.
  core::Result<void> parallaxSprites(const YamlNode& node, std::uint32_t at) {
    if (const YamlNode* entries = node.find("entries"); entries != nullptr) {
      for (const YamlNode& entry : entries->items()) {
        for (std::uint32_t field = 0; const char* key : {"size", "x", "y", "depth", "u"}) {
          const YamlNode* value = entry.find(key);
          if (value == nullptr) {
            return core::fail(
                std::format("line {}: a parallax sprite needs {}", entry.line(), key));
          }
          if (auto written = writeValue(entry, *value, at + (field * Int16Size), Int16Size);
              !written) {
            return written;
          }
          ++field;
        }
        at += static_cast<std::uint32_t>(ParallaxSpriteSize);
      }
    }
    return write(node, at, ListEnd, Int16Size);
  }

  // s32 count, then per keyframe s16 frames, s8 du, s8 dv (EfeUvKeyframe).
  core::Result<void> uvKeyframes(const YamlNode& node, std::uint32_t at) {
    const YamlNode* keyframes = node.find("keyframes");
    if (keyframes == nullptr) {
      return {};
    }
    const std::size_t count = keyframes->items().size();
    if (auto written = write(node, at, static_cast<std::int64_t>(count), Int32Size); !written) {
      return written;
    }
    std::uint32_t entryAt = at + static_cast<std::uint32_t>(Int32Size);
    for (const YamlNode& keyframe : keyframes->items()) {
      auto frames = integerOf(keyframe, "frames");
      auto du = integerOf(keyframe, "du");
      auto dv = integerOf(keyframe, "dv");
      if (!frames || !du || !dv) {
        return core::fail(
            std::format("line {}: a UV keyframe needs frames, du and dv", keyframe.line()));
      }
      for (auto [value, offset, size] :
           std::array{std::tuple{*frames, 0U, Int16Size}, std::tuple{*du, 2U, std::size_t{1}},
                      std::tuple{*dv, 3U, std::size_t{1}}}) {
        if (auto written = write(keyframe, entryAt + offset, value, size); !written) {
          return written;
        }
      }
      entryAt += static_cast<std::uint32_t>(UvKeyframeSize);
    }
    return {};
  }

  core::Result<void> instances(const YamlNode& node, std::uint32_t at) {
    if (node.find("bytes") != nullptr) {
      return writeBytes(node, "bytes", at);
    }
    auto count = integerOf(node, "count");
    auto stride = integerOf(node, "stride");
    if (!count || !stride) {
      return std::unexpected{!count ? count.error() : stride.error()};
    }
    for (std::int64_t i = 0; i < *count; ++i) {
      if (auto written = write(node, at + static_cast<std::uint32_t>(i * *stride),
                               FreeInstanceFrame, Int32Size);
          !written) {
        return written;
      }
    }
    return {};
  }

  std::map<std::string, std::uint32_t, std::less<>> labels_;
  EffectScript program_;
};

} // namespace

std::optional<std::size_t> EffectScript::instructionAt(std::uint32_t offset) const {
  const auto found = std::ranges::lower_bound(instructions, offset, {}, &EffectInstruction::offset);
  if (found == instructions.end() || found->offset != offset) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - instructions.begin());
}

core::Result<EffectScript> readEffectScript(const YamlNode& effect, const YamlNode& script) {
  return Reader{}.read(effect, script);
}

} // namespace dwe::assets
