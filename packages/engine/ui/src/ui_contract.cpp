#include <dwe/ui/ui_contract.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <vector>

namespace dwe::ui {
std::string uiApiVersionText(UiApiVersion version) {
  return std::format("{}.{}.{}", version.major, version.minor, version.patch);
}

namespace {

constexpr std::string_view NumberPlaceholder = "<n>";
constexpr std::string_view IdentifierPlaceholder = "<id>";

bool isNumber(std::string_view part) {
  return !part.empty() && std::ranges::all_of(part, [](char c) { return c >= '0' && c <= '9'; });
}

bool isIdentifier(std::string_view part) {
  return !part.empty() && std::ranges::all_of(part, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

bool matchesPart(std::string_view pattern, std::string_view part) {
  if (pattern == NumberPlaceholder) {
    return isNumber(part);
  }
  if (pattern == IdentifierPlaceholder) {
    return isIdentifier(part);
  }
  return pattern == part;
}

// The screens in the order the contract first names them.
std::vector<std::string_view> screensOf(std::span<const UiContractEntry> contract) {
  std::vector<std::string_view> screens;
  for (const UiContractEntry& entry : contract) {
    if (!std::ranges::contains(screens, entry.screen)) {
      screens.push_back(entry.screen);
    }
  }
  return screens;
}

std::string_view kindWord(UiContractKind kind) {
  switch (kind) {
  case UiContractKind::Setting:
    return "setting";
  case UiContractKind::Command:
    return "command";
  case UiContractKind::Option:
    return "option";
  case UiContractKind::Argument:
    return "argument";
  case UiContractKind::Query:
    return "query";
  case UiContractKind::Action:
    return "action";
  case UiContractKind::Event:
    return "event";
  case UiContractKind::Report:
    return "report";
  case UiContractKind::Function:
    return "function";
  }
  return "";
}

std::string_view kindKey(UiContractKind kind) {
  switch (kind) {
  case UiContractKind::Setting:
    return "setting";
  case UiContractKind::Command:
    return "command";
  case UiContractKind::Option:
    return "option";
  case UiContractKind::Argument:
    return "argument";
  case UiContractKind::Query:
    return "query";
  case UiContractKind::Action:
    return "action";
  case UiContractKind::Event:
    return "event";
  case UiContractKind::Report:
    return "report";
  case UiContractKind::Function:
    return "function";
  }
  return "";
}

std::string_view valueWord(UiValueKind value) {
  switch (value) {
  case UiValueKind::None:
    return "—";
  case UiValueKind::Boolean:
    return "yes/no";
  case UiValueKind::Number:
    return "number";
  case UiValueKind::Text:
    return "text";
  case UiValueKind::Any:
    return "number or text";
  case UiValueKind::Table:
    return "table";
  }
  return "";
}

std::string_view valueKey(UiValueKind value) {
  switch (value) {
  case UiValueKind::None:
    return "none";
  case UiValueKind::Boolean:
    return "boolean";
  case UiValueKind::Number:
    return "number";
  case UiValueKind::Text:
    return "text";
  case UiValueKind::Any:
    return "any";
  case UiValueKind::Table:
    return "table";
  }
  return "";
}

std::string jsonString(std::string_view text) {
  std::string quoted = "\"";
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      quoted += '\\';
      quoted += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      quoted += std::format("\\u{:04x}", static_cast<unsigned>(c));
    } else {
      quoted += c;
    }
  }
  return quoted + '"';
}

// A summary as Markdown text: < and > outside its code spans as entities, so a placeholder such as
// battle.menu.<n>.command reads as text and not as an HTML tag (the docs site's Vue compiler stops
// on one it cannot close).
std::string markdownText(std::string_view text) {
  std::string escaped;
  bool inCode = false;
  for (const char c : text) {
    if (c == '`') {
      inCode = !inCode;
    }
    if (!inCode && c == '<') {
      escaped += "&lt;";
    } else if (!inCode && c == '>') {
      escaped += "&gt;";
    } else {
      escaped += c;
    }
  }
  return escaped;
}

std::string versionText() {
  return uiApiVersionText(CurrentUiApiVersion);
}

// The engine's own Lua API, the same for every document (scripting.cpp): the ui table, hooks,
// the text function, the document's elements and events, and the handlers a document may define.
constexpr UiContractEntry native(std::string_view space, std::string_view name, UiValueKind value,
                                 std::string_view signature, std::string_view summary) {
  return {.screen = space,
          .kind = UiContractKind::Function,
          .name = name,
          .value = value,
          .summary = summary,
          .signature = signature};
}

constexpr UiValueKind NoValue = UiValueKind::None;
constexpr UiValueKind BooleanValue = UiValueKind::Boolean;
constexpr UiValueKind NumberValue = UiValueKind::Number;
constexpr UiValueKind TextValue = UiValueKind::Text;
constexpr UiValueKind AnyValue = UiValueKind::Any;
constexpr UiValueKind TableValue = UiValueKind::Table;

constexpr std::array NativeContract{
    native("ui", "ui.setting", AnyValue, "(name: string): any",
           "The value of a setting the game publishes; nil if it does not publish it."),
    native("ui", "ui.send", NoValue, "(name: string, value: any?)",
           "Sends a command to the game, which handles it once."),
    native("ui", "ui.report", NoValue, "(name: string, value: any?)",
           "Tells the game a state; the game reads the last value when it needs it."),
    native("ui", "ui.show", NoValue, "(screen: string, modal: boolean?, args: table?)",
           "Shows a screen; modal takes the navigation. args reaches its on_show."),
    native("ui", "ui.hide", NoValue, "(screen: string)", "Hides a screen."),
    native("ui", "ui.is_visible", BooleanValue, "(screen: string): boolean",
           "Whether a screen is visible."),
    native("ui", "ui.layout", TextValue, "(option: string): string?",
           "The value the skin gives a layout option on this screen; nil without one."),
    native("ui", "ui.prompt", TextValue, "(action: string): string",
           "The name of the control that performs an action on the device in use."),
    native("ui", "ui.device", TextValue, "(): string",
           "The device in use: keyboard_mouse or gamepad."),
    native("ui", "ui.gamepad_family", TextValue, "(): string", "The family of the gamepad in use."),
    native("ui", "ui.languages", TableValue, "(): string[]",
           "The languages the player can choose."),
    native("ui", "ui.skins", TableValue, "(): string[]", "The skins the player can choose."),
    native("ui", "ui.skin_name_key", TextValue, "(id: string): string",
           "The text key of a skin's name; empty if it does not exist."),
    native("ui", "ui.cue", BooleanValue, "(name: string): boolean",
           "Plays box_open, box_close or confirm; false for any other name."),
    native("ui", "t", TextValue, "(key: string, args: table?): string",
           "The text of a key in the player's language, with its placeholders filled in."),
    native("ui", "document_name", TextValue, "",
           "The name the interface loaded the document with."),
    native("hooks", "hooks.on", NoValue,
           "(event: string, handler: fun(event: table, document: Element))",
           "Calls handler each time the event arrives; up to 64 per document."),
    native("hooks", "hooks.off", NoValue, "(event: string, handler: function)",
           "Stops calling handler for that event."),
    native("document", "on_show", NoValue, "(document: Element, args: table)",
           "Called by the interface when the document is shown."),
    native("document", "on_hide", NoValue, "(document: Element)",
           "Called by the interface when the document is hidden."),
    native("document", "on_setting", NoValue, "(document: Element, name: string)",
           "Called by the interface when a setting changes, with the document visible."),
    native("document", "on_action", BooleanValue, "(document: Element, action: string): boolean?",
           "A navigation action (confirm, cancel, menu...); true if the document handles it."),
    native("document", "on_tick", NoValue, "(document: Element, seconds: number)",
           "Every frame while the document is visible."),
    native("document", "on_device", NoValue, "(document: Element)", "The device in use changed."),
    native("document", "on_language", NoValue, "(document: Element)", "The language changed."),
    native("Element", "Element:valid", BooleanValue, "(): boolean", "Whether the element exists."),
    native("Element", "Element.id", TextValue, "", "Its id (a read-only field)."),
    native("Element", "Element:get", TableValue, "(id: string): Element",
           "The element with that id inside this one."),
    native("Element", "Element:set_text", NoValue, "(text: string)", "Sets its text."),
    native("Element", "Element:set_prompt", NoValue, "(action: string, icon: boolean?)",
           "Turns it into the picture of the control that performs the action."),
    native("Element", "Element:set_prompt_text", NoValue, "(text: string, original: boolean?)",
           "Sets a text with its {prompt:action} marks drawn as controls."),
    native("Element", "Element:text", TextValue, "(): string", "Its content."),
    native("Element", "Element:set_class", NoValue, "(name: string, on: boolean)",
           "Adds or removes a class."),
    native("Element", "Element:has_class", BooleanValue, "(name: string): boolean",
           "Whether it has a class."),
    native("Element", "Element:attribute", TextValue, "(name: string): string?",
           "The value of an attribute; nil without one."),
    native("Element", "Element:set_attribute", NoValue, "(name: string, value: string)",
           "Sets an attribute; not an event one (on...)."),
    native("Element", "Element:set_property", NoValue, "(name: string, value: string)",
           "Sets a style property."),
    native("Element", "Element:remove_property", NoValue, "(name: string)",
           "Removes a property that set_property set."),
    native("Element", "Element:rect", NumberValue, "(): number, number, number, number",
           "Its box in dp from the window's corner: left, top, width and height."),
    native("Element", "Element:focus", NoValue, "()", "Gives it the focus."),
    native("Element", "Element:has_focus", BooleanValue, "(): boolean",
           "Whether it has the focus."),
    native("Element", "Element:click", NoValue, "()", "Presses it."),
    native("Element", "Element:scroll_to_top", NoValue, "()", "Scrolls a list back to the top."),
    native("Event", "Event.type", TextValue, "", "The event's type (a read-only field)."),
    native("Event", "Event.wheel", NumberValue, "",
           "How far the wheel turned, positive downward (a read-only field)."),
    native("Event", "Event:stop", NoValue, "()", "Stops the event from bubbling up."),
};

std::string_view trimmed(std::string_view text) {
  while (!text.empty() && text.front() == ' ') {
    text.remove_prefix(1);
  }
  while (!text.empty() && text.back() == ' ') {
    text.remove_suffix(1);
  }
  return text;
}

// The parts of a Lua signature at the top level: "(a: number, b: fun(x: table)): boolean, string?"
// splits its parameters at the commas outside parentheses.
std::vector<std::string_view> topLevelParts(std::string_view text) {
  std::vector<std::string_view> parts;
  int depth = 0;
  std::size_t start = 0;
  for (std::size_t at = 0; at < text.size(); ++at) {
    if (text[at] == '(') {
      ++depth;
    } else if (text[at] == ')') {
      --depth;
    } else if (text[at] == ',' && depth == 0) {
      parts.push_back(trimmed(text.substr(start, at - start)));
      start = at + 1;
    }
  }
  if (const std::string_view last = trimmed(text.substr(start)); !last.empty()) {
    parts.push_back(last);
  }
  return parts;
}

struct LuaSignature {
  std::vector<std::string_view> parameters; // "name: type"
  std::vector<std::string_view> results;    // "type"
};

// "(a: number): boolean": the parameters between the first parenthesis and its match, then the
// results after the colon.
LuaSignature luaSignature(std::string_view signature) {
  LuaSignature parts;
  if (!signature.starts_with('(')) {
    return parts;
  }
  int depth = 0;
  std::size_t close = std::string_view::npos;
  for (std::size_t at = 0; at < signature.size(); ++at) {
    if (signature[at] == '(') {
      ++depth;
    } else if (signature[at] == ')' && --depth == 0) {
      close = at;
      break;
    }
  }
  if (close == std::string_view::npos) {
    return parts;
  }
  parts.parameters = topLevelParts(signature.substr(1, close - 1));
  std::string_view rest = trimmed(signature.substr(close + 1));
  if (rest.starts_with(':')) {
    parts.results = topLevelParts(rest.substr(1));
  }
  return parts;
}

std::string_view luaType(UiValueKind value) {
  switch (value) {
  case UiValueKind::None:
    return "nil";
  case UiValueKind::Boolean:
    return "boolean";
  case UiValueKind::Number:
    return "number";
  case UiValueKind::Text:
    return "string";
  case UiValueKind::Any:
    return "any";
  case UiValueKind::Table:
    return "table";
  }
  return "any";
}

bool isFunctionKind(UiContractKind kind) {
  return kind == UiContractKind::Function || kind == UiContractKind::Query ||
         kind == UiContractKind::Action;
}

// The stub of one function: its summary, @param and @return lines and an empty body.
std::string luaFunctionStub(const UiContractEntry& entry) {
  std::string text = std::format("\n--- {}\n", entry.summary);
  const LuaSignature signature = luaSignature(entry.signature);
  std::string names;
  for (const std::string_view parameter : signature.parameters) {
    const std::size_t colon = parameter.find(':');
    const std::string_view name = trimmed(parameter.substr(0, colon));
    const std::string_view type = colon == std::string_view::npos
                                      ? std::string_view{"any"}
                                      : trimmed(parameter.substr(colon + 1));
    text += std::format("---@param {} {}\n", name, type);
    names += names.empty() ? std::string{name} : std::format(", {}", name);
  }
  for (const std::string_view result : signature.results) {
    text += std::format("---@return {}\n", result);
  }
  return text + std::format("function {}({}) end\n", entry.name, names);
}

} // namespace

bool matchesContractName(std::string_view pattern, std::string_view name) {
  auto patternParts = std::views::split(pattern, '.');
  auto nameParts = std::views::split(name, '.');
  auto patternPart = patternParts.begin();
  auto namePart = nameParts.begin();
  for (; patternPart != patternParts.end() && namePart != nameParts.end();
       ++patternPart, ++namePart) {
    if (!matchesPart(std::string_view{*patternPart}, std::string_view{*namePart})) {
      return false;
    }
  }
  return patternPart == patternParts.end() && namePart == nameParts.end();
}

const UiContractEntry* findContractEntry(std::span<const UiContractEntry> contract,
                                         std::string_view screen, UiContractKind kind,
                                         std::string_view name) {
  const auto found = std::ranges::find_if(contract, [&](const UiContractEntry& entry) {
    return entry.screen == screen && entry.kind == kind && matchesContractName(entry.name, name);
  });
  return found == contract.end() ? nullptr : &*found;
}

std::string contractMarkdown(std::span<const UiContractEntry> contract) {
  std::string text = std::format("\nUI API version: **{}**.\n", versionText());
  for (const std::string_view screen : screensOf(contract)) {
    text += std::format("\n### `{}`\n\n| Name | Kind | Value | What it is |\n|---|---|---|---|\n",
                        screen);
    for (const UiContractEntry& entry : contract) {
      if (entry.screen != screen) {
        continue;
      }
      std::string summary = markdownText(entry.summary);
      if (!entry.source.empty()) {
        summary += std::format(" Comes from `{}`.", entry.source);
      }
      text += std::format("| `{}{}` | {} | {} | {} |\n", entry.name, entry.signature,
                          kindWord(entry.kind), valueWord(entry.value), summary);
    }
  }
  return text + "\n";
}

std::string contractJson(std::span<const UiContractEntry> contract) {
  std::string text =
      std::format("{{\n  \"version\": {},\n  \"screens\": {{", jsonString(versionText()));
  const std::vector<std::string_view> screens = screensOf(contract);
  for (std::size_t screen = 0; screen < screens.size(); ++screen) {
    text += std::format("{}\n    {}: [", screen == 0 ? "" : ",", jsonString(screens[screen]));
    bool first = true;
    for (const UiContractEntry& entry : contract) {
      if (entry.screen != screens[screen]) {
        continue;
      }
      std::string extra;
      if (!entry.signature.empty()) {
        extra += std::format(", \"signature\": {}", jsonString(entry.signature));
      }
      if (!entry.source.empty()) {
        extra += std::format(", \"source\": {}", jsonString(entry.source));
      }
      text +=
          std::format("{}\n      {{\"name\": {}, \"kind\": {}, \"value\": {}, \"summary\": {}{}}}",
                      first ? "" : ",", jsonString(entry.name), jsonString(kindKey(entry.kind)),
                      jsonString(valueKey(entry.value)), jsonString(entry.summary), extra);
      first = false;
    }
    text += "\n    ]";
  }
  return text + "\n  }\n}\n";
}

std::span<const UiContractEntry> nativeUiContract() {
  return NativeContract;
}

std::string contractLuaStubs(std::span<const UiContractEntry> contract) {
  std::string text = std::format(
      "---@meta\n-- The UI API {} (docs/modding/ui-api.md) for the Lua language server. The game\n"
      "-- tests write it with DWE_WRITE_API_DOC=1: do not edit it by hand.\n",
      versionText());
  std::vector<std::string_view> tables;
  for (const std::string_view screen : screensOf(contract)) {
    for (const UiContractEntry& entry : contract) {
      if (entry.screen != screen || !isFunctionKind(entry.kind)) {
        continue;
      }
      const std::size_t separator = entry.name.find_first_of(".:");
      if (separator == std::string_view::npos) {
        // A global: a field (document_name) or a function (t, on_show).
        text += entry.signature.empty()
                    ? std::format("\n--- {}\n---@type {}\n{} = nil\n", entry.summary,
                                  luaType(entry.value), entry.name)
                    : luaFunctionStub(entry);
        continue;
      }
      const std::string_view table = entry.name.substr(0, separator);
      if (!std::ranges::contains(tables, table)) {
        tables.push_back(table);
        if (table.front() >= 'A' && table.front() <= 'Z') {
          // A class: its read-only fields, then its methods below.
          text += std::format("\n---@class {}\n", table);
          for (const UiContractEntry& field : contract) {
            if (field.kind == UiContractKind::Function && field.signature.empty() &&
                field.name.starts_with(table) && field.name.size() > table.size() &&
                field.name[table.size()] == '.') {
              text += std::format("---@field {} {} {}\n", field.name.substr(table.size() + 1),
                                  luaType(field.value), field.summary);
            }
          }
          text += std::format("local {} = {{}}\n", table);
        } else {
          text += std::format("\n{} = {{}}\n", table);
        }
      }
      if (!entry.signature.empty()) {
        text += luaFunctionStub(entry);
      }
    }
  }
  text += "\n---@alias UiEventName string\n";
  for (const UiContractEntry& entry : contract) {
    if (entry.kind == UiContractKind::Event) {
      text += std::format("---| \"{}\" {}\n", entry.name, entry.summary);
    }
  }
  return text;
}

} // namespace dwe::ui
