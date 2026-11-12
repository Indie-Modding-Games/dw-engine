#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace dwe::ui {

// The UI API's version (docs/modding/ui-api.md), apart from the gameplay Lua API's: a minor adds,
// a major removes or changes a meaning. It stays below 1.0.0 while the contract is today's settings
// and commands, before the first public release.
struct UiApiVersion {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t patch = 0;
};
inline constexpr UiApiVersion CurrentUiApiVersion{.major = 0, .minor = 1, .patch = 0};

// What an entry of the contract is to a screen's documents.
enum class UiContractKind : std::uint8_t {
  // A value the game publishes and the documents read (ui.setting).
  Setting,
  // A command the documents send (ui.send) and the game handles.
  Command,
  // A player option: the documents read it and send its name with a new value to change it.
  Option,
};

// The value a setting holds or a command carries.
enum class UiValueKind : std::uint8_t {
  None,
  Boolean,
  Number,
  Text,
  // A number or a text, as the entry says.
  Any,
};

// One setting, command or option a screen's documents use. A name may hold placeholders for the
// parts published one by one: <n> stands for a whole number from 0, <id> for an identifier
// (letters, digits, underscores), so `player.chart.<n>.level` covers player.chart.0.level...
struct UiContractEntry {
  std::string_view screen;
  UiContractKind kind = UiContractKind::Setting;
  std::string_view name;
  UiValueKind value = UiValueKind::None;
  std::string_view summary;
};

// Whether a name is one an entry's name covers, placeholders and all.
[[nodiscard]] bool matchesContractName(std::string_view pattern, std::string_view name);

// The entry of a screen of that kind covering a name; nullptr when the contract lacks it.
[[nodiscard]] const UiContractEntry* findContractEntry(std::span<const UiContractEntry> contract,
                                                       std::string_view screen, UiContractKind kind,
                                                       std::string_view name);

// The contract as the generated part of docs/modding/ui-api.md: the version, then one table per
// screen.
[[nodiscard]] std::string contractMarkdown(std::span<const UiContractEntry> contract);

// The contract as content/schemas/ui_api/ui_api.json, for tools and editors.
[[nodiscard]] std::string contractJson(std::span<const UiContractEntry> contract);

} // namespace dwe::ui
