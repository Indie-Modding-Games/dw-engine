#include "script_extractors.hpp"

#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/scn_file.hpp>
#include <dwe/formats/script/script_yaml.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "asset_names.hpp"

namespace dwe::dwx {

namespace {

constexpr std::string_view MapHeadPath = "SCN/MAPHEAD.SCN";
constexpr std::string_view ScriptPackPath = "SCN/DG.SCN";

// What the listings of a file add up to, for the notes.
struct Totals {
    std::size_t scripts = 0;
    std::size_t instructions = 0;
    std::size_t texts = 0;
    std::size_t unreachable = 0;
    std::size_t padding = 0; // bytes after the end of the code
};

core::Result<OutputFile> yamlOutput(const core::Result<assets::AssetId>& id, const core::YamlNode& node) {
    if (!id) {
        return std::unexpected{id.error()};
    }
    const auto text = core::emitYaml(node);
    if (!text) {
        return std::unexpected{text.error()};
    }
    const auto bytes = std::as_bytes(std::span{*text});
    return OutputFile{.id = *id, .extension = "yaml", .bytes = std::vector<std::byte>(bytes.begin(), bytes.end())};
}

// Disassembles one script and adds its listing. Every byte up to the end of the code must decode; what follows it
// is the padding of the script's sectors, which nothing reads.
core::Result<void> addScript(Extraction& extraction, Totals& totals, std::string_view volumePath,
                             std::optional<std::size_t> id, std::span<const std::byte> script) {
    const std::string name = id ? std::format("script {}", *id) : std::string{"the script"};
    auto disassembly = formats::script::disassembleScript(script);
    if (!disassembly) {
        extraction.unknowns.push_back(std::format("{}: {}", name, disassembly.error().message));
        return {};
    }
    for (const std::string& unknown : disassembly->unknowns) {
        extraction.unknowns.push_back(std::format("{}: {}", name, unknown));
    }
    auto output = yamlOutput(names::scriptListing(volumePath, id), formats::script::scriptToYaml(*disassembly));
    if (!output) {
        return std::unexpected{output.error()};
    }
    extraction.outputs.push_back(std::move(*output));
    ++totals.scripts;
    totals.instructions += disassembly->instructions.size();
    totals.texts += static_cast<std::size_t>(std::ranges::count_if(
        disassembly->instructions, [](const formats::script::Instruction& i) { return i.text.has_value(); }));
    totals.unreachable += disassembly->unreachable;
    totals.padding += script.size() - std::min<std::size_t>(disassembly->codeEnd, script.size());
    return {};
}

void noteTotals(Extraction& extraction, const Totals& totals) {
    extraction.notes.push_back(
        std::format("{} scripts, {} instructions, {} texts", totals.scripts, totals.instructions, totals.texts));
    extraction.notes.push_back(std::format("{} instructions are not reached from any section; they are kept in the "
                                           "listings (most are an unreachable jump after an END_SECTION)",
                                           totals.unreachable));
    if (totals.padding != 0) {
        extraction.notes.push_back(std::format("{} bytes after the end of the scripts' code are the padding of their "
                                               "sectors: leftovers that nothing reads",
                                               totals.padding));
    }
}

core::Result<Extraction> extractScriptPack(const Disc& disc, std::span<const std::byte> file) {
    auto slots = formats::script::readScriptTable(file);
    if (!slots) {
        return std::unexpected{slots.error()};
    }
    Extraction extraction;
    Totals totals;
    const std::size_t tableEnd = (slots->size() + 1) * sizeof(std::uint32_t);
    const std::size_t tableBytes = std::min<std::size_t>(formats::script::ScriptTableSize, slots->front().offset);
    if (tableEnd < tableBytes) {
        const auto rest = file.subspan(tableEnd, tableBytes - tableEnd);
        const auto leftovers = std::ranges::count_if(rest, [](std::byte b) { return b != std::byte{0}; });
        if (leftovers != 0) {
            extraction.notes.push_back(std::format("the table holds {} script offsets; {} of the {} bytes after them "
                                                   "up to the first script are leftovers that nothing reads",
                                                   slots->size(), leftovers, rest.size()));
        }
    }
    // initializeScripts loads MAPHEAD.SCN as script 0, so the copy in this file is never read.
    std::optional<std::vector<std::byte>> mapHead;
    if (const formats::disc::IsoFile* mapHeadFile = disc.findFile(MapHeadPath); mapHeadFile != nullptr) {
        auto bytes = formats::disc::readIsoFile(disc.source(), *mapHeadFile);
        if (!bytes) {
            return std::unexpected{bytes.error()};
        }
        mapHead = std::move(*bytes);
    }
    for (std::size_t id = 0; id < slots->size(); ++id) {
        const auto script = file.subspan((*slots)[id].offset, (*slots)[id].size);
        if (id == 0 && mapHead && script.size() >= mapHead->size() &&
            std::ranges::equal(script.first(mapHead->size()), *mapHead)) {
            extraction.notes.push_back(std::format("script 0 is a copy of {}, which the game loads instead; it is "
                                                   "extracted from that file",
                                                   MapHeadPath));
            continue;
        }
        if (auto added = addScript(extraction, totals, ScriptPackPath, id, script); !added) {
            return std::unexpected{added.error()};
        }
    }
    noteTotals(extraction, totals);
    return extraction;
}

} // namespace

bool handlesScriptFile(std::string_view path) {
    return path == MapHeadPath || path == ScriptPackPath;
}

core::Result<Extraction> extractScriptFile(const Disc& disc, const formats::disc::IsoFile& file) {
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    if (file.path == ScriptPackPath) {
        return extractScriptPack(disc, *bytes);
    }
    Extraction extraction;
    Totals totals;
    if (auto added = addScript(extraction, totals, file.path, std::nullopt, *bytes); !added) {
        return std::unexpected{added.error()};
    }
    noteTotals(extraction, totals);
    return extraction;
}

} // namespace dwe::dwx
