#pragma once

// The names of the metatable fields that the engine reads and sets. C strings: Lua takes them without a size.
namespace dwe::script::detail {

inline constexpr const char* MetatableField = "__metatable";
inline constexpr const char* IndexField = "__index";
inline constexpr const char* NewIndexField = "__newindex";
inline constexpr const char* LengthField = "__len";
inline constexpr const char* FinalizerField = "__gc";
inline constexpr const char* ToStringField = "__tostring";
inline constexpr const char* NameField = "__name";

} // namespace dwe::script::detail
