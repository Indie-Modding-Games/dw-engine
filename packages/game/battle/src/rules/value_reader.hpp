#pragma once

#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/battle/messages.hpp>
#include <dwe/game/battle/rules.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

namespace dwe::game::battle {

using assets::YamlReader;
using core::YamlNode;

inline constexpr std::int32_t PercentMax = 100;

// Reads the values of the section with the checks every kind of value needs, naming the key in the
// errors.
class ValueReader {
public:
  explicit ValueReader(YamlReader& reader) : reader_(&reader) {}

  YamlReader& yaml() { return *reader_; }

  bool group(const YamlNode& node, std::string_view what,
             std::initializer_list<std::string_view> keys) {
    return reader_->expectKeys(node, what, keys);
  }

  bool group(const YamlNode& node, std::string_view what,
             std::initializer_list<std::string_view> keys,
             std::initializer_list<std::string_view> optional) {
    return reader_->expectKeys(node, what, keys, optional);
  }

  const YamlNode& field(const YamlNode& node, std::string_view key) {
    return reader_->field(node, key);
  }

  std::int32_t any(const YamlNode& node, std::string_view key) {
    return reader_->integer<std::int32_t>(reader_->field(node, key));
  }

  std::int64_t any64(const YamlNode& node, std::string_view key) {
    return reader_->integer<std::int64_t>(reader_->field(node, key));
  }

  std::int32_t count(const YamlNode& node, std::string_view key) {
    const YamlNode& value = reader_->field(node, key);
    const auto number = reader_->integer<std::int32_t>(value);
    if (reader_->ok() && number < 0) {
      reader_->fail(value, messages::formatMessage(messages::NotNegative, key));
    }
    return number;
  }

  std::int64_t count64(const YamlNode& node, std::string_view key) {
    const YamlNode& value = reader_->field(node, key);
    const auto number = reader_->integer<std::int64_t>(value);
    if (reader_->ok() && number < 0) {
      reader_->fail(value, messages::formatMessage(messages::NotNegative, key));
    }
    return number;
  }

  std::int32_t positive(const YamlNode& node, std::string_view key) {
    const YamlNode& value = reader_->field(node, key);
    const auto number = reader_->integer<std::int32_t>(value);
    if (reader_->ok() && number <= 0) {
      reader_->fail(value, messages::formatMessage(messages::NotPositive, key));
    }
    return number;
  }

  std::int32_t percent(const YamlNode& node, std::string_view key) {
    return percentOf(reader_->field(node, key), key);
  }

  std::int32_t percentOf(const YamlNode& value, std::string_view what) {
    const auto number = reader_->integer<std::int32_t>(value);
    if (reader_->ok() && (number < 0 || number > PercentMax)) {
      reader_->fail(value, messages::formatMessage(messages::NotPercent, what));
    }
    return number;
  }

  std::string text(const YamlNode& node, std::string_view key) {
    return reader_->text(reader_->field(node, key));
  }

  template <std::size_t N>
  std::array<std::int32_t, N> numbers(const YamlNode& node, std::string_view key, bool percents) {
    std::array<std::int32_t, N> values{};
    const std::span<const YamlNode> items = reader_->sequence(reader_->field(node, key), key, N);
    for (std::size_t i = 0; i < items.size(); ++i) {
      values.at(i) = percents ? percentOf(items[i], key) : reader_->integer<std::int32_t>(items[i]);
    }
    return values;
  }

  void ordered(const YamlNode& node, std::string_view low, std::int32_t lowValue,
               std::string_view high, std::int32_t highValue) {
    if (reader_->ok() && lowValue > highValue) {
      reader_->fail(
          node, messages::formatMessage(messages::NotOrdered, messages::SectionText, low, high));
    }
  }

private:
  YamlReader* reader_;
};

// folder_rules_reader.cpp: the `folder:` group (docs/architecture/folder-battle.md).
[[nodiscard]] FolderRules readFolder(ValueReader& in, const YamlNode& node);

} // namespace dwe::game::battle
