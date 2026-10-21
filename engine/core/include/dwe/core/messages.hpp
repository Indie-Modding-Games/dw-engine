#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

namespace dwe::core::messages {

// The error messages of the core module, as std::format templates for core::formatMessage.

// JSON schemas: the first argument is the place in the schema, such as #/properties/id.
inline constexpr std::string_view SchemaNotMapping = "{}: must be a mapping";
inline constexpr std::string_view SchemaPatternInvalid = "{}: {}";

} // namespace dwe::core::messages
