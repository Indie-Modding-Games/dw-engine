#pragma once

#include <dwe/formats/exe/executable_layout.hpp>
#include <dwe/formats/exe/item_effects.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "mips_code.hpp"

// A synthetic executable: a PS-X EXE header and a text with small, made-up tables at addresses chosen here.
namespace dwe::formats::test {

class ImageBuilder {
public:
    explicit ImageBuilder(std::uint32_t baseAddress) : baseAddress_(baseAddress) {}

    // Reserves size bytes, aligned to 4, and returns their address.
    std::uint32_t place(std::size_t size) {
        const auto address = static_cast<std::uint32_t>(baseAddress_ + text_.size());
        text_.resize(text_.size() + ((size + 3) & ~std::size_t{3}));
        return address;
    }

    void u8(std::uint32_t address, std::uint8_t value) { text_.at(address - baseAddress_) = std::byte{value}; }

    void u16(std::uint32_t address, std::uint16_t value) {
        u8(address, static_cast<std::uint8_t>(value));
        u8(address + 1, static_cast<std::uint8_t>(value >> 8U));
    }

    void u32(std::uint32_t address, std::uint32_t value) {
        u16(address, static_cast<std::uint16_t>(value));
        u16(address + 2, static_cast<std::uint16_t>(value >> 16U));
    }

    void i16(std::uint32_t address, std::int16_t value) { u16(address, static_cast<std::uint16_t>(value)); }

    // Writes the bytes of text without a terminator.
    void bytes(std::uint32_t address, std::string_view text) {
        for (std::size_t i = 0; i < text.size(); ++i) {
            u8(static_cast<std::uint32_t>(address + i), static_cast<std::uint8_t>(text[i]));
        }
    }

    // Places a NUL-terminated string and returns its address.
    std::uint32_t string(std::string_view text) {
        const std::uint32_t address = place(text.size() + 1);
        bytes(address, text);
        return address;
    }

    // Places a table of pointers to new strings and returns its address.
    std::uint32_t stringTable(std::initializer_list<std::string_view> texts) {
        std::vector<std::uint32_t> pointers;
        for (const std::string_view text : texts) {
            pointers.push_back(string(text));
        }
        const std::uint32_t table = place(pointers.size() * 4);
        for (std::size_t i = 0; i < pointers.size(); ++i) {
            u32(static_cast<std::uint32_t>(table + (i * 4)), pointers[i]);
        }
        return table;
    }

    [[nodiscard]] std::uint32_t baseAddress() const { return baseAddress_; }
    [[nodiscard]] const std::vector<std::byte>& text() const { return text_; }

    // The whole executable file: the 0x800-byte header and the text.
    [[nodiscard]] std::vector<std::byte> executable() const {
        std::vector<std::byte> file(0x800);
        constexpr std::string_view Magic = "PS-X EXE";
        for (std::size_t i = 0; i < Magic.size(); ++i) {
            file[i] = static_cast<std::byte>(Magic[i]);
        }
        const auto put = [&](std::size_t offset, std::uint32_t value) {
            for (std::size_t i = 0; i < 4; ++i) {
                file[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
            }
        };
        put(0x10, baseAddress_);
        put(0x18, baseAddress_);
        put(0x1C, static_cast<std::uint32_t>(text_.size()));
        file.insert(file.end(), text_.begin(), text_.end());
        return file;
    }

private:
    std::uint32_t baseAddress_;
    std::vector<std::byte> text_;
};

// handleFood of the fixture: item 0 gets 12 energy, any other item none. Every other local is the zero register.
inline constexpr std::array<exe::ValueLocation, exe::FoodValueCount> FixtureFoodValues = [] {
    std::array<exe::ValueLocation, exe::FoodValueCount> values{};
    values.fill(exe::ValueLocation::inRegister(exe::MipsRegister::Zero));
    values[0] = exe::ValueLocation::inRegister(exe::MipsRegister::V0);
    return values;
}();

struct ExecutableFixture {
    std::vector<std::byte> file;
    exe::ExecutableLayout layout;
};

// Builds an executable where every table of the layout has two or three made-up records.
inline ExecutableFixture makeExecutableFixture() {
    constexpr std::uint32_t Base = 0x80010000;
    ImageBuilder image{Base};
    exe::ExecutableLayout layout;
    layout.version = "test";
    layout.inlineNames = true;

    // Three Digimon: 20-byte name, then the 0x20 bytes of DigimonPara.
    layout.digimonData = {.address = image.place(156), .count = 3};
    const std::array<std::string_view, 3> names = {"Testmon", "Testmon", "Other mon"};
    for (std::uint32_t i = 0; i < 3; ++i) {
        const std::uint32_t record = layout.digimonData.address + (i * 0x34);
        image.bytes(record, names[i]);
        image.u32(record + 0x14, i + 1);            // boneCount
        image.i16(record + 0x18, -5);               // radius
        image.i16(record + 0x1A, 300);              // height
        image.u8(record + 0x1C, 2);                 // type
        image.u8(record + 0x1D, 3);                 // level
        image.u8(record + 0x1E, 1);                 // special[0]
        image.u8(record + 0x1F, 0xFF);              // special[1]
        image.u8(record + 0x20, 0xFF);              // special[2]
        image.u8(record + 0x21, i == 0 ? 1 : 0xFF); // dropItem
        image.u8(record + 0x22, 50);                // dropChance
        for (std::uint32_t slot = 0; slot < 16; ++slot) {
            image.u8(record + 0x23 + slot, slot == 0 ? 1 : 0xFF);
        }
    }
    layout.digimonFileNames = {.address = image.stringTable({"TSTA", "TSTB", "OTHR"}), .count = 3};
    layout.digimonSkeletons = {.address = image.place(12), .count = 3};
    for (std::uint32_t i = 0; i < 3; ++i) {
        const std::uint32_t bones = image.place(std::size_t{i + 1} * 2);
        for (std::uint32_t bone = 0; bone <= i; ++bone) {
            image.u8(bones + (bone * 2), static_cast<std::uint8_t>(bone));
            image.u8(bones + (bone * 2) + 1, static_cast<std::uint8_t>(bone == 0 ? 0xFF : bone - 1));
        }
        image.u32(layout.digimonSkeletons.address + (i * 4), bones);
    }

    layout.moveData = {.address = image.place(32), .count = 2};
    image.u32(layout.moveData.address, 1000);
    image.i16(layout.moveData.address + 4, 120);
    image.u8(layout.moveData.address + 6, 30);
    image.u8(layout.moveData.address + 0xF, 7);
    layout.moveNames = {.address = image.stringTable({"Spit Fire", "Spit Fire"}), .count = 2};
    layout.moveLearnChances = {.address = image.place(3), .count = 1};
    image.u8(layout.moveLearnChances.address, 25);
    image.u8(layout.moveLearnChances.address + 1, 16);
    image.u8(layout.moveLearnChances.address + 2, 11);

    layout.itemPara = {.address = image.place(64), .count = 2};
    image.bytes(layout.itemPara.address, "Meat");
    image.bytes(layout.itemPara.address + 32, "Odd\x82\xA0\\");
    image.u32(layout.itemPara.address + 0x14, 50);
    image.u16(layout.itemPara.address + 0x18, 2);
    image.i16(layout.itemPara.address + 0x1A, -1);
    image.u16(layout.itemPara.address + 0x1E, 0xBEEF);
    layout.itemDescriptions = {.address = image.stringTable({"Food.", "Strange."}), .count = 2};

    layout.evolutionRequirements = {.address = image.place(56), .count = 2};
    image.i16(layout.evolutionRequirements.address, -1);
    image.i16(layout.evolutionRequirements.address + 28, 2);
    image.i16(layout.evolutionRequirements.address + 28 + 2, 1000);
    image.u8(layout.evolutionRequirements.address + 28 + 0x1A, 0x11);
    layout.evolutionStatsGains = {.address = image.place(28), .count = 2};
    image.i16(layout.evolutionStatsGains.address + 12, 1);
    image.i16(layout.evolutionStatsGains.address + 14 + 12, 77); // outside DIGIMON_DATA
    layout.evolutionPaths = {.address = image.place(22), .count = 2};
    for (std::uint32_t i = 0; i < 22; ++i) {
        image.u8(layout.evolutionPaths.address + i, 0xFF);
    }
    image.u8(layout.evolutionPaths.address + 5, 2); // Digimon 1 evolves to 2

    layout.raiseData = {.address = image.place(28), .count = 1};
    image.u8(layout.raiseData.address, 6);
    image.u8(layout.raiseData.address + 0xA, 9);     // energyUsage
    image.i16(layout.raiseData.address + 0xC, 400);  // poopTimer
    image.u8(layout.raiseData.address + 0x11, 0);    // favoriteFood
    image.i16(layout.raiseData.address + 0x1A, -20); // viewZ

    layout.cardData = {.address = image.place(8), .count = 2};
    image.u8(layout.cardData.address, 2);
    image.i16(layout.cardData.address + 2, 300);
    layout.cardPrices = {.address = image.place(20), .count = 5};
    image.u32(layout.cardPrices.address, 100);

    layout.tournamentNames = {.address = image.stringTable({"Fire Cup", "Wind Cup"}), .count = 2};
    layout.tournamentGrades = {.address = image.stringTable({"C", "B"}), .count = 2};
    layout.tournamentSchedule = {.address = image.place(12), .count = 2};
    for (std::uint32_t i = 0; i < 12; ++i) {
        image.u8(layout.tournamentSchedule.address + i, 0xFF);
    }
    image.u8(layout.tournamentSchedule.address, 1);
    image.u8(layout.tournamentSchedule.address + 1, 0);
    layout.tournamentMedalIds = {.address = image.place(4), .count = 2};
    layout.medalNames = {.address = image.stringTable({"First Medal"}), .count = 1};
    layout.medalDescriptions = {.address = image.stringTable({"Line one", "", "Line three"}), .count = 3};
    layout.bgmTrackNames = {.address = image.stringTable({"Town Theme"}), .count = 1};
    layout.enemyMusicMapping = {.address = image.place(4), .count = 4};
    layout.shopMaps = {.address = image.place(4), .count = 2};
    image.i16(layout.shopMaps.address, 1);
    image.i16(layout.shopMaps.address + 2, -1);
    layout.shopkeepDigimonTypes = {.address = image.place(3), .count = 3};
    image.u8(layout.shopkeepDigimonTypes.address, 2);
    image.u8(layout.shopkeepDigimonTypes.address + 1, 0xFF);

    layout.mapEntries = {.address = image.place(32), .count = 2};
    image.bytes(layout.mapEntries.address, "START");
    image.u8(layout.mapEntries.address + 0xA, 1);
    image.u8(layout.mapEntries.address + 0xC, 0xC3); // NPCs, fixed time, sound 3
    image.u8(layout.mapEntries.address + 0xD, 1);
    layout.mapNames = {.address = image.stringTable({"Somewhere"}), .count = 1};
    layout.doorMapData = {.address = image.place(54), .count = 1};
    for (std::uint32_t slot = 0; slot < 6; ++slot) {
        image.u8(layout.doorMapData.address + slot, slot == 0 ? 3 : 0xFF);
    }
    image.i16(layout.doorMapData.address + 6, -100);
    layout.toiletData = {.address = image.place(8), .count = 1};
    image.i16(layout.toiletData.address, 10);
    layout.mapLightUpdateData = {.address = image.place(8), .count = 1};
    image.u16(layout.mapLightUpdateData.address + 6, 0xFFFF);

    layout.digimonVlallSoundIds = {.address = image.place(3), .count = 3};
    image.u8(layout.digimonVlallSoundIds.address + 1, 4);
    layout.digimonVballSoundIds = {.address = image.place(3), .count = 3};
    image.u8(layout.digimonVballSoundIds.address + 2, 9);
    layout.mapSoundPara = {.address = image.place(8), .count = 2};
    image.u16(layout.mapSoundPara.address + 4, 6);
    image.u16(layout.mapSoundPara.address + 6, 10);
    layout.transportTargets = {.address = image.place(16), .count = 2};
    image.u8(layout.transportTargets.address, 1);
    image.u8(layout.transportTargets.address + 1, 3);
    image.u16(layout.transportTargets.address + 2, 0x51);
    image.u32(layout.transportTargets.address + 4, 500);
    image.u8(layout.transportTargets.address + 8, 2);
    image.u16(layout.transportTargets.address + 10, 0x52);
    image.u32(layout.transportTargets.address + 12, 1000);
    layout.jukeboxTracks = {.address = image.place(4), .count = 2};
    image.u8(layout.jukeboxTracks.address, 0x1D);
    image.u8(layout.jukeboxTracks.address + 3, 2);

    // Item 0 is food, item 1 heals HP; the HP handler's code is never run, so any address names it.
    using namespace mips;
    const std::array foodCode{li(V0, 0), bne(A0, Zero, 2), Nop, li(V0, 12), jr(Ra), Nop};
    const std::uint32_t food = placeCode(image, std::span<const std::uint32_t>{foodCode});
    const std::uint32_t hpHealing = image.place(4);
    layout.itemFunctions = {.address = image.place(8), .count = 2};
    image.u32(layout.itemFunctions.address, food);
    image.u32(layout.itemFunctions.address + 4, hpHealing);
    layout.healingValues = {.address = image.place(8), .count = 4};
    image.i16(layout.healingValues.address + 2, 500);
    layout.evolutionItemTargets = {.address = image.place(1), .count = 1};
    layout.itemCode = {.handlers = {.hpHealing = hpHealing, .food = food},
                       .firstHpHealingItem = 0,
                       .firstMpHealingItem = 0,
                       .firstEvolutionItem = 2,
                       .food = {.function = food, .join = food + 16, .values = FixtureFoodValues},
                       .chips = {}};

    return {.file = image.executable(), .layout = layout};
}

} // namespace dwe::formats::test
