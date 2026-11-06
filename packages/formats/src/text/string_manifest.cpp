#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/text/string_manifest.hpp>

#include <algorithm>
#include <array>
#include <format>

namespace dwe::formats::text {

namespace {

// The longest string the reader accepts; the longest in the US data has under 200 bytes.
constexpr std::size_t MaxStringLength = 1024;

constexpr StringBlock pointers(std::string_view source, std::string_view name,
                               std::uint32_t address, std::uint32_t count) {
  return StringBlock{.source = source,
                     .name = name,
                     .storage = StringStorage::Pointers,
                     .address = address,
                     .count = count,
                     .stride = 0,
                     .length = 0,
                     .addresses = {}};
}

constexpr StringBlock records(std::string_view source, std::string_view name, std::uint32_t address,
                              std::uint32_t count, std::uint32_t stride, std::uint32_t length) {
  return StringBlock{.source = source,
                     .name = name,
                     .storage = StringStorage::Records,
                     .address = address,
                     .count = count,
                     .stride = stride,
                     .length = length,
                     .addresses = {}};
}

constexpr StringBlock embedded(std::string_view source, std::string_view name,
                               std::uint32_t address, std::uint32_t count, std::uint32_t stride) {
  return StringBlock{.source = source,
                     .name = name,
                     .storage = StringStorage::Embedded,
                     .address = address,
                     .count = count,
                     .stride = stride,
                     .length = stride,
                     .addresses = {}};
}

constexpr StringBlock named(std::string_view source, std::string_view name,
                            std::span<const std::uint32_t> addresses) {
  return StringBlock{.source = source,
                     .name = name,
                     .storage = StringStorage::Named,
                     .count = static_cast<std::uint32_t>(addresses.size()),
                     .addresses = addresses};
}

// US (SLUS_010.32). Tables from config/us/symbols*.txt and their declarations; named strings from
// the text blocks in text/<dir>/<name>.yaml, without the ones a table above already points to. A
// named string without a symbol of its own follows the one before it in its block, 4-byte aligned,
// as the compiler lays them out.
constexpr std::array UsOverlays = {
    OverlayLoad{.file = "BTL_REL.BIN", .loadAddress = 0x80052AE0},
    OverlayLoad{.file = "STD_REL.BIN", .loadAddress = 0x80052AE0},
    OverlayLoad{.file = "VS_REL.BIN", .loadAddress = 0x80052AE0},
    OverlayLoad{.file = "KAR_REL.BIN", .loadAddress = 0x80053800},
    OverlayLoad{.file = "FISH_REL.BIN", .loadAddress = 0x80070000},
    OverlayLoad{.file = "TRN_REL.BIN", .loadAddress = 0x80088800},
};

constexpr std::array<std::uint32_t, 11> BtlBattleEffectMessages = {
    0x80072EF8, 0x80072F18, 0x80072F2C, 0x80072F44, 0x80072F58, 0x80072F70,
    0x80072F90, 0x80072FA4, 0x80072FBC, 0x80072FD8, 0x80072FE4};
constexpr std::array<std::uint32_t, 19> FishMessages = {
    0x8007A5F4, 0x8007A608, 0x8007A614, 0x8007A628, 0x8007A634, 0x801349D8, 0x8007A650,
    0x8007A688, 0x8007A69C, 0x8007A6A8, 0x8007A6C4, 0x8007A6D8, 0x8007A6F8, 0x8007A70C,
    0x8007A720, 0x8007A73C, 0x8007A758, 0x8007A774, 0x8007A784};
constexpr std::array<std::uint32_t, 1> KarMenu = {0x8005ABB8};
constexpr std::array<std::uint32_t, 1> BattleUiBitsLabels = {0x801344D4};
constexpr std::array<std::uint32_t, 1> GameMenuYearDay = {0x80134298};
constexpr std::array<std::uint32_t, 11> GameMenuLevelTitles = {
    0x801342A8, 0x801342B0, 0x80123DE8, 0x80123DF4, 0x80123E04, 0x801342B8,
    0x80123E10, 0x801342C0, 0x801342C8, 0x801342D0, 0x801342D8};
constexpr std::array<std::uint32_t, 3> InventorySortLabels = {0x801343BC, 0x801343C4, 0x801343CC};
constexpr std::array<std::uint32_t, 3> InventoryConfirmLabels = {0x8012A9B4, 0x801343D4,
                                                                 0x801343D8};
constexpr std::array<std::uint32_t, 1> ItemStrIsSick = {0x80125F64};
constexpr std::array<std::uint32_t, 16> MainMenuSaveLabels = {
    0x80134662, 0x80134664, 0x8013466C, 0x80134674, 0x80134678, 0x8013467C, 0x80134680, 0x80134684,
    0x8013468C, 0x80134694, 0x80134698, 0x8013469C, 0x801346A4, 0x801346AC, 0x801346B0, 0x801346B4};
constexpr std::array<std::uint32_t, 1> MainMenuSavePlayerLabel = {0x801346C4};
constexpr std::array<std::uint32_t, 26> MainMenuMemcardMessages = {
    0x80131008, 0x80131014, 0x80131024, 0x80131030, 0x8013103C, 0x8013104C, 0x8013105C,
    0x8013106C, 0x8013107C, 0x80131090, 0x801310A4, 0x801311D4, 0x801311EC, 0x80131200,
    0x80131218, 0x80131228, 0x80131234, 0x80131240, 0x80131254, 0x80131264, 0x80131278,
    0x80131290, 0x801312A0, 0x801312B8, 0x801312D0, 0x801312E4};
constexpr std::array<std::uint32_t, 32> MainMenuSaveMessages = {
    0x801312FC, 0x80131318, 0x80131328, 0x80131340, 0x8013134C, 0x8013136C, 0x80131378, 0x80131390,
    0x801313B0, 0x801313D0, 0x801313E4, 0x80131400, 0x8013141C, 0x8013143C, 0x80131448, 0x80131454,
    0x80131460, 0x8013147C, 0x80131498, 0x801314B0, 0x801314C8, 0x801314E4, 0x80131500, 0x8013151C,
    0x80131538, 0x8013154C, 0x80131568, 0x80131580, 0x801315A0, 0x801315C0, 0x801315D0, 0x801315EC};
constexpr std::array<std::uint32_t, 1> PartnerImplIsSick = {0x801225A0};
constexpr std::array<std::uint32_t, 8> TamerMessages = {
    0x801341FC, 0x80122D68, 0x80122D80, 0x80122D94, 0x80122DAC, 0x80122DC8, 0x80122DDC, 0x80122DF4};
constexpr std::array<std::uint32_t, 1> Utils2Pause = {0x80134430};
constexpr std::array<std::uint32_t, 1> ScriptMenuHiraganaKaRow = {0x8013045C};
constexpr std::array<std::uint32_t, 6> ScriptMenuLowercaseRows = {
    0x801306F0, 0x801306FC, 0x80130708, 0x80130714, 0x80130720, 0x8013072C};
constexpr std::array<std::uint32_t, 1> ScriptMenuTradeLabel = {0x801307CC};
constexpr std::array<std::uint32_t, 3> ScriptMenuNamingButtons = {0x801345F4, 0x801345F8,
                                                                  0x80134600};
constexpr std::array<std::uint32_t, 7> TrnRewardMessages = {
    0x8008F0D8, 0x8008F0F0, 0x8008F100, 0x8008F10C, 0x8008F11C, 0x8008F12C, 0x8008F140};
constexpr std::array<std::uint32_t, 1> TrnRewardMastered = {0x8008F19C};
constexpr std::array<std::uint32_t, 2> Singles = {0x8012F458, 0x8012F584};

constexpr std::array UsBlocks = {
    pointers("", "move_names", 0x80126054, 122),
    records("", "digimon_names", 0x8012CEB4, 180, 0x34, 20),
    records("", "item_names", 0x801269DC, 128, 0x20, 20),
    embedded("", "item_menu_labels", 0x8012A9D8, 4, 8),
    embedded("", "stat_labels", 0x80124C0C, 6, 12),
    pointers("", "item_descs", 0x801279DC, 128),
    pointers("", "map_names", 0x801291BC, 70),
    pointers("", "bgm_track_names", 0x8012FEC8, 63),
    pointers("", "tournament_names", 0x801300E0, 23),
    pointers("", "tournament_grades", 0x8013013C, 23),
    pointers("", "special_speakers", 0x8013035C, 6),
    pointers("", "medal_names", 0x80124828, 15),
    pointers("", "medal_descriptions", 0x80124864, 45),
    pointers("", "status_view_labels", 0x801247B8, 8),
    pointers("", "tech_view_labels", 0x801247D8, 10),
    pointers("", "player_view_labels", 0x80124800, 5),
    pointers("", "card_chart_labels", 0x80124814, 5),
    pointers("", "memory_card_operation_messages", 0x80131898, 6),
    pointers("", "memory_card_error_messages", 0x801318B0, 11),
    pointers("BTL_REL.BIN", "shouts", 0x80072DE8, 7),
    pointers("BTL_REL.BIN", "command_names", 0x80072E34, 8),
    pointers("STD_REL.BIN", "command_names", 0x8007A688, 8),
    pointers("VS_REL.BIN", "command_names", 0x80070744, 8),
    pointers("KAR_REL.BIN", "penguinmon_dialogue", 0x8005AF58, 61),
    pointers("KAR_REL.BIN", "metalmamemon_dialogue", 0x8005B318, 61),
    named("BTL_REL.BIN", "battle_effect_messages", BtlBattleEffectMessages),
    named("FISH_REL.BIN", "messages", FishMessages),
    named("KAR_REL.BIN", "menu", KarMenu),
    named("", "battle_ui_bits_labels", BattleUiBitsLabels),
    named("", "game_menu_year_day", GameMenuYearDay),
    named("", "game_menu_level_titles", GameMenuLevelTitles),
    named("", "inventory_sort_labels", InventorySortLabels),
    named("", "inventory_confirm_labels", InventoryConfirmLabels),
    named("", "item_str_is_sick", ItemStrIsSick),
    named("", "main_menu_save_labels", MainMenuSaveLabels),
    named("", "main_menu_save_player_label", MainMenuSavePlayerLabel),
    named("", "main_menu_memcard_messages", MainMenuMemcardMessages),
    named("", "main_menu_save_messages", MainMenuSaveMessages),
    named("", "partner_impl_is_sick", PartnerImplIsSick),
    named("", "tamer_messages", TamerMessages),
    named("", "utils2_pause", Utils2Pause),
    named("", "script_menu_hiragana_ka_row", ScriptMenuHiraganaKaRow),
    named("", "script_menu_lowercase_rows", ScriptMenuLowercaseRows),
    named("", "script_menu_trade_label", ScriptMenuTradeLabel),
    named("", "script_menu_naming_buttons", ScriptMenuNamingButtons),
    named("TRN_REL.BIN", "reward_messages", TrnRewardMessages),
    named("TRN_REL.BIN", "reward_mastered", TrnRewardMastered),
    named("", "singles", Singles),
};

// The decomp names these strings, but their symbols are not in config/us and the data around them
// does not pin them down.
constexpr std::array<std::string_view, 3> UsMissing = {
    "btl/battle_hud.yaml MESSAGES: 7 strings",
    "vs/vs_scene.yaml RESULTS: 2 strings",
    "vs/vs_select.yaml PRESS_START: 2 strings",
};

constexpr std::array Manifests = {
    StringManifest{.version = "us",
                   .language = "en",
                   .overlays = UsOverlays,
                   .blocks = UsBlocks,
                   .missing = UsMissing},
};

core::Result<std::string> stringAt(std::uint32_t address, const exe::MemoryImage& image,
                                   const exe::MemoryImage& executable) {
  auto text = image.stringAt(address, MaxStringLength);
  if (!text && &image != &executable) {
    text = executable.stringAt(address, MaxStringLength);
  }
  return text;
}

core::Result<std::string> fieldAt(std::uint32_t address, std::uint32_t length,
                                  const exe::MemoryImage& image) {
  const auto bytes = image.bytesAt(address, length);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  const auto nul = std::ranges::find(*bytes, std::byte{0});
  const auto used = bytes->first(static_cast<std::size_t>(nul - bytes->begin()));
  return std::string(reinterpret_cast<const char*>(used.data()), used.size());
}

// Reads entry i of a block; a NULL pointer gives nothing.
core::Result<std::optional<std::string>> readEntry(const StringBlock& block, std::uint32_t i,
                                                   const exe::MemoryImage& image,
                                                   const exe::MemoryImage& executable) {
  switch (block.storage) {
  case StringStorage::Pointers: {
    const auto word = image.bytesAt(block.address + (i * 4), 4);
    if (!word) {
      return core::fail("the pointer is outside the file");
    }
    const std::uint32_t pointer = ByteReader{*word}.u32(0).value_or(0);
    if (pointer == 0) {
      return std::optional<std::string>{};
    }
    return stringAt(pointer, image, executable);
  }
  case StringStorage::Records:
  case StringStorage::Embedded:
    return fieldAt(block.address + (i * block.stride), block.length, image);
  case StringStorage::Named:
    return stringAt(block.addresses[i], image, executable);
  }
  return core::fail("unknown storage");
}

} // namespace

std::optional<StringManifest> findStringManifest(std::string_view version) {
  const auto found = std::ranges::find(Manifests, version, &StringManifest::version);
  return found == Manifests.end() ? std::nullopt : std::optional{*found};
}

core::Result<std::vector<std::optional<std::string>>>
readStringBlock(const StringBlock& block, const exe::MemoryImage& image,
                const exe::MemoryImage& executable) {
  std::vector<std::optional<std::string>> strings;
  for (std::uint32_t i = 0; i < block.count; ++i) {
    auto text = readEntry(block, i, image, executable);
    if (!text) {
      return core::fail(std::format("{}[{}]: {}", block.name, i, text.error().message));
    }
    strings.push_back(std::move(*text));
  }
  return strings;
}

} // namespace dwe::formats::text
