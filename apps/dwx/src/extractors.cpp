#include <array>

#include "audio_extractors.hpp"
#include "extractor.hpp"
#include "image_extractors.hpp"
#include "map_extractors.hpp"
#include "model_extractors.hpp"
#include "script_extractors.hpp"
#include "table_extractors.hpp"

namespace dwe::dwx {

namespace {

// Every extractor, most specific first: CHDAT/ALLTIM.TIM is a pack before it is a .TIM file.
constexpr std::array Registry = {
    Extractor{.name = "tim_pack", .handles = handlesTimPack, .extract = extractTimPack},
    Extractor{.name = "stacked_tim", .handles = handlesStackedTims, .extract = extractStackedTims},
    Extractor{.name = "tim_file", .handles = handlesTimFile, .extract = extractTimFile},
    Extractor{.name = "executable", .handles = handlesExecutable, .extract = extractExecutable},
    Extractor{.name = "boot_config", .handles = handlesBootConfig, .extract = extractBootConfig},
    Extractor{.name = "map", .handles = handlesMapFile, .extract = extractMapFile},
    Extractor{.name = "map_background", .handles = handlesMapBackground, .extract = extractMapBackground},
    Extractor{.name = "arena_collision", .handles = handlesArenaCollision, .extract = extractArenaCollision},
    Extractor{.name = "digimon_model", .handles = handlesDigimonModel, .extract = extractDigimonModel},
    Extractor{.name = "static_model", .handles = handlesStaticModel, .extract = extractStaticModel},
    Extractor{.name = "sound_bank", .handles = handlesSoundFile, .extract = extractSoundFile},
    Extractor{.name = "event_script", .handles = handlesScriptFile, .extract = extractScriptFile},
};

} // namespace

std::string outputPath(const OutputFile& output) {
    return output.id.str() + "." + output.extension;
}

std::span<const Extractor> extractors() {
    return Registry;
}

const Extractor* findExtractor(std::span<const Extractor> registry, std::string_view path) {
    for (const auto& extractor : registry) {
        if (extractor.handles(path)) {
            return &extractor;
        }
    }
    return nullptr;
}

} // namespace dwe::dwx
