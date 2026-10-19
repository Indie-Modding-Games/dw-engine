#include "padding_extractor.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace dwe::dwx {

namespace {

constexpr std::array PaddingFiles = {std::string_view{"MMM.DAT"}};
// How many ranges a report lists before it only counts the rest.
constexpr std::size_t ListedRanges = 8;

} // namespace

std::vector<ByteRange> nonZeroRanges(std::span<const std::byte> bytes) {
    std::vector<ByteRange> ranges;
    const auto isZero = [](std::byte value) { return value == std::byte{0}; };
    auto at = bytes.begin();
    while (at != bytes.end()) {
        const auto begin = std::find_if_not(at, bytes.end(), isZero);
        if (begin == bytes.end()) {
            break;
        }
        const auto end = std::find_if(begin, bytes.end(), isZero);
        ranges.push_back({.begin = static_cast<std::size_t>(begin - bytes.begin()),
                          .end = static_cast<std::size_t>(end - bytes.begin())});
        at = end;
    }
    return ranges;
}

std::string describeRanges(std::span<const ByteRange> ranges) {
    std::string text;
    for (std::size_t i = 0; i < ranges.size() && i < ListedRanges; ++i) {
        text += std::format("{}0x{:x}-0x{:x}", i == 0 ? "" : ", ", ranges[i].begin, ranges[i].end);
    }
    if (ranges.size() > ListedRanges) {
        text += std::format(" and {} more", ranges.size() - ListedRanges);
    }
    return text;
}

bool handlesPadding(std::string_view path) {
    return std::ranges::contains(PaddingFiles, path);
}

core::Result<Extraction> extractPadding(const Disc& disc, const formats::disc::IsoFile& file) {
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    Extraction extraction;
    const auto ranges = nonZeroRanges(*bytes);
    if (ranges.empty()) {
        extraction.notes.emplace_back("padding: all zero; no code opens the file, so it gives no asset");
    } else {
        extraction.unknowns.push_back(
            std::format("the file was expected to be all zero, but these bytes are not: {}", describeRanges(ranges)));
    }
    return extraction;
}

} // namespace dwe::dwx
