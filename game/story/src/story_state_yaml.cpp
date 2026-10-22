#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/story/story_state_yaml.hpp>

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include "messages.hpp"

namespace dwe::game::story {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view TriggersKey = "triggers";
constexpr std::string_view VariablesKey = "variables";
constexpr std::int32_t BitsPerByte = 8;
constexpr std::int32_t TriggerBytes = TriggerCount / BitsPerByte;
constexpr std::int32_t DigitsPerByte = 2;
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

std::string triggerDigits(const StoryState& state) {
    std::string digits;
    for (std::int32_t byte = 0; byte < TriggerBytes; ++byte) {
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

// Sets the triggers the digits hold; false if they are not TriggerBytes bytes of hex digits.
bool readTriggerDigits(std::string_view digits, StoryState& state) {
    if (digits.size() != static_cast<std::size_t>(TriggerBytes) * static_cast<std::size_t>(DigitsPerByte)) {
        return false;
    }
    for (std::int32_t byte = 0; byte < TriggerBytes; ++byte) {
        const std::size_t at = static_cast<std::size_t>(byte) * static_cast<std::size_t>(DigitsPerByte);
        const std::int32_t high = digitValue(digits[at]);
        const std::int32_t low = digitValue(digits[at + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        const auto bits = static_cast<unsigned>((high * HexBase) + low);
        for (std::int32_t bit = 0; bit < BitsPerByte; ++bit) {
            if (!state.setTrigger((byte * BitsPerByte) + bit, ((bits >> bit) & 1U) != 0)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

YamlNode toYaml(const StoryState& state) {
    YamlNode node = YamlNode::mapping();
    node.set(std::string{TriggersKey}, YamlNode::scalar(triggerDigits(state), YamlNode::ScalarStyle::Quoted));
    YamlNode variables = YamlNode::sequence();
    for (std::int32_t variable = 0; variable < SavedVariableCount; ++variable) {
        variables.append(YamlNode::scalar(std::format("{}", state.variable(variable).value_or(0))));
    }
    node.set(std::string{VariablesKey}, std::move(variables));
    return node;
}

core::Result<StoryState> fromYaml(const YamlNode& node) {
    YamlReader reader;
    if (!reader.expectKeys(node, messages::StoryStateText, {TriggersKey, VariablesKey})) {
        return std::unexpected{reader.error(messages::StoryStateText)};
    }
    StoryState state;
    const YamlNode& triggers = reader.field(node, TriggersKey);
    if (reader.ok() && !readTriggerDigits(reader.text(triggers), state)) {
        reader.fail(triggers, core::formatMessage(messages::TriggerDigitsWrong, TriggerBytes * DigitsPerByte));
    }
    const auto variables =
        reader.sequence(reader.field(node, VariablesKey), VariablesKey, static_cast<std::size_t>(SavedVariableCount));
    for (std::size_t index = 0; reader.ok() && index < variables.size(); ++index) {
        const auto value = reader.integer<std::int32_t>(variables[index]);
        if (value < 0 || value > ByteLimit) {
            reader.fail(variables[index], core::formatMessage(messages::VariableNotAByte, value));
            break;
        }
        if (auto set = state.setVariable(static_cast<std::int32_t>(index), static_cast<std::uint8_t>(value)); !set) {
            reader.fail(variables[index], set.error().message);
            break;
        }
    }
    return reader.finish(state, messages::StoryStateText);
}

} // namespace dwe::game::story
