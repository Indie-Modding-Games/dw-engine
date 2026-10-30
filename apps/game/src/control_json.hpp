#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dwe::app {

// The JSON the control channel speaks (remote_control.hpp): its requests are read into these
// values, and a part of the state trace's line is picked out of one. Objects keep the order of
// their keys.

struct JsonValue;
struct JsonMember;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::vector<JsonMember>;

struct JsonValue {
  std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> data;

  [[nodiscard]] bool isNull() const { return std::holds_alternative<std::nullptr_t>(data); }
  [[nodiscard]] const bool* boolean() const { return std::get_if<bool>(&data); }
  [[nodiscard]] const double* number() const { return std::get_if<double>(&data); }
  [[nodiscard]] const std::string* text() const { return std::get_if<std::string>(&data); }
  [[nodiscard]] const JsonArray* array() const { return std::get_if<JsonArray>(&data); }
  [[nodiscard]] const JsonObject* object() const { return std::get_if<JsonObject>(&data); }
  // An object's member by key; nothing when this is not an object or has no such key.
  [[nodiscard]] const JsonValue* find(std::string_view key) const;
};

struct JsonMember {
  std::string key;
  JsonValue value;
};

// Reads one JSON value, the whole text (spaces around it aside), or says where it is wrong.
core::Result<JsonValue> parseJson(std::string_view text);

// The value as compact JSON text. A whole number is written without a fraction.
std::string writeJson(const JsonValue& value);

// A JSON string literal of the text, quotes included.
std::string jsonString(std::string_view text);

// The part of a value a dotted path names: an object's key, an array's index, or `key=value` for
// the first element of an array whose `key` is that number or text (npcs.id=6). An empty path is
// the whole value.
core::Result<JsonValue> selectJson(const JsonValue& value, std::string_view path);

} // namespace dwe::app
