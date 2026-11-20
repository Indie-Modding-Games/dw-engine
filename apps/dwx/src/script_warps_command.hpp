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

// field/script_warps and field/npc_spawns as the story Lua of the packs gives them
// (docs/architecture/script-warps.md, «From the Lua»).
struct DerivedScriptWarps {
  std::string yaml; // field/script_warps, as dwx extract writes it
  // field/npc_spawns, as dwx extract writes it: the NPCs the map heads put on their maps, which
  // co-op shows at once when another player's story change moves them.
  std::string spawnsYaml;
  std::size_t modules = 0;
  std::size_t warps = 0;
  std::size_t spawns = 0;
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

// Where dwx script-warps writes the derived documents: field/script_warps to `warps`,
// field/npc_spawns to `spawns`.
struct ScriptWarpsOutputs {
  std::optional<std::filesystem::path> warps;
  std::optional<std::filesystem::path> spawns;
};

// dwx script-warps: with an output, writes the derived documents asked for there; without, compares
// them with the packs' field/script_warps and field/npc_spawns (the latter only when the packs have
// it) and says where they first differ. ExitOk when written or equal, ExitUnknown when one differs,
// ExitFailed when the Lua or the packs do not read.
int scriptWarpsCommand(std::span<const std::filesystem::path> folders,
                       const ScriptWarpsOutputs& outputs, std::string_view rules);

} // namespace dwe::dwx
