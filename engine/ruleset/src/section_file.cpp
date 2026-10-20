#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/ruleset/messages.hpp>
#include <dwe/ruleset/ruleset_values.hpp>
#include <dwe/ruleset/section_file.hpp>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace dwe::ruleset {

namespace {

using core::YamlNode;
using messages::formatMessage;

constexpr char LineEnd = '\n';
constexpr char DoubleQuote = '"';
constexpr char SingleQuote = '\'';
constexpr char Escape = '\\';
constexpr char FolderSeparator = '/';
// The bytes that continue a UTF-8 character: 10xxxxxx.
constexpr unsigned char ContinuationMask = 0xC0;
constexpr unsigned char ContinuationBits = 0x80;

// A scalar of the file to rewrite: where the parser saw it (line and column from 1, in characters) and its new text.
struct ScalarPatch {
    std::string path;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string oldText;
    std::string replacement;
};

// Where a scalar's text is in the file, in bytes.
struct TextSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
};

std::string shownPath(const std::string& path) {
    return path.empty() ? std::string{messages::RootPath} : path;
}

bool sameKeys(const YamlNode& file, const YamlNode& values) {
    return std::ranges::equal(file.entries(), values.entries(), {}, &core::YamlEntry::key, &core::YamlEntry::key);
}

core::Result<void> collectPatches(const YamlNode& file, const YamlNode& values, const std::string& path,
                                  std::vector<ScalarPatch>& patches) {
    if (file.kind() != values.kind()) {
        return core::fail(formatMessage(messages::ShapeChanged, shownPath(path)));
    }
    if (file.isScalar()) {
        if (file.text() == values.text() && file.style() == values.style()) {
            return {};
        }
        auto replacement = inlineYaml(values);
        if (!replacement) {
            return std::unexpected{replacement.error()};
        }
        patches.push_back(ScalarPatch{.path = path,
                                      .line = file.line(),
                                      .column = file.column(),
                                      .oldText = file.text(),
                                      .replacement = std::move(*replacement)});
        return {};
    }
    if (file.isSequence()) {
        if (file.items().size() != values.items().size()) {
            return core::fail(formatMessage(messages::ShapeChanged, shownPath(path)));
        }
        for (std::size_t index = 0; index < file.items().size(); ++index) {
            const std::string itemPath = joinPath(path, std::to_string(index));
            if (auto collected = collectPatches(file.items()[index], values.items()[index], itemPath, patches);
                !collected) {
                return collected;
            }
        }
        return {};
    }
    if (!sameKeys(file, values)) {
        return core::fail(formatMessage(messages::ShapeChanged, shownPath(path)));
    }
    for (std::size_t index = 0; index < file.entries().size(); ++index) {
        const core::YamlEntry& entry = file.entries()[index];
        if (auto collected =
                collectPatches(entry.value, values.entries()[index].value, joinPath(path, entry.key), patches);
            !collected) {
            return collected;
        }
    }
    return {};
}

// The byte where a line starts (from 1), or nothing past the end of the text.
std::optional<std::size_t> lineStart(std::string_view text, std::size_t line) {
    std::size_t offset = 0;
    for (std::size_t current = 1; current < line; ++current) {
        const std::size_t end = text.find(LineEnd, offset);
        if (end == std::string_view::npos) {
            return std::nullopt;
        }
        offset = end + 1;
    }
    return offset;
}

bool continuesCharacter(char byte) {
    return (static_cast<unsigned char>(byte) & ContinuationMask) == ContinuationBits;
}

// The byte of a column (from 1, in characters, as the parser counts it) of the line that starts at `start`.
std::optional<std::size_t> columnByte(std::string_view text, std::size_t start, std::size_t column) {
    std::size_t offset = start;
    for (std::size_t current = 1; current < column; ++current) {
        if (offset >= text.size() || text[offset] == LineEnd) {
            return std::nullopt;
        }
        ++offset;
        while (offset < text.size() && continuesCharacter(text[offset])) {
            ++offset;
        }
    }
    return offset;
}

// The end of a quoted scalar that starts at `begin` with its quote, if it closes on the same line.
std::optional<std::size_t> quotedEnd(std::string_view line, std::size_t begin) {
    const char quote = line[begin];
    for (std::size_t at = begin + 1; at < line.size(); ++at) {
        const bool escaped = quote == DoubleQuote && line[at] == Escape;
        const bool doubledQuote =
            quote == SingleQuote && line[at] == SingleQuote && at + 1 < line.size() && line[at + 1] == SingleQuote;
        if (escaped || doubledQuote) {
            ++at;
        } else if (line[at] == quote) {
            return at + 1;
        }
    }
    return std::nullopt;
}

// Where the text of a scalar is in the file: from its quote to the closing one, or its plain text, on one line.
core::Result<TextSpan> findScalar(std::string_view text, const ScalarPatch& patch) {
    const auto notInPlace = [&patch] {
        return core::fail(formatMessage(messages::NotInPlace, shownPath(patch.path), patch.line));
    };
    const std::optional<std::size_t> start = lineStart(text, patch.line);
    if (!start) {
        return notInPlace();
    }
    const std::optional<std::size_t> begin = columnByte(text, *start, patch.column);
    if (!begin || *begin >= text.size()) {
        return notInPlace();
    }
    const std::size_t lineEnd = std::min(text.find(LineEnd, *begin), text.size());
    const std::string_view line = text.substr(0, lineEnd);
    if (line[*begin] == DoubleQuote || line[*begin] == SingleQuote) {
        const std::optional<std::size_t> end = quotedEnd(line, *begin);
        if (!end) {
            return notInPlace();
        }
        return TextSpan{.begin = *begin, .end = *end};
    }
    if (line.substr(*begin, patch.oldText.size()) != patch.oldText) {
        return notInPlace();
    }
    return TextSpan{.begin = *begin, .end = *begin + patch.oldText.size()};
}

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

core::Result<assets::AssetId> sectionId(const Section& section) {
    std::string path{RulesFolder};
    path += FolderSeparator;
    path += section.owner;
    path += FolderSeparator;
    path += section.name;
    return assets::AssetId::parse(path);
}

core::Result<void> writeText(const std::filesystem::path& file, std::string_view text) {
    std::ofstream stream{file, std::ios::binary | std::ios::trunc};
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        return core::fail(formatMessage(messages::WriteFailed, assets::pathText(file)));
    }
    return {};
}

} // namespace

core::Result<std::string> patchSectionText(std::string_view fileText, const YamlNode& values) {
    auto file = core::parseYaml(fileText);
    if (!file) {
        return std::unexpected{file.error()};
    }
    std::vector<ScalarPatch> patches;
    if (auto collected = collectPatches(*file, values, {}, patches); !collected) {
        return std::unexpected{collected.error()};
    }
    std::vector<std::pair<TextSpan, std::string>> edits;
    for (ScalarPatch& patch : patches) {
        auto span = findScalar(fileText, patch);
        if (!span) {
            return std::unexpected{span.error()};
        }
        edits.emplace_back(*span, std::move(patch.replacement));
    }
    // From the end of the text back, so the spans before each edit stay where they are.
    std::ranges::sort(edits, std::ranges::greater{}, [](const auto& edit) { return edit.first.begin; });
    std::string patched{fileText};
    for (const auto& [span, replacement] : edits) {
        patched.replace(span.begin, span.end - span.begin, replacement);
    }
    return patched;
}

core::Result<std::filesystem::path> saveSection(const assets::VirtualFileSystem& vfs, const Section& section) {
    auto id = sectionId(section);
    if (!id) {
        return std::unexpected{id.error()};
    }
    const assets::AssetSource* source = vfs.resolve(*id);
    if (source == nullptr) {
        return core::fail(formatMessage(messages::NotInPacks, messages::SectionText, id->str()));
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(source->file, error)) {
        return core::fail(formatMessage(messages::NotOnDisk, assets::pathText(source->file)));
    }
    auto bytes = vfs.read(*id);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    auto patched = patchSectionText(textOf(*bytes), section.node);
    if (!patched) {
        return core::fail(formatMessage(messages::InFile, assets::pathText(source->file), patched.error().message));
    }
    if (auto written = writeText(source->file, *patched); !written) {
        return std::unexpected{written.error()};
    }
    return source->file;
}

} // namespace dwe::ruleset
