#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <string_view>

#include "extractor.hpp"

// The extractors of the event scripts (script_interp.c: initializeScripts, getScript).
namespace dwe::dwx {

// SCN/MAPHEAD.SCN and SCN/DG.SCN: one listing per script, script/maphead and script/dg/<id>.
bool handlesScriptFile(std::string_view path);
core::Result<Extraction> extractScriptFile(const Disc& disc, const formats::disc::IsoFile& file);

} // namespace dwe::dwx
