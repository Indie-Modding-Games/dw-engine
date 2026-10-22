#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

// The user's own assets that scripts/dev.sh mounts read-only for the tests, one owner for every
// test that reads them. The CI has none: a test that needs one skips when it is not there, and its
// name starts with "the user's disc" so it runs as a slow test (cmake/DweModule.cmake).
namespace dwe::test_support {

inline constexpr std::string_view DiscVariable = "DWE_DISC";
inline constexpr std::string_view ContentVariable = "DWE_CONTENT";
inline constexpr std::string_view DecompVariable = "DWE_DECOMP";

// The user's disc image (DWE_DISC), when there is one.
[[nodiscard]] std::optional<std::filesystem::path> userDisc();

// A content folder extracted from the user's disc with dwx (DWE_CONTENT), when there is one.
[[nodiscard]] std::optional<std::filesystem::path> userContent();

// A checkout of the decompilation (DWE_DECOMP), when there is one.
[[nodiscard]] std::optional<std::filesystem::path> userDecomp();

} // namespace dwe::test_support
