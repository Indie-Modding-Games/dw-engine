#include "api_names.hpp"

#include <algorithm>
#include <cstddef>
#include <format>

namespace dwe::script::detail {
namespace {

constexpr std::size_t MaxNameLength = 64;
constexpr char EventSeparator = '.';

} // namespace

bool isApiName(std::string_view name) {
  const auto isNameCharacter = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  };
  return !name.empty() && name.size() <= MaxNameLength && name.front() >= 'a' &&
         name.front() <= 'z' && std::ranges::all_of(name, isNameCharacter);
}

bool isEventName(std::string_view event) {
  const std::size_t separator = event.find(EventSeparator);
  return separator != std::string_view::npos && isApiName(event.substr(0, separator)) &&
         isApiName(event.substr(separator + 1));
}

std::string qualifiedApiName(std::string_view apiNamespace, std::string_view name) {
  return std::format("{}.{}", apiNamespace, name);
}

} // namespace dwe::script::detail
