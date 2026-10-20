// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has one.

#include <dwe/core/sha1.hpp>
#include <dwe/formats/disc/boot.hpp>
#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/known_releases.hpp>
#include <dwe/formats/exe/executable_layout.hpp>
#include <dwe/formats/exe/item_effects.hpp>
#include <dwe/formats/exe/memory_image.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdlib>
#include <ostream>
#include <string>
#include <string_view>

using namespace dwe::formats;
using exe::ItemHandler;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// The items whose case does more than set values (item.c: handleChips): the auto pilot runs a script section and the
// porta potty resets the toilet timer.
constexpr std::size_t AutoPilotChip = 0x16;
constexpr std::size_t PortaPotty = 0x20;

} // namespace

TEST_CASE("the user's disc: every item has a known handler and every food and chip case but two is decoded") {
    const char* path = std::getenv("DWE_DISC");
    if (path == nullptr || std::string_view{path}.empty()) {
        MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
        return;
    }
    auto image = disc::DiscImage::open(path);
    REQUIRE_MESSAGE(image.has_value(), errorOf(image));
    const auto volume = disc::readIsoVolume(*image);
    REQUIRE_MESSAGE(volume.has_value(), errorOf(volume));
    const auto executable = disc::findBootExecutable(*image, *volume);
    REQUIRE_MESSAGE(executable.has_value(), errorOf(executable));
    const auto bytes = disc::readIsoFile(*image, *executable);
    REQUIRE_MESSAGE(bytes.has_value(), errorOf(bytes));
    const auto release = disc::findKnownRelease(dwe::core::sha1(*bytes));
    if (!release) {
        FAIL("the executable is not a known release");
        return;
    }
    const auto layout = exe::findExecutableLayout(release->version);
    if (!layout) {
        MESSAGE("no executable layout for version ", release->version, ": skipping");
        return;
    }
    const auto memory = exe::MemoryImage::fromExecutable(*bytes);
    REQUIRE_MESSAGE(memory.has_value(), errorOf(memory));
    const auto effects = exe::readItemEffects(*memory, *layout);
    REQUIRE_MESSAGE(effects.has_value(), errorOf(effects));
    CHECK(effects->size() == layout->itemFunctions.count);

    std::size_t food = 0;
    std::size_t chips = 0;
    std::size_t decoded = 0;
    for (std::size_t item = 0; item < effects->size(); ++item) {
        const exe::ItemEffectCode& effect = (*effects)[item];
        if (effect.handler == ItemHandler::Food) {
            ++food;
            CHECK_MESSAGE(effect.values.size() == exe::FoodValueCount, "food ", item, ": ", effect.skipped);
        } else if (effect.handler == ItemHandler::Chip) {
            ++chips;
            if (item == AutoPilotChip || item == PortaPotty) {
                CHECK_MESSAGE(effect.values.empty(), "chip ", item, " was expected to be skipped");
            } else {
                CHECK_MESSAGE(effect.values.size() == exe::ChipValueCount, "chip ", item, ": ", effect.skipped);
            }
        } else if (effect.handler == ItemHandler::HpHealing || effect.handler == ItemHandler::MpHealing) {
            CHECK_MESSAGE(effect.healing.has_value(), "healing item ", item);
        } else if (effect.handler == ItemHandler::Evolution) {
            // The last evolution items lead to fixed Digimon in code (item.c: handleEvoItems), past the table's end.
            const bool inTable = item - layout->itemCode.firstEvolutionItem < layout->evolutionItemTargets.count;
            CHECK_MESSAGE(effect.evolutionTarget.has_value() == inTable, "evolution item ", item);
        }
        if (!effect.values.empty()) {
            ++decoded;
        }
    }
    MESSAGE(effects->size(), " items: ", food, " food, ", chips, " chips, ", decoded, " cases decoded");
    CHECK(food > 0);
    CHECK(chips > 0);
    // Every food case and every chip case but the two that do more than set values.
    CHECK(decoded + 2 == food + chips);
}
