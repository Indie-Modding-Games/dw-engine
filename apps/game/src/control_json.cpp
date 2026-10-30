#include "control_json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <ranges>
#include <system_error>
#include <utility>

namespace dwe::app {

namespace {

// Deeper than any request or trace line; it keeps a hostile line from exhausting the stack.
constexpr int MaxDepth = 64;

class Reader {
public:
  explicit Reader(std::string_view text) : text_{text} {}

  core::Result<JsonValue> document() {
    auto value = this->value(0);
    if (!value) {
      return value;
    }
    skipSpaces();
    if (at_ != text_.size()) {
      return failHere("text after the value");
    }
    return value;
  }

private:
  std::unexpected<core::Error> failHere(std::string_view problem) const {
    return core::fail(std::format("json: {} at column {}", problem, at_ + 1));
  }

  void skipSpaces() {
    while (at_ < text_.size() &&
           (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) {
      ++at_;
    }
  }

  bool take(std::string_view word) {
    if (text_.substr(at_).starts_with(word)) {
      at_ += word.size();
      return true;
    }
    return false;
  }

  core::Result<JsonValue> value(int depth) {
    if (depth > MaxDepth) {
      return failHere("too deep");
    }
    skipSpaces();
    if (at_ >= text_.size()) {
      return failHere("a value is missing");
    }
    const char c = text_[at_];
    if (c == '{') {
      return object(depth);
    }
    if (c == '[') {
      return array(depth);
    }
    if (c == '"') {
      auto read = string();
      if (!read) {
        return std::unexpected{read.error()};
      }
      return JsonValue{std::move(*read)};
    }
    if (take("true")) {
      return JsonValue{true};
    }
    if (take("false")) {
      return JsonValue{false};
    }
    if (take("null")) {
      return JsonValue{nullptr};
    }
    return number();
  }

  core::Result<JsonValue> number() {
    double parsed = 0.0;
    const char* first = text_.data() + at_;
    const auto read = std::from_chars(first, text_.data() + text_.size(), parsed);
    if (read.ec != std::errc{} || read.ptr == first || !std::isfinite(parsed)) {
      return failHere("not a value");
    }
    at_ += static_cast<std::size_t>(read.ptr - first);
    return JsonValue{parsed};
  }

  // Four hex digits of a \u escape.
  std::optional<std::uint32_t> hex4() {
    if (at_ + 4 > text_.size()) {
      return std::nullopt;
    }
    std::uint32_t code = 0;
    const auto read = std::from_chars(text_.data() + at_, text_.data() + at_ + 4, code, 16);
    if (read.ec != std::errc{} || read.ptr != text_.data() + at_ + 4) {
      return std::nullopt;
    }
    at_ += 4;
    return code;
  }

  static void appendUtf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
      out += static_cast<char>(code);
    } else if (code < 0x800) {
      out += static_cast<char>(0xC0 | (code >> 6));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
      out += static_cast<char>(0xE0 | (code >> 12));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (code >> 18));
      out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    }
  }

  core::Result<std::string> string() {
    ++at_; // the opening quote
    std::string out;
    while (at_ < text_.size()) {
      const char c = text_[at_++];
      if (c == '"') {
        return out;
      }
      if (static_cast<unsigned char>(c) < 0x20) {
        return failHere("a control character in a string");
      }
      if (c != '\\') {
        out += c;
        continue;
      }
      if (at_ >= text_.size()) {
        break;
      }
      switch (const char escaped = text_[at_++]) {
      case '"':
      case '\\':
      case '/':
        out += escaped;
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u': {
        auto code = hex4();
        if (!code) {
          return failHere("a bad \\u escape");
        }
        // A surrogate pair is one code point.
        if (*code >= 0xD800 && *code < 0xDC00 && take("\\u")) {
          const auto low = hex4();
          if (!low || *low < 0xDC00 || *low >= 0xE000) {
            return failHere("a bad surrogate pair");
          }
          *code = 0x10000 + ((*code - 0xD800) << 10) + (*low - 0xDC00);
        }
        appendUtf8(out, *code);
        break;
      }
      default:
        return failHere("an unknown escape");
      }
    }
    return failHere("an unterminated string");
  }

  core::Result<JsonValue> array(int depth) {
    ++at_;
    JsonArray items;
    skipSpaces();
    if (take("]")) {
      return JsonValue{std::move(items)};
    }
    while (true) {
      auto item = value(depth + 1);
      if (!item) {
        return item;
      }
      items.push_back(std::move(*item));
      skipSpaces();
      if (take("]")) {
        return JsonValue{std::move(items)};
      }
      if (!take(",")) {
        return failHere("',' or ']' expected");
      }
    }
  }

  core::Result<JsonValue> object(int depth) {
    ++at_;
    JsonObject members;
    skipSpaces();
    if (take("}")) {
      return JsonValue{std::move(members)};
    }
    while (true) {
      skipSpaces();
      if (at_ >= text_.size() || text_[at_] != '"') {
        return failHere("a key expected");
      }
      auto key = string();
      if (!key) {
        return std::unexpected{key.error()};
      }
      skipSpaces();
      if (!take(":")) {
        return failHere("':' expected");
      }
      auto item = value(depth + 1);
      if (!item) {
        return item;
      }
      members.push_back({.key = std::move(*key), .value = std::move(*item)});
      skipSpaces();
      if (take("}")) {
        return JsonValue{std::move(members)};
      }
      if (!take(",")) {
        return failHere("',' or '}' expected");
      }
    }
  }

  std::string_view text_;
  std::size_t at_ = 0;
};

void write(std::string& out, const JsonValue& value) {
  if (value.isNull()) {
    out += "null";
  } else if (const bool* flag = value.boolean()) {
    out += *flag ? "true" : "false";
  } else if (const double* number = value.number()) {
    if (*number == std::trunc(*number) && std::abs(*number) < 1e15) {
      std::format_to(std::back_inserter(out), "{}", static_cast<std::int64_t>(*number));
    } else {
      std::format_to(std::back_inserter(out), "{}", *number);
    }
  } else if (const std::string* text = value.text()) {
    out += jsonString(*text);
  } else if (const JsonArray* items = value.array()) {
    out += '[';
    for (std::size_t index = 0; index < items->size(); ++index) {
      if (index > 0) {
        out += ',';
      }
      write(out, (*items)[index]);
    }
    out += ']';
  } else if (const JsonObject* members = value.object()) {
    out += '{';
    for (std::size_t index = 0; index < members->size(); ++index) {
      if (index > 0) {
        out += ',';
      }
      out += jsonString((*members)[index].key);
      out += ':';
      write(out, (*members)[index].value);
    }
    out += '}';
  }
}

std::optional<std::size_t> indexOf(std::string_view text) {
  std::size_t index = 0;
  const auto read = std::from_chars(text.data(), text.data() + text.size(), index);
  if (text.empty() || read.ec != std::errc{} || read.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return index;
}

// Whether an element's member is the text of a `key=value` segment, as a number or as text.
bool memberMatches(const JsonValue& member, std::string_view wanted) {
  if (const std::string* text = member.text()) {
    return *text == wanted;
  }
  if (const double* number = member.number()) {
    double parsed = 0.0;
    const auto read = std::from_chars(wanted.data(), wanted.data() + wanted.size(), parsed);
    return read.ec == std::errc{} && read.ptr == wanted.data() + wanted.size() && parsed == *number;
  }
  return false;
}

} // namespace

const JsonValue* JsonValue::find(std::string_view key) const {
  const JsonObject* members = object();
  if (members == nullptr) {
    return nullptr;
  }
  const auto found = std::ranges::find(*members, key, &JsonMember::key);
  return found == members->end() ? nullptr : &found->value;
}

core::Result<JsonValue> parseJson(std::string_view text) {
  return Reader{text}.document();
}

std::string writeJson(const JsonValue& value) {
  std::string out;
  write(out, value);
  return out;
}

std::string jsonString(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        std::format_to(std::back_inserter(out), "\\u{:04x}", static_cast<unsigned>(c));
      } else {
        out += c;
      }
    }
  }
  out += '"';
  return out;
}

core::Result<JsonValue> selectJson(const JsonValue& value, std::string_view path) {
  const JsonValue* at = &value;
  std::string walked;
  for (const auto part : std::views::split(path, '.')) {
    const std::string_view segment{part};
    if (segment.empty()) {
      continue;
    }
    const JsonValue* next = nullptr;
    if (const JsonArray* items = at->array()) {
      if (const auto index = indexOf(segment)) {
        next = *index < items->size() ? &(*items)[*index] : nullptr;
      } else if (const auto equals = segment.find('='); equals != std::string_view::npos) {
        const std::string_view key = segment.substr(0, equals);
        const std::string_view wanted = segment.substr(equals + 1);
        const auto found = std::ranges::find_if(*items, [&](const JsonValue& item) {
          const JsonValue* member = item.find(key);
          return member != nullptr && memberMatches(*member, wanted);
        });
        next = found == items->end() ? nullptr : &*found;
      }
    } else {
      next = at->find(segment);
    }
    if (next == nullptr) {
      return core::fail(std::format("'{}': nothing at '{}'{}", path, segment,
                                    walked.empty() ? "" : std::format(" under '{}'", walked)));
    }
    walked += walked.empty() ? "" : ".";
    walked += segment;
    at = next;
  }
  return *at;
}

} // namespace dwe::app
