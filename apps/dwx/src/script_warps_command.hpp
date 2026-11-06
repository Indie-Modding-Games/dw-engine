#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::dwx {

// The ruleset whose bird_transport lists the transport menu's destinations, unless asked for
// another.
inline constexpr std::string_view DefaultTransportRules = "original";

// field/script_warps as the story Lua of the packs gives it (docs/architecture/warps-de-script.md,
// «Desde el Lua»).
struct DerivedScriptWarps {
  std::string yaml; // the document, as dwx extract writes it
  std::size_t modules = 0;
  std::size_t warps = 0;
  // What a reader should know: modules whose line map was not used (their records have no
  // offsets), maps that cannot be read, a ruleset without destinations.
  std::vector<std::string> notes;
};

// Reads every story module of the packs (story/<n>, with its line map story/<n>_lines) back into
// its listing (formats::script::readLuaModule) and walks them as dwx extract walks the listings:
// the NPCs' sections come from the map documents, the map ids from data/maps and the transport
// menu's destinations from rules/<rules>/bird_transport. Fails, naming each, when a module does not
// read: a module left out would drop its map changes without a word.
core::Result<DerivedScriptWarps> deriveScriptWarps(const assets::VirtualFileSystem& files,
                                                   std::string_view rules);

// deriveScriptWarps on the packs of `folders` (content folders or packs), mounted in that order.
core::Result<DerivedScriptWarps> deriveScriptWarps(std::span<const std::filesystem::path> folders,
                                                   std::string_view rules);

// dwx script-warps: with `out`, writes the derived document there; without, compares it with the
// field/script_warps of the packs and says where they first differ. ExitOk when written or equal,
// ExitUnknown when they differ, ExitFailed when the Lua or the packs do not read.
int scriptWarpsCommand(std::span<const std::filesystem::path> folders,
                       const std::optional<std::filesystem::path>& out, std::string_view rules);

} // namespace dwe::dwx
