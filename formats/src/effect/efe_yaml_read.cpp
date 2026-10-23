#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/effect/efe_file.hpp>
#include <dwe/formats/effect/efe_script.hpp>
#include <dwe/formats/effect/efe_yaml.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::formats::effect {

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
constexpr std::size_t OperatorCount = static_cast<std::size_t>(EfeOperator::ShiftRight) + 1;
constexpr std::size_t ComparisonCount = static_cast<std::size_t>(EfeComparison::GreaterOrEqual) + 1;
constexpr std::uint32_t ByteBits = 8;
constexpr std::uint32_t ByteMask = 0xFF;
// A free instance starts with a frame of -1 (EfeInstance.frame; battle_effect.c: spawn looks for
// one).
constexpr std::int64_t FreeInstanceFrame = -1;
constexpr std::size_t Int32Size = 4;

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
  core::Result<EfeProgram> read(const YamlNode& root) {
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
    program_.data.assign(program_.startScript, EfeFill);
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

  static core::Result<EfeVariable> variable(const YamlNode& node, std::string_view key) {
    auto text = textOf(node, key);
    if (!text) {
      return std::unexpected{text.error()};
    }
    std::string_view rest = *text;
    EfeVariable result;
    if (rest.starts_with(BasePrefix)) {
      result.space = EfeSpace::Base;
      result.flag = EfeBaseFlag;
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
    if (const YamlNode* flag = node.find(std::format("{}_flag", key)); flag != nullptr) {
      if (const auto value = parseInteger(flag->text())) {
        result.flag = static_cast<std::uint16_t>(*value);
      }
    }
    return result;
  }

  static core::Result<EfeWidth> widthOf(const YamlNode& node, std::string_view key) {
    auto width = integerOf(node, key);
    if (!width) {
      return std::unexpected{width.error()};
    }
    switch (*width) {
    case static_cast<std::int64_t>(EfeWidth::Int8):
      return EfeWidth::Int8;
    case static_cast<std::int64_t>(EfeWidth::Int16):
      return EfeWidth::Int16;
    case static_cast<std::int64_t>(EfeWidth::Int32):
      return EfeWidth::Int32;
    default:
      return core::fail(std::format("line {}: {} is not 1, 2 or 4", node.line(), key));
    }
  }

  static std::optional<EfeOpcode> opcodeNamed(std::string_view name) {
    for (std::size_t code = 0; code < EfeOpcodeCount; ++code) {
      const auto opcode = static_cast<EfeOpcode>(code);
      if (efeOpcodeName(opcode) == name) {
        return opcode;
      }
    }
    return std::nullopt;
  }

  static std::optional<std::uint8_t> subOpNamed(std::string_view name) {
    for (std::size_t code = 0; code < EfeSubOpCount; ++code) {
      if (efeSubOp(static_cast<std::uint8_t>(code)).name == name) {
        return static_cast<std::uint8_t>(code);
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] core::Result<EfeInstruction> instruction(const YamlNode& node) const {
    EfeInstruction in;
    auto at = integerOf(node, "at");
    auto name = textOf(node, "op");
    if (!at || !name) {
      return std::unexpected{!at ? at.error() : name.error()};
    }
    in.offset = static_cast<std::uint32_t>(*at);
    const std::optional<EfeOpcode> opcode = opcodeNamed(*name);
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

  // The operands each op's YAML gives (efeScriptToYaml).
  core::Result<void> fill(const YamlNode& node, EfeInstruction& in) const {
    switch (in.opcode) {
    case EfeOpcode::LoadImmediate:
    case EfeOpcode::PushImmediate: {
      auto value = integerOf(node, "value");
      if (!value) {
        return std::unexpected{value.error()};
      }
      in.value = static_cast<std::int16_t>(*value);
      return {};
    }
    case EfeOpcode::LoadVariable:
    case EfeOpcode::PushVariable:
    case EfeOpcode::ApplyOperator:
    case EfeOpcode::BranchIf:
      return variableOperands(node, in);
    case EfeOpcode::PushAddress:
    case EfeOpcode::PopToVariable: {
      auto var = variable(node, in.opcode == EfeOpcode::PushAddress ? "address" : "variable");
      if (!var) {
        return std::unexpected{var.error()};
      }
      in.variable = *var;
      return {};
    }
    case EfeOpcode::LoadIndexed: {
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
    case EfeOpcode::Jump:
    case EfeOpcode::Call: {
      auto to = target(node, "target");
      if (!to) {
        return std::unexpected{to.error()};
      }
      in.target = *to;
      return {};
    }
    case EfeOpcode::SubOp: {
      auto sub = textOf(node, "sub");
      if (!sub) {
        return std::unexpected{sub.error()};
      }
      const std::optional<std::uint8_t> code = subOpNamed(*sub);
      if (!code) {
        return core::fail(std::format("line {}: {} is not a sub-op", node.line(), *sub));
      }
      in.subOp = *code;
      return {};
    }
    case EfeOpcode::Spawn: {
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
    case EfeOpcode::LoadRandom:
    case EfeOpcode::Stop:
    case EfeOpcode::Stop6:
    case EfeOpcode::Stop8:
    case EfeOpcode::Return:
      return {};
    }
    return {};
  }

  core::Result<void> variableOperands(const YamlNode& node, EfeInstruction& in) const {
    auto var = variable(node, "variable");
    auto width = widthOf(node, "width");
    if (!var || !width) {
      return std::unexpected{!var ? var.error() : width.error()};
    }
    in.variable = *var;
    in.width = *width;
    if (in.opcode == EfeOpcode::ApplyOperator) {
      auto name = textOf(node, "operator");
      if (!name) {
        return std::unexpected{name.error()};
      }
      for (std::size_t op = 0; op < OperatorCount; ++op) {
        if (efeOperatorName(static_cast<EfeOperator>(op)) == *name) {
          in.op = static_cast<EfeOperator>(op);
          return {};
        }
      }
      return core::fail(std::format("line {}: {} is not an operator", node.line(), *name));
    }
    if (in.opcode == EfeOpcode::BranchIf) {
      auto name = textOf(node, "comparison");
      auto to = target(node, "target");
      if (!name || !to) {
        return std::unexpected{!name ? name.error() : to.error()};
      }
      in.target = *to;
      for (std::size_t comparison = 0; comparison < ComparisonCount; ++comparison) {
        if (efeComparisonName(static_cast<EfeComparison>(comparison)) == *name) {
          in.comparison = static_cast<EfeComparison>(comparison);
          return {};
        }
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
  EfeProgram program_;
};

} // namespace

std::optional<std::size_t> EfeProgram::instructionAt(std::uint32_t offset) const {
  const auto found = std::ranges::lower_bound(instructions, offset, {}, &EfeInstruction::offset);
  if (found == instructions.end() || found->offset != offset) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - instructions.begin());
}

core::Result<EfeProgram> readEfeScriptYaml(const YamlNode& root) {
  return Reader{}.read(root);
}

} // namespace dwe::formats::effect
