#include <dwe/formats/disc/boot.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>

namespace dwe::formats::disc {

namespace {

constexpr std::string_view SystemCnfPath = "SYSTEM.CNF";
// SYSTEM.CNF is a few short lines; anything larger is not one.
constexpr std::uint32_t MaxSystemCnfSize = 2048;

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b,
                              [](unsigned char x, unsigned char y) { return std::toupper(x) == std::toupper(y); });
}

} // namespace

core::Result<std::string> parseBootPath(std::string_view systemCnf) {
    for (const auto lineRange : std::views::split(systemCnf, '\n')) {
        const std::string_view line{lineRange};
        const auto equals = line.find('=');
        if (equals == std::string_view::npos || !equalsIgnoringCase(trim(line.substr(0, equals)), "BOOT")) {
            continue;
        }
        std::string_view path = trim(line.substr(equals + 1));
        if (path.size() >= 6 && equalsIgnoringCase(path.substr(0, 6), "cdrom:")) {
            path.remove_prefix(6);
        }
        path = path.substr(0, path.find(';'));
        while (path.starts_with('\\')) {
            path.remove_prefix(1);
        }
        if (path.empty()) {
            return core::fail("SYSTEM.CNF has an empty BOOT path");
        }
        std::string result{path};
        std::ranges::replace(result, '\\', '/');
        return result;
    }
    return core::fail("SYSTEM.CNF has no BOOT line");
}

core::Result<IsoFile> findBootExecutable(SectorSource& source, const IsoVolume& volume) {
    const auto systemCnf = std::ranges::find(volume.files, SystemCnfPath, &IsoFile::path);
    if (systemCnf == volume.files.end()) {
        return core::fail("the disc has no SYSTEM.CNF");
    }
    if (systemCnf->size > MaxSystemCnfSize) {
        return core::fail("SYSTEM.CNF is too large");
    }
    const auto bytes = readIsoFile(source, *systemCnf);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    const std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    const auto bootPath = parseBootPath(text);
    if (!bootPath) {
        return std::unexpected{bootPath.error()};
    }
    const auto executable = std::ranges::find_if(
        volume.files, [&](const IsoFile& file) { return equalsIgnoringCase(file.path, *bootPath); });
    if (executable == volume.files.end()) {
        return core::fail(std::format("SYSTEM.CNF boots {}, which is not on the disc", *bootPath));
    }
    return *executable;
}

} // namespace dwe::formats::disc
