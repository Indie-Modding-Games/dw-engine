#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::session {

// MAPHEAD's line tables, by section: a section that is a run of JUMP_TO to SHOW_TEXTBOX, as the
// text keys of those texts in their order (script_common.c: resolveMapHeadEntry reads entry i of
// the section and shows the text it jumps to). Read from the MAPHEAD listing, script/maphead.
using MapHeadLineTables = std::map<std::int32_t, std::vector<std::string>>;

core::Result<MapHeadLineTables> readMapHeadLineTables(const assets::VirtualFileSystem& files);

} // namespace dwe::session
