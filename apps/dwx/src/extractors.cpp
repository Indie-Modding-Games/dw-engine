#include <array>

#include "extractor.hpp"

namespace dwe::dwx {

namespace {

// Every extractor, most specific first.
constexpr std::array<Extractor, 0> Registry{};

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
