#include <dwe/ui/ui_contract.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <vector>

namespace dwe::ui {
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
    return "ajuste";
  case UiContractKind::Command:
    return "comando";
  case UiContractKind::Option:
    return "opción";
  case UiContractKind::Argument:
    return "argumento";
  case UiContractKind::Query:
    return "consulta";
  case UiContractKind::Action:
    return "acción";
  case UiContractKind::Event:
    return "evento";
  case UiContractKind::Report:
    return "estado";
  case UiContractKind::Function:
    return "función";
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
    return "sí/no";
  case UiValueKind::Number:
    return "número";
  case UiValueKind::Text:
    return "texto";
  case UiValueKind::Any:
    return "número o texto";
  case UiValueKind::Table:
    return "tabla";
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

std::string versionText() {
  return std::format("{}.{}.{}", CurrentUiApiVersion.major, CurrentUiApiVersion.minor,
                     CurrentUiApiVersion.patch);
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
           "El valor de un ajuste que el juego publica; nil si no lo publica."),
    native("ui", "ui.send", NoValue, "(name: string, value: any?)",
           "Manda un comando al juego, que lo atiende una vez."),
    native("ui", "ui.report", NoValue, "(name: string, value: any?)",
           "Cuenta un estado al juego, que lee el último valor cuando lo necesita."),
    native("ui", "ui.show", NoValue, "(screen: string, modal: boolean?, args: table?)",
           "Enseña una pantalla; modal toma la navegación. args llega a su on_show."),
    native("ui", "ui.hide", NoValue, "(screen: string)", "Esconde una pantalla."),
    native("ui", "ui.is_visible", BooleanValue, "(screen: string): boolean",
           "Si una pantalla se ve."),
    native("ui", "ui.layout", TextValue, "(option: string): string?",
           "El valor que el aspecto da a una opción de disposición en esta pantalla; nil sin él."),
    native("ui", "ui.prompt", TextValue, "(action: string): string",
           "El nombre del control que hace una acción en el dispositivo en uso."),
    native("ui", "ui.device", TextValue, "(): string",
           "El dispositivo en uso: keyboard_mouse o gamepad."),
    native("ui", "ui.gamepad_family", TextValue, "(): string", "La familia del mando en uso."),
    native("ui", "ui.languages", TableValue, "(): string[]",
           "Los idiomas que el jugador puede elegir."),
    native("ui", "ui.skins", TableValue, "(): string[]",
           "Los aspectos que el jugador puede elegir."),
    native("ui", "ui.skin_name_key", TextValue, "(id: string): string",
           "La clave de texto del nombre de un aspecto; vacía si no existe."),
    native("ui", "ui.cue", BooleanValue, "(name: string): boolean",
           "Suena box_open, box_close o confirm; false con otro nombre."),
    native("ui", "t", TextValue, "(key: string, args: table?): string",
           "El texto de una clave en el idioma del jugador, con sus huecos rellenos."),
    native("ui", "document_name", TextValue, "",
           "El nombre con el que la interfaz cargó el documento."),
    native("hooks", "hooks.on", NoValue,
           "(event: string, handler: fun(event: table, document: Element))",
           "Llama a handler cada vez que llega el evento; hasta 64 por documento."),
    native("hooks", "hooks.off", NoValue, "(event: string, handler: function)",
           "Deja de llamar a handler con ese evento."),
    native("document", "on_show", NoValue, "(document: Element, args: table)",
           "Lo llama la interfaz al enseñar el documento."),
    native("document", "on_hide", NoValue, "(document: Element)",
           "Lo llama la interfaz al esconder el documento."),
    native("document", "on_setting", NoValue, "(document: Element, name: string)",
           "Lo llama la interfaz cuando cambia un ajuste, con el documento a la vista."),
    native("document", "on_action", BooleanValue, "(document: Element, action: string): boolean?",
           "Una acción de navegación (confirm, cancel, menu...); true si el documento la atiende."),
    native("document", "on_tick", NoValue, "(document: Element, seconds: number)",
           "Cada frame con el documento a la vista."),
    native("document", "on_device", NoValue, "(document: Element)",
           "Cambió el dispositivo en uso."),
    native("document", "on_language", NoValue, "(document: Element)", "Cambió el idioma."),
    native("Element", "Element:valid", BooleanValue, "(): boolean", "Si el elemento existe."),
    native("Element", "Element.id", TextValue, "", "Su id (campo de solo lectura)."),
    native("Element", "Element:get", TableValue, "(id: string): Element",
           "El elemento con ese id dentro de este."),
    native("Element", "Element:set_text", NoValue, "(text: string)", "Pone su texto."),
    native("Element", "Element:set_prompt", NoValue, "(action: string, icon: boolean?)",
           "Lo convierte en el dibujo del control que hace la acción."),
    native("Element", "Element:set_prompt_text", NoValue, "(text: string, original: boolean?)",
           "Pone un texto con sus marcas {prompt:acción} dibujadas como controles."),
    native("Element", "Element:text", TextValue, "(): string", "Su contenido."),
    native("Element", "Element:set_class", NoValue, "(name: string, on: boolean)",
           "Pone o quita una clase."),
    native("Element", "Element:has_class", BooleanValue, "(name: string): boolean",
           "Si tiene una clase."),
    native("Element", "Element:attribute", TextValue, "(name: string): string?",
           "El valor de un atributo; nil sin él."),
    native("Element", "Element:set_attribute", NoValue, "(name: string, value: string)",
           "Pone un atributo; no uno de evento (on...)."),
    native("Element", "Element:set_property", NoValue, "(name: string, value: string)",
           "Pone una propiedad de estilo."),
    native("Element", "Element:remove_property", NoValue, "(name: string)",
           "Quita una propiedad que puso set_property."),
    native("Element", "Element:rect", NumberValue, "(): number, number, number, number",
           "Su caja en dp desde la esquina de la ventana: izquierda, arriba, ancho y alto."),
    native("Element", "Element:focus", NoValue, "()", "Le da el foco."),
    native("Element", "Element:has_focus", BooleanValue, "(): boolean", "Si tiene el foco."),
    native("Element", "Element:click", NoValue, "()", "Lo pulsa."),
    native("Element", "Element:scroll_to_top", NoValue, "()", "Vuelve una lista arriba."),
    native("Event", "Event.type", TextValue, "", "El tipo del evento (campo de solo lectura)."),
    native("Event", "Event.wheel", NumberValue, "",
           "Cuánto giró la rueda, positivo hacia abajo (campo de solo lectura)."),
    native("Event", "Event:stop", NoValue, "()", "No deja que el evento siga subiendo."),
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
  std::string text = std::format("\nVersión de la API de la interfaz: **{}**.\n", versionText());
  for (const std::string_view screen : screensOf(contract)) {
    text += std::format("\n### `{}`\n\n| Nombre | Tipo | Valor | Qué es |\n|---|---|---|---|\n",
                        screen);
    for (const UiContractEntry& entry : contract) {
      if (entry.screen != screen) {
        continue;
      }
      std::string summary{entry.summary};
      if (!entry.source.empty()) {
        summary += std::format(" Sale de `{}`.", entry.source);
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
