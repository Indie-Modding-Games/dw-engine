#include <dwe/ui/ui_contract.hpp>

#include <algorithm>
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
      if (entry.screen == screen) {
        text += std::format("| `{}` | {} | {} | {} |\n", entry.name, kindWord(entry.kind),
                            valueWord(entry.value), entry.summary);
      }
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
      text +=
          std::format("{}\n      {{\"name\": {}, \"kind\": {}, \"value\": {}, \"summary\": {}}}",
                      first ? "" : ",", jsonString(entry.name), jsonString(kindKey(entry.kind)),
                      jsonString(valueKey(entry.value)), jsonString(entry.summary));
      first = false;
    }
    text += "\n    ]";
  }
  return text + "\n  }\n}\n";
}

} // namespace dwe::ui
