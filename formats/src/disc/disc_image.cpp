#include <dwe/formats/disc/cue_sheet.hpp>
#include <dwe/formats/disc/disc_image.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace dwe::formats::disc {

namespace {

// A CUE sheet is a few lines of text; anything much larger is not one.
constexpr std::uintmax_t MaxCueSheetSize = std::uintmax_t{1} << 16U;

bool hasCueExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".cue";
}

core::Result<std::string> readCueSheetText(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return core::fail(std::format("cannot read {}: {}", path.string(), error.message()));
    }
    if (size > MaxCueSheetSize) {
        return core::fail(std::format("{} is too large for a CUE sheet", path.string()));
    }
    std::ifstream file(path, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    if (file.bad()) {
        return core::fail(std::format("cannot read {}", path.string()));
    }
    return text;
}

// Tells a raw image from a cooked one by its first sector: a raw image starts with the sync pattern.
core::Result<SectorLayout> detectLayout(std::ifstream& file, std::uintmax_t size) {
    std::array<std::byte, RawSectorSize> first{};
    file.read(reinterpret_cast<char*>(first.data()), static_cast<std::streamsize>(first.size()));
    file.clear();
    if (size % RawSectorSize == 0 && size >= RawSectorSize && decodeSector(first, SectorLayout::Raw)) {
        return SectorLayout::Raw;
    }
    if (size % Form1DataSize == 0) {
        return SectorLayout::Cooked;
    }
    return core::fail("the file is neither a raw 2352-byte image nor a 2048-byte ISO image");
}

} // namespace

DiscImage::DiscImage(std::filesystem::path dataFile, std::ifstream file, SectorLayout layout, std::uint32_t sectorCount)
    : dataFile_(std::move(dataFile)), file_(std::move(file)), layout_(layout), sectorCount_(sectorCount) {}

core::Result<DiscImage> DiscImage::open(const std::filesystem::path& path) {
    std::filesystem::path dataFile = path;
    std::optional<SectorLayout> layout;
    if (hasCueExtension(path)) {
        const auto text = readCueSheetText(path);
        if (!text) {
            return std::unexpected{text.error()};
        }
        auto sheet = parseCueSheet(*text);
        if (!sheet) {
            return core::fail(std::format("{}: {}", path.string(), sheet.error().message));
        }
        dataFile = path.parent_path() / std::filesystem::path{sheet->dataFile};
        layout = sheet->layout;
    }

    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(dataFile, error);
    if (error) {
        return core::fail(std::format("cannot read {}: {}", dataFile.string(), error.message()));
    }
    std::ifstream file(dataFile, std::ios::binary);
    if (!file) {
        return core::fail(std::format("cannot open {}", dataFile.string()));
    }
    if (!layout) {
        const auto detected = detectLayout(file, size);
        if (!detected) {
            return core::fail(std::format("{}: {}", dataFile.string(), detected.error().message));
        }
        layout = *detected;
    }
    const std::size_t sector = sectorSize(*layout);
    if (size % sector != 0) {
        return core::fail(
            std::format("{} is {} bytes, not a whole number of {}-byte sectors", dataFile.string(), size, sector));
    }
    if (size / sector > std::numeric_limits<std::uint32_t>::max()) {
        return core::fail(std::format("{} is too large for a disc image", dataFile.string()));
    }
    return DiscImage{std::move(dataFile), std::move(file), *layout, static_cast<std::uint32_t>(size / sector)};
}

core::Result<SectorData> DiscImage::readSector(std::uint32_t lba) {
    if (lba >= sectorCount_) {
        return core::fail(std::format("sector {} is past the end of the image ({} sectors)", lba, sectorCount_));
    }
    const std::size_t size = sectorSize(layout_);
    file_.seekg(static_cast<std::streamoff>(lba) * static_cast<std::streamoff>(size));
    file_.read(reinterpret_cast<char*>(buffer_.data()), static_cast<std::streamsize>(size));
    if (!file_ || std::cmp_not_equal(file_.gcount(), size)) {
        file_.clear();
        return core::fail(std::format("cannot read sector {} of {}", lba, dataFile_.string()));
    }
    const auto decoded = decodeSector(std::span{buffer_}.first(size), layout_);
    if (!decoded) {
        return core::fail(std::format("sector {}: {}", lba, decoded.error().message));
    }
    return decoded;
}

} // namespace dwe::formats::disc
