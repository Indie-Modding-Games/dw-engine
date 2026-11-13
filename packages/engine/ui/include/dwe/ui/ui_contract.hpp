#pragma once

#include <compare>
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

  friend constexpr auto operator<=>(const UiApiVersion&, const UiApiVersion&) = default;
};
inline constexpr UiApiVersion CurrentUiApiVersion{.major = 0, .minor = 15, .patch = 0};

// A version as text: "0.9.0".
[[nodiscard]] std::string uiApiVersionText(UiApiVersion version);

// What an entry of the contract is to a screen's documents.
enum class UiContractKind : std::uint8_t {
  // A value the game publishes and the documents read (ui.setting).
  Setting,
  // A command the documents send (ui.send) and the game handles.
  Command,
  // A player option: the documents read it and send its name with a new value to change it.
  Option,
  // A value a document is shown with (ui.show's third argument), which its on_show reads.
  Argument,
  // A function of a namespace of the UI API that reads the game (partner.status()): it returns a
  // fresh table and changes nothing.
  Query,
  // A function of a namespace of the UI API that asks the game to act (bag.use(slot)): it checks
  // its arguments, returns true when the request is queued or false and why, and the game answers
  // with an event.
  Action,
  // An event of the UI API (hooks.on): its handlers get a table with `version` and its fields.
  Event,
  // A state a document reports (ui.report) and the game reads when it needs it, such as whether a
  // menu page shows a 3D model: the latest value counts, unlike a command the game handles once.
  Report,
  // A function of the engine's own Lua API, the same for every document (ui.send, hooks.on,
  // element:set_class), or a handler a document defines for the UI to call (on_show). Without a
  // signature it is a field: a global (document_name) or a read-only property (element.id).
  Function,
};

// The value a setting holds or a command carries.
enum class UiValueKind : std::uint8_t {
  None,
  Boolean,
  Number,
  Text,
  // A number or a text, as the entry says.
  Any,
  // A Lua table, as the entry says.
  Table,
};

// One setting, command or option a screen's documents use. A name may hold placeholders for the
// parts published one by one: <n> stands for a whole number from 0, <id> for an identifier
// (letters, digits, underscores), so `player.chart.<n>.level` covers player.chart.0.level...
//
// The UI API's namespaces (clock, partner, bag...) are screens of the contract too: their queries,
// actions and events are named namespace.function (partner.status), with the Lua signature of the
// function (`(index: integer): boolean, string?`), and an event names the settings it fires on.
struct UiContractEntry {
  std::string_view screen;
  UiContractKind kind = UiContractKind::Setting;
  std::string_view name;
  UiValueKind value = UiValueKind::None;
  std::string_view summary;
  // A query's or an action's Lua signature: its parameters and what it returns.
  std::string_view signature{};
  // An event's settings, separated by spaces, or "@options" for every option of the contract.
  std::string_view source{};
};

// Whether a name is one an entry's name covers, placeholders and all.
[[nodiscard]] bool matchesContractName(std::string_view pattern, std::string_view name);

// The entry of a screen of that kind covering a name; nullptr when the contract lacks it.
[[nodiscard]] const UiContractEntry* findContractEntry(std::span<const UiContractEntry> contract,
                                                       std::string_view screen, UiContractKind kind,
                                                       std::string_view name);

// The engine's own Lua API (scripting.cpp), which every game's contract starts with: the ui table,
// hooks, t, document_name, the handlers a document defines, and the methods of its elements and
// events.
[[nodiscard]] std::span<const UiContractEntry> nativeUiContract();

// The contract as content/schemas/ui_api/ui_api.d.lua: LuaLS definitions of every function, with
// its parameters and results, and the names of the events, for completion in an editor.
[[nodiscard]] std::string contractLuaStubs(std::span<const UiContractEntry> contract);

// The contract as the generated part of docs/modding/ui-api.md: the version, then one table per
// screen.
[[nodiscard]] std::string contractMarkdown(std::span<const UiContractEntry> contract);

// The contract as content/schemas/ui_api/ui_api.json, for tools and editors.
[[nodiscard]] std::string contractJson(std::span<const UiContractEntry> contract);

} // namespace dwe::ui
