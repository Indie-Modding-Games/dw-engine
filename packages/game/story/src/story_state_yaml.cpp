#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/story/story_state_yaml.hpp>

#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include "messages.hpp"

namespace dwe::game::story {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view TriggersKey = "triggers";
constexpr std::string_view VariablesKey = "variables";
constexpr std::string_view ExtraVariablesKey = "extra_variables";
constexpr std::int32_t BitsPerByte = 8;
constexpr std::int32_t BaseTriggerBytes = BaseTriggerCount / BitsPerByte;
constexpr std::size_t DigitsPerByte = 2;
constexpr std::int32_t HexBase = 16;
constexpr std::int32_t ByteLimit = 255;

std::int32_t digitValue(char digit) {
  if (digit >= '0' && digit <= '9') {
    return digit - '0';
  }
  if (digit >= 'a' && digit <= 'f') {
    return digit - 'a' + 10;
  }
  return -1;
}

std::int32_t triggerBytes(const StoryState& state) {
  return (state.triggerCount() + BitsPerByte - 1) / BitsPerByte;
}

std::string triggerDigits(const StoryState& state) {
  std::string digits;
  for (std::int32_t byte = 0; byte < triggerBytes(state); ++byte) {
    unsigned bits = 0;
    for (std::int32_t bit = 0; bit < BitsPerByte; ++bit) {
      if (state.trigger((byte * BitsPerByte) + bit).value_or(false)) {
        bits |= 1U << static_cast<unsigned>(bit);
      }
    }
    digits += std::format("{:02x}", bits);
  }
  return digits;
}

// What a read could not keep: set triggers and non-zero extra variables past the capacity.
struct Dropped {
  std::int32_t triggers = 0;
  std::int32_t variables = 0;
};

// Sets the triggers the digits hold; false if they are not whole bytes of hex digits, at least the
// base set's.
bool readTriggerDigits(std::string_view digits, StoryState& state, Dropped& dropped) {
  if (digits.size() % DigitsPerByte != 0 ||
      digits.size() < static_cast<std::size_t>(BaseTriggerBytes) * DigitsPerByte) {
    return false;
  }
  const std::size_t bytes = digits.size() / DigitsPerByte;
  for (std::size_t byte = 0; byte < bytes; ++byte) {
    const std::size_t at = byte * DigitsPerByte;
    const std::int32_t high = digitValue(digits[at]);
    const std::int32_t low = digitValue(digits[at + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    const auto bits = static_cast<unsigned>((high * HexBase) + low);
    for (std::int32_t bit = 0; bit < BitsPerByte; ++bit) {
      const auto trigger = static_cast<std::int32_t>(byte) * BitsPerByte + bit;
      const bool set = ((bits >> static_cast<unsigned>(bit)) & 1U) != 0;
      if (trigger >= state.triggerCount()) {
        dropped.triggers += set ? 1 : 0;
      } else if (!state.setTrigger(trigger, set)) {
        return false;
      }
    }
  }
  return true;
}

void readBaseVariables(YamlReader& reader, std::span<const YamlNode> variables, StoryState& state) {
  for (std::size_t index = 0; reader.ok() && index < variables.size(); ++index) {
    const auto value = reader.integer<std::int32_t>(variables[index]);
    if (value < 0 || value > ByteLimit) {
      reader.fail(variables[index], core::formatMessage(messages::VariableNotAByte, value));
      return;
    }
    if (auto set = state.setVariable(static_cast<std::int32_t>(index), value); !set) {
      reader.fail(variables[index], set.error().message);
      return;
    }
  }
}

void readExtraVariables(YamlReader& reader, std::span<const YamlNode> variables, StoryState& state,
                        Dropped& dropped) {
  for (std::size_t index = 0; reader.ok() && index < variables.size(); ++index) {
    const auto value = reader.integer<std::int32_t>(variables[index]);
    if (value < 0) {
      reader.fail(variables[index], core::formatMessage(messages::ExtraVariableNegative, value));
      return;
    }
    const auto variable = BaseVariableCount + static_cast<std::int32_t>(index);
    if (variable >= state.variableCount()) {
      dropped.variables += value != 0 ? 1 : 0;
    } else if (auto set = state.setVariable(variable, value); !set) {
      reader.fail(variables[index], set.error().message);
      return;
    }
  }
}

} // namespace

YamlNode toYaml(const StoryState& state) {
  YamlNode node = YamlNode::mapping();
  node.set(std::string{TriggersKey},
           YamlNode::scalar(triggerDigits(state), YamlNode::ScalarStyle::Quoted));
  YamlNode variables = YamlNode::sequence();
  for (std::int32_t variable = 0; variable < SavedVariableCount; ++variable) {
    variables.append(YamlNode::scalar(std::format("{}", state.variable(variable).value_or(0))));
  }
  node.set(std::string{VariablesKey}, std::move(variables));
  YamlNode extra = YamlNode::sequence();
  for (std::int32_t variable = BaseVariableCount; variable < state.variableCount(); ++variable) {
    extra.append(YamlNode::scalar(std::format("{}", state.variable(variable).value_or(0))));
  }
  node.set(std::string{ExtraVariablesKey}, std::move(extra));
  return node;
}

core::Result<StoryState> fromYaml(const YamlNode& node, StoryCapacity capacity) {
  YamlReader reader;
  if (!reader.expectKeys(node, messages::StoryStateText,
                         {TriggersKey, VariablesKey, ExtraVariablesKey})) {
    return std::unexpected{reader.error(messages::StoryStateText)};
  }
  StoryState state{capacity};
  Dropped dropped;
  const YamlNode& triggers = reader.field(node, TriggersKey);
  if (reader.ok() && !readTriggerDigits(reader.text(triggers), state, dropped)) {
    reader.fail(triggers,
                core::formatMessage(messages::TriggerDigitsWrong,
                                    static_cast<std::size_t>(BaseTriggerBytes) * DigitsPerByte));
  }
  readBaseVariables(reader,
                    reader.sequence(reader.field(node, VariablesKey), VariablesKey,
                                    static_cast<std::size_t>(SavedVariableCount)),
                    state);
  readExtraVariables(reader,
                     reader.sequence(reader.field(node, ExtraVariablesKey), ExtraVariablesKey),
                     state, dropped);
  if (reader.ok() && (dropped.triggers > 0 || dropped.variables > 0)) {
    core::logWarning("{}", core::formatMessage(messages::StoryStateDropped, dropped.triggers,
                                               dropped.variables));
  }
  return reader.finish(state, messages::StoryStateText);
}

core::Result<YamlNode> storyStateFromVersion1(YamlNode state) {
  if (!state.isMapping() || state.find(ExtraVariablesKey) != nullptr) {
    return core::fail(std::string{messages::StoryStateNotVersion1});
  }
  state.set(std::string{ExtraVariablesKey}, YamlNode::sequence());
  return state;
}

} // namespace dwe::game::story
