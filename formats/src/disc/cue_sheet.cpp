#include <dwe/formats/disc/cue_sheet.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <ranges>
#include <vector>

namespace dwe::formats::disc {

namespace {

// Commands that only describe the disc (titles, catalogue numbers) and do not change how it is read.
constexpr std::array IgnoredCommands = {
    std::string_view{"REM"},   std::string_view{"CATALOG"},   std::string_view{"CDTEXTFILE"}, std::string_view{"TITLE"},
    std::string_view{"FLAGS"}, std::string_view{"PERFORMER"}, std::string_view{"SONGWRITER"}, std::string_view{"ISRC"},
};

constexpr std::string_view Utf8ByteOrderMark = "\xEF\xBB\xBF";

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

// Splits a line into words; a word in double quotes may contain spaces.
core::Result<std::vector<std::string_view>> splitWords(std::string_view line) {
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < line.size()) {
        if (isSpace(line[i])) {
            ++i;
            continue;
        }
        if (line[i] == '"') {
            const std::size_t end = line.find('"', i + 1);
            if (end == std::string_view::npos) {
                return core::fail("unterminated quoted name");
            }
            words.push_back(line.substr(i + 1, end - i - 1));
            i = end + 1;
            continue;
        }
        std::size_t end = i;
        while (end < line.size() && !isSpace(line[end])) {
            ++end;
        }
        words.push_back(line.substr(i, end - i));
        i = end;
    }
    return words;
}

std::optional<SectorLayout> layoutOfTrackMode(std::string_view mode) {
    if (mode == "MODE2/2352" || mode == "MODE1/2352") {
        return SectorLayout::Raw;
    }
    if (mode == "MODE1/2048") {
        return SectorLayout::Cooked;
    }
    return std::nullopt;
}

} // namespace

core::Result<CueSheet> parseCueSheet(std::string_view text) {
    if (text.starts_with(Utf8ByteOrderMark)) {
        text.remove_prefix(Utf8ByteOrderMark.size());
    }
    std::optional<std::string> dataFile;
    std::optional<SectorLayout> layout;
    bool hasStart = false;

    int lineNumber = 0;
    for (const auto lineRange : std::views::split(text, '\n')) {
        ++lineNumber;
        const auto words = splitWords(std::string_view{lineRange});
        if (!words) {
            return core::fail(std::format("CUE line {}: {}", lineNumber, words.error().message));
        }
        if (words->empty() || std::ranges::find(IgnoredCommands, words->front()) != IgnoredCommands.end()) {
            continue;
        }
        const std::string_view command = words->front();
        const auto lineError = [lineNumber](std::string_view why) {
            return core::fail(std::format("CUE line {}: {}", lineNumber, why));
        };

        if (command == "FILE") {
            if (dataFile) {
                return lineError("several FILE entries; only single-file images are supported");
            }
            if (words->size() != 3 || (*words)[2] != "BINARY") {
                return lineError("expected FILE \"name\" BINARY");
            }
            dataFile = std::string{(*words)[1]};
        } else if (command == "TRACK") {
            if (!dataFile) {
                return lineError("TRACK before FILE");
            }
            if (layout) {
                return lineError("several tracks; only single-track data discs are supported");
            }
            if (words->size() != 3 || (*words)[1] != "01") {
                return lineError("expected TRACK 01 <mode>");
            }
            layout = layoutOfTrackMode((*words)[2]);
            if (!layout) {
                return lineError(std::format("unsupported track mode {}; expected MODE2/2352, MODE1/2352 or MODE1/2048",
                                             (*words)[2]));
            }
        } else if (command == "INDEX") {
            if (!layout) {
                return lineError("INDEX before TRACK");
            }
            if (words->size() != 3 || (*words)[1] != "01" || (*words)[2] != "00:00:00") {
                return lineError("only INDEX 01 00:00:00 is supported (a data track that starts the file)");
            }
            hasStart = true;
        } else {
            return lineError(std::format("unsupported command {}", command));
        }
    }

    if (!dataFile || !layout || !hasStart) {
        return core::fail("the CUE sheet has no FILE, TRACK and INDEX 01");
    }
    return CueSheet{.dataFile = std::move(*dataFile), .layout = *layout};
}

} // namespace dwe::formats::disc
