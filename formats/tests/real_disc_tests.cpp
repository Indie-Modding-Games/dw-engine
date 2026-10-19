// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has one.

#include <dwe/core/sha1.hpp>
#include <dwe/formats/disc/boot.hpp>
#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/file_table.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/known_releases.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

using namespace dwe::formats::disc;

namespace {

// The message of a failed result. doctest builds a message even when the check passes, so it cannot call error().
template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

} // namespace

TEST_CASE("the user's disc: the executable's file table matches the volume") {
    const char* disc = std::getenv("DWE_DISC");
    if (disc == nullptr || std::string_view{disc}.empty()) {
        MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
        return;
    }
    auto image = DiscImage::open(disc);
    REQUIRE_MESSAGE(image.has_value(), errorOf(image));
    const auto volume = readIsoVolume(*image);
    REQUIRE_MESSAGE(volume.has_value(), errorOf(volume));
    CHECK(volume->files.size() > 500);

    const auto executable = findBootExecutable(*image, *volume);
    REQUIRE_MESSAGE(executable.has_value(), errorOf(executable));
    const auto bytes = readIsoFile(*image, *executable);
    REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
    const auto release = findKnownRelease(dwe::core::sha1(*bytes));
    if (!release) {
        FAIL("the executable is not a known release");
        return;
    }
    CHECK(release->file == ReleaseFile::Executable);
    MESSAGE("version ", release->version, ", executable ", executable->path, ", ", volume->files.size(), " files");

    const auto location = findFileTableLocation(release->version);
    if (!location) {
        FAIL("no file table location for version ", release->version);
        return;
    }
    const auto table = readFileTable(*bytes, *location);
    REQUIRE_MESSAGE(table.has_value(), errorOf(table));
    CHECK(table->size() + 1 == location->capacity);

    const auto check = checkFileTable(*table, volume->files);
    for (const auto& mismatch : check.mismatches) {
        MESSAGE(mismatch);
    }
    CHECK(check.mismatches.empty());
    for (const auto& entry : check.tableOnly) {
        MESSAGE("in the table only: ", entry.filename, " at sector ", entry.sector);
        CHECK(std::cmp_less_equal(entry.sector + 1, image->sectorCount()));
    }
    CHECK(static_cast<std::size_t>(std::ranges::count(check.inTable, true)) + check.tableOnly.size() == table->size());
}
