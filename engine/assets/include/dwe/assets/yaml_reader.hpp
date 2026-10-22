#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace dwe::assets {

// Reads typed values out of a parsed YAML document. The first problem is kept, with its line and
// column, and every later read returns an empty value, so a reader of a whole document checks for
// an error once at the end instead of after every value.
class YamlReader {
public:
  // Checks that a node is a mapping with exactly these keys: a missing or unknown key is an error,
  // so a misspelt key never goes unnoticed. Returns whether it was.
  bool expectKeys(const core::YamlNode& node, std::string_view what,
                  std::initializer_list<std::string_view> keys);

  // Checks that a node is a mapping whose keys are all among these, and that it has the required
  // ones.
  bool expectKeys(const core::YamlNode& node, std::string_view what,
                  std::initializer_list<std::string_view> required,
                  std::initializer_list<std::string_view> optional);

  // The value of a mapping's key, or an empty scalar (and an error) if it is missing.
  const core::YamlNode& field(const core::YamlNode& mapping, std::string_view key);

  // The items of a sequence, or none (and an error) if the node is not one.
  std::span<const core::YamlNode> sequence(const core::YamlNode& node, std::string_view what);

  // The items of a sequence that must have exactly `count` items.
  std::span<const core::YamlNode> sequence(const core::YamlNode& node, std::string_view what,
                                           std::size_t count);

  // A whole decimal number that fits in T; 0 on error.
  template <std::integral T>
  T integer(const core::YamlNode& node) {
    const std::optional<std::int64_t> value = readInteger(node);
    if (!value) {
      return T{};
    }
    if (*value < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
        *value > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
      fail(node, "the number is out of range");
      return T{};
    }
    return static_cast<T>(*value);
  }

  // A number, or nothing when the node is null.
  template <std::integral T>
  std::optional<T> optionalInteger(const core::YamlNode& node) {
    if (isNull(node)) {
      return std::nullopt;
    }
    return integer<T>(node);
  }

  // The number at a key that a mapping may leave out, or nothing when it is missing or null.
  template <std::integral T>
  std::optional<T> optionalInteger(const core::YamlNode& mapping, std::string_view key) {
    const core::YamlNode* value = mapping.isMapping() ? mapping.find(key) : nullptr;
    if (value == nullptr) {
      return std::nullopt;
    }
    return optionalInteger<T>(*value);
  }

  // A decimal number, whole or with a fraction (tunables such as speeds); 0 on error.
  float number(const core::YamlNode& node);

  // true or false.
  bool boolean(const core::YamlNode& node);

  // The text of a scalar.
  std::string text(const core::YamlNode& node);

  // The text of a scalar, or nothing when the node is null.
  std::optional<std::string> optionalText(const core::YamlNode& node);

  // Whether a node is YAML's null: an unquoted `null` or `~`.
  [[nodiscard]] static bool isNull(const core::YamlNode& node);

  // Records a problem at a node, unless an earlier one is already recorded.
  void fail(const core::YamlNode& at, std::string_view problem);

  // Whether no problem was found.
  [[nodiscard]] bool ok() const { return !error_; }

  // The first problem, prefixed with what was being read.
  [[nodiscard]] core::Error error(std::string_view context) const;

  // The value if no problem was found, otherwise the first problem.
  template <typename T>
  [[nodiscard]] core::Result<T> finish(T value, std::string_view context) const {
    if (error_) {
      return std::unexpected{error(context)};
    }
    return value;
  }

private:
  std::optional<std::int64_t> readInteger(const core::YamlNode& node);

  std::optional<core::Error> error_;
};

} // namespace dwe::assets
