#include <dwe/ui/glyph_art.hpp>
#include <dwe/ui/prompts.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string_view>
#include <variant>

namespace dwe::ui {
namespace {

using platform::GamepadAxis;
using platform::GamepadButton;
using platform::GamepadFamily;
using platform::KeyCode;
using platform::MouseButton;

// USB HID usage ids of the keys whose names are not a single printed character.
struct NamedKey {
  KeyCode key;
  std::string_view name;
};

constexpr std::array NamedKeys{
    NamedKey{.key = 40, .name = "enter"},
    NamedKey{.key = 41, .name = "escape"},
    NamedKey{.key = 42, .name = "backspace"},
    NamedKey{.key = 43, .name = "tab"},
    NamedKey{.key = 44, .name = "space"},
    NamedKey{.key = 45, .name = "minus"},
    NamedKey{.key = 46, .name = "equals"},
    NamedKey{.key = 47, .name = "left_bracket"},
    NamedKey{.key = 48, .name = "right_bracket"},
    NamedKey{.key = 49, .name = "backslash"},
    NamedKey{.key = 51, .name = "semicolon"},
    NamedKey{.key = 52, .name = "apostrophe"},
    NamedKey{.key = 53, .name = "grave"},
    NamedKey{.key = 54, .name = "comma"},
    NamedKey{.key = 55, .name = "period"},
    NamedKey{.key = 56, .name = "slash"},
    NamedKey{.key = 57, .name = "caps_lock"},
    NamedKey{.key = 73, .name = "insert"},
    NamedKey{.key = 74, .name = "home"},
    NamedKey{.key = 75, .name = "page_up"},
    NamedKey{.key = 76, .name = "delete"},
    NamedKey{.key = 77, .name = "end"},
    NamedKey{.key = 78, .name = "page_down"},
    NamedKey{.key = 79, .name = "right"},
    NamedKey{.key = 80, .name = "left"},
    NamedKey{.key = 81, .name = "down"},
    NamedKey{.key = 82, .name = "up"},
    NamedKey{.key = 88, .name = "keypad_enter"},
    NamedKey{.key = 224, .name = "left_ctrl"},
    NamedKey{.key = 225, .name = "left_shift"},
    NamedKey{.key = 226, .name = "left_alt"},
    NamedKey{.key = 228, .name = "right_ctrl"},
    NamedKey{.key = 229, .name = "right_shift"},
    NamedKey{.key = 230, .name = "right_alt"},
};

// The HID ranges of letters, digits and function keys.
constexpr KeyCode FirstLetter = 4;
constexpr KeyCode LastLetter = 29;
constexpr KeyCode FirstDigit = 30; // 1; 0 comes after 9
constexpr KeyCode LastDigit = 39;
constexpr KeyCode FirstFunction = 58; // F1 to F12
constexpr KeyCode LastFunction = 69;

constexpr std::string_view KeyPrefix = "key:";
constexpr std::string_view GamepadPrefix = "gamepad:";

// Where the labels are among the texts (localization/<language>/prompt).
constexpr std::string_view LabelKeyPrefix = "prompt.";
constexpr std::string_view FamilyKeyPrefix = "prompt.family.";
constexpr char ControlSeparator = ':';
constexpr char KeySeparator = '.';

std::string_view mouseButtonName(MouseButton button) {
  switch (button) {
  case MouseButton::Left:
    return "left";
  case MouseButton::Middle:
    return "middle";
  case MouseButton::Right:
    return "right";
  case MouseButton::Back:
    return "back";
  case MouseButton::Forward:
    return "forward";
  }
  return "left";
}

std::string_view gamepadButtonName(GamepadButton button) {
  switch (button) {
  case GamepadButton::South:
    return "south";
  case GamepadButton::East:
    return "east";
  case GamepadButton::West:
    return "west";
  case GamepadButton::North:
    return "north";
  case GamepadButton::Start:
    return "start";
  case GamepadButton::Back:
    return "back";
  case GamepadButton::Guide:
    return "guide";
  case GamepadButton::LeftStick:
    return "left_stick_click";
  case GamepadButton::RightStick:
    return "right_stick_click";
  case GamepadButton::LeftShoulder:
    return "left_shoulder";
  case GamepadButton::RightShoulder:
    return "right_shoulder";
  case GamepadButton::DpadUp:
    return "dpad_up";
  case GamepadButton::DpadDown:
    return "dpad_down";
  case GamepadButton::DpadLeft:
    return "dpad_left";
  case GamepadButton::DpadRight:
    return "dpad_right";
  }
  return "south";
}

std::string_view axisName(GamepadAxis axis) {
  switch (axis) {
  case GamepadAxis::LeftX:
  case GamepadAxis::LeftY:
    return "left_stick";
  case GamepadAxis::RightX:
  case GamepadAxis::RightY:
    return "right_stick";
  case GamepadAxis::LeftTrigger:
    return "left_trigger";
  case GamepadAxis::RightTrigger:
    return "right_trigger";
  }
  return "left_stick";
}

// A key that shows as its own name in capitals when the labels do not name it: a letter, a digit or
// F1 to F12.
bool printsItself(std::string_view key) {
  if (key.size() == 1) {
    return std::isalnum(static_cast<unsigned char>(key.front())) != 0;
  }
  return key.size() <= 3 && key.front() == 'f' && std::ranges::all_of(key.substr(1), [](char c) {
           return std::isdigit(static_cast<unsigned char>(c)) != 0;
         });
}

std::string capitals(std::string_view text) {
  std::string upper{text};
  for (char& c : upper) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return upper;
}

// The key of a control's label: "key:enter" is prompt.key.enter.
std::string labelKey(std::string_view control) {
  std::string key = std::format("{}{}", LabelKeyPrefix, control);
  std::ranges::replace(key, ControlSeparator, KeySeparator);
  return key;
}

} // namespace

std::string controlLabel(std::string_view control, GamepadFamily family,
                         const localization::Catalog& texts) {
  if (control.starts_with(GamepadPrefix)) {
    const std::string_view button = control.substr(GamepadPrefix.size());
    if (auto found = texts.find(std::format("{}{}{}{}", FamilyKeyPrefix, gamepadFamilyName(family),
                                            KeySeparator, button))) {
      return localization::formatText(*found, {});
    }
  }
  if (auto found = texts.find(labelKey(control))) {
    return localization::formatText(*found, {});
  }
  if (control.starts_with(KeyPrefix) && printsItself(control.substr(KeyPrefix.size()))) {
    return capitals(control.substr(KeyPrefix.size()));
  }
  return std::string{control};
}

std::string keyControl(KeyCode key) {
  if (key >= FirstLetter && key <= LastLetter) {
    return std::format("{}{}", KeyPrefix, static_cast<char>('a' + (key - FirstLetter)));
  }
  if (key >= FirstDigit && key <= LastDigit) {
    return std::format("{}{}", KeyPrefix, key == LastDigit ? 0 : key - FirstDigit + 1);
  }
  if (key >= FirstFunction && key <= LastFunction) {
    return std::format("{}f{}", KeyPrefix, key - FirstFunction + 1);
  }
  for (const NamedKey& named : NamedKeys) {
    if (named.key == key) {
      return std::format("{}{}", KeyPrefix, named.name);
    }
  }
  return std::format("{}{}", KeyPrefix, key);
}

std::string gamepadButtonControl(GamepadButton button) {
  return std::format("{}{}", GamepadPrefix, gamepadButtonName(button));
}

std::string_view gamepadFamilyName(GamepadFamily family) {
  switch (family) {
  case GamepadFamily::Xbox:
    return "xbox";
  case GamepadFamily::PlayStation:
    return "playstation";
  case GamepadFamily::Nintendo:
    return "nintendo";
  case GamepadFamily::Steam:
    return "steam";
  case GamepadFamily::Generic:
    break;
  }
  return "generic";
}

namespace {

// The modifiers a keyboard has on both sides. An action bound to both keys of a pair (cancel's
// Ctrls, walk's Shifts) names the pair, as the player knows it: "key:ctrl", not the left one.
struct ModifierPair {
  KeyCode left;
  KeyCode right;
  std::string_view control;
};
constexpr std::array ModifierPairs{
    ModifierPair{.left = 224, .right = 228, .control = "key:ctrl"},
    ModifierPair{.left = 225, .right = 229, .control = "key:shift"},
    ModifierPair{.left = 226, .right = 230, .control = "key:alt"},
};

bool bindsKey(const input::ActionMap& map, std::string_view action, KeyCode key) {
  return std::ranges::any_of(map.bindings(action), [key](const input::Binding& binding) {
    const auto* bound = std::get_if<input::KeyBinding>(&binding);
    return bound != nullptr && bound->key == key;
  });
}

std::string keyPromptControl(const input::ActionMap& map, std::string_view action, KeyCode key) {
  for (const ModifierPair& pair : ModifierPairs) {
    if ((key == pair.left && bindsKey(map, action, pair.right)) ||
        (key == pair.right && bindsKey(map, action, pair.left))) {
      return std::string{pair.control};
    }
  }
  return keyControl(key);
}

} // namespace

std::string promptControl(const input::ActionMap& map, input::DeviceKind device,
                          std::string_view action) {
  std::string fallback;
  for (const input::Binding& binding : map.bindings(action)) {
    if (device == input::DeviceKind::KeyboardMouse) {
      if (const auto* key = std::get_if<input::KeyBinding>(&binding)) {
        return keyPromptControl(map, action, key->key);
      }
      if (const auto* mouse = std::get_if<input::MouseBinding>(&binding);
          mouse != nullptr && fallback.empty()) {
        fallback = std::format("mouse:{}", mouseButtonName(mouse->button));
      }
    } else {
      if (const auto* button = std::get_if<input::GamepadButtonBinding>(&binding)) {
        return gamepadButtonControl(button->button);
      }
      if (const auto* axis = std::get_if<input::GamepadAxisBinding>(&binding);
          axis != nullptr && fallback.empty()) {
        fallback = std::format("{}{}", GamepadPrefix, axisName(axis->axis));
      }
    }
  }
  return fallback;
}

std::string promptLabel(const input::ActionMap& map, input::DeviceKind device, GamepadFamily family,
                        std::string_view action, const localization::Catalog& texts) {
  const std::string control = promptControl(map, device, action);
  return control.empty() ? std::string{} : controlLabel(control, family, texts);
}

} // namespace dwe::ui

namespace dwe::ui {
namespace {

using platform::GamepadFamily;

constexpr std::string_view MousePrefix = "mouse:";

// The shapes of the four face buttons by position, as the original's pad marks them.
struct FaceShape {
  std::string_view position;
  std::string_view shape;
  std::string_view character; // the original font's (originalShapeCharacter)
};
constexpr std::array FaceShapes{
    FaceShape{.position = "south", .shape = "cross", .character = "\xE2\x96\xB3"}, // △
    FaceShape{.position = "east", .shape = "circle", .character = "\xE2\x97\x8B"}, // ○
    FaceShape{.position = "west", .shape = "square", .character = "\xE2\x96\xA1"}, // □
    FaceShape{.position = "north", .shape = "triangle", .character = "\xC3\x97"},  // ×
};

// The original's texts that name a button, and the action the game binds that button to. The
// font draws the cross in the cell the extraction writes as "△" and the triangle in "×"; a
// corrected extraction ("✕") reads the same. The curling's help says "X button" in words.
struct OriginalButton {
  std::string_view text;
  std::string_view action;
};
constexpr std::array OriginalButtons{
    OriginalButton{.text = "X button", .action = "confirm"},
    OriginalButton{.text = "\xE2\x96\xB3", .action = "confirm"}, // △, the cross's cell
    OriginalButton{.text = "\xE2\x9C\x95", .action = "confirm"}, // ✕
    OriginalButton{.text = "\xC3\x97", .action = "menu"},        // ×, the triangle's cell
    OriginalButton{.text = "\xE2\x97\x8B", .action = "alt"},     // ○
    OriginalButton{.text = "\xE2\x96\xA1", .action = "extra"},   // □
};

constexpr std::string_view MarkerOpen = "{prompt:";
constexpr char MarkerClose = '}';

// The keys whose cap shows a symbol or a short name instead of their label.
struct KeySymbol {
  std::string_view key;
  std::string_view symbol;
};
constexpr std::array KeySymbols{
    KeySymbol{.key = "space", .symbol = "@space"},
    KeySymbol{.key = "enter", .symbol = "@enter"},
    KeySymbol{.key = "keypad_enter", .symbol = "@enter"},
    KeySymbol{.key = "backspace", .symbol = "@backspace"},
    KeySymbol{.key = "up", .symbol = "@up"},
    KeySymbol{.key = "down", .symbol = "@down"},
    KeySymbol{.key = "left", .symbol = "@left"},
    KeySymbol{.key = "right", .symbol = "@right"},
    KeySymbol{.key = "escape", .symbol = "ESC"},
    KeySymbol{.key = "tab", .symbol = "TAB"},
    KeySymbol{.key = "delete", .symbol = "DEL"},
    KeySymbol{.key = "insert", .symbol = "INS"},
    KeySymbol{.key = "home", .symbol = "HOME"},
    KeySymbol{.key = "end", .symbol = "END"},
    KeySymbol{.key = "page_up", .symbol = "PGUP"},
    KeySymbol{.key = "page_down", .symbol = "PGDN"},
    KeySymbol{.key = "caps_lock", .symbol = "CAPS"},
    KeySymbol{.key = "left_shift", .symbol = "SHIFT"},
    KeySymbol{.key = "right_shift", .symbol = "SHIFT"},
    KeySymbol{.key = "ctrl", .symbol = "CTRL"},
    KeySymbol{.key = "shift", .symbol = "SHIFT"},
    KeySymbol{.key = "alt", .symbol = "ALT"},
    KeySymbol{.key = "left_ctrl", .symbol = "CTRL"},
    KeySymbol{.key = "right_ctrl", .symbol = "CTRL"},
    KeySymbol{.key = "left_alt", .symbol = "ALT"},
    KeySymbol{.key = "right_alt", .symbol = "ALT"},
    KeySymbol{.key = "minus", .symbol = "-"},
    KeySymbol{.key = "equals", .symbol = "="},
    KeySymbol{.key = "comma", .symbol = ","},
    KeySymbol{.key = "period", .symbol = "."},
    KeySymbol{.key = "slash", .symbol = "@slash"},
};

// The longest text a cap draws: a longer label is cut (the drawing's font is the glyphs' own).
constexpr std::size_t MaxSymbolText = 7;

// A label in the capitals the drawn glyphs' font has (A-Z, 0-9, + and -), cut to MaxSymbolText.
std::string symbolText(std::string_view label) {
  std::string text;
  for (const char c : label) {
    const auto u = static_cast<unsigned char>(c);
    if (std::isalnum(u) != 0 && u < 0x80) {
      text += static_cast<char>(std::toupper(u));
    } else if (c == '+' || c == '-') {
      text += c;
    }
    if (text.size() == MaxSymbolText) {
      break;
    }
  }
  return text.empty() ? std::string{"?"} : text;
}

std::string keySymbol(std::string_view key, std::string_view label) {
  for (const KeySymbol& named : KeySymbols) {
    if (named.key == key) {
      return std::string{named.symbol};
    }
  }
  return symbolText(label);
}

std::string escapeRml(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    default:
      out += c;
    }
  }
  return out;
}

std::string_view kindName(GlyphKind kind) {
  switch (kind) {
  case GlyphKind::Key:
    return "key";
  case GlyphKind::Mouse:
    return "mouse";
  case GlyphKind::Face:
    return "face";
  case GlyphKind::Button:
    return "button";
  case GlyphKind::Stick:
    return "stick";
  case GlyphKind::None:
    break;
  }
  return "none";
}

std::string_view faceStyleName(FaceStyle style) {
  switch (style) {
  case FaceStyle::Shapes:
    return "shapes";
  case FaceStyle::Positions:
    return "positions";
  case FaceStyle::Letters:
    break;
  }
  return "letters";
}

constexpr std::array PromptClassPrefixes{std::string_view{"glyph"}, std::string_view{"family-"},
                                         std::string_view{"face-"}, std::string_view{"shape-"}};

// The drawing's path (glyph_art.hpp): a round button for a face button, a cap for anything else.
std::string classicArtSource(const PromptGlyph& glyph, int frame) {
  const std::string_view base = glyph.kind == GlyphKind::Face ? "button" : "cap";
  return std::format("/glyph/classic/{}/{}/{}", base, glyph.symbol, frame);
}

} // namespace

FaceStyle faceStyleOf(GamepadFamily family) {
  switch (family) {
  case GamepadFamily::PlayStation:
    return FaceStyle::Shapes;
  case GamepadFamily::Xbox:
  case GamepadFamily::Nintendo:
  case GamepadFamily::Steam:
    return FaceStyle::Letters;
  case GamepadFamily::Generic:
    break;
  }
  return FaceStyle::Positions;
}

std::string_view originalShapeCharacter(std::string_view shape) {
  for (const FaceShape& face : FaceShapes) {
    if (face.shape == shape) {
      return face.character;
    }
  }
  return {};
}

PromptGlyph promptGlyph(const input::ActionMap& map, input::DeviceKind device, GamepadFamily family,
                        std::string_view action, const localization::Catalog& texts) {
  PromptGlyph glyph;
  glyph.control = promptControl(map, device, action);
  if (glyph.control.empty()) {
    return glyph;
  }
  glyph.label = controlLabel(glyph.control, family, texts);
  if (glyph.control.starts_with(KeyPrefix)) {
    glyph.kind = GlyphKind::Key;
    glyph.symbol = keySymbol(std::string_view{glyph.control}.substr(KeyPrefix.size()), glyph.label);
    return glyph;
  }
  if (glyph.control.starts_with(MousePrefix)) {
    glyph.kind = GlyphKind::Mouse;
    const std::string_view button = std::string_view{glyph.control}.substr(MousePrefix.size());
    glyph.symbol = button == "left"     ? "LMB"
                   : button == "right"  ? "RMB"
                   : button == "middle" ? "MMB"
                   : button == "back"   ? "M4"
                                        : "M5";
    return glyph;
  }
  const std::string_view button = std::string_view{glyph.control}.substr(GamepadPrefix.size());
  const auto face = std::ranges::find(FaceShapes, button, &FaceShape::position);
  if (face != FaceShapes.end()) {
    glyph.kind = GlyphKind::Face;
    glyph.face = faceStyleOf(family);
    glyph.position = std::string{face->position};
    switch (glyph.face) {
    case FaceStyle::Shapes:
      glyph.shape = std::string{face->shape};
      glyph.symbol = std::format("@{}", face->shape);
      break;
    case FaceStyle::Positions:
      glyph.symbol = std::format("@{}", face->position);
      break;
    case FaceStyle::Letters:
      glyph.symbol = symbolText(glyph.label);
      break;
    }
    return glyph;
  }
  if (button.starts_with("dpad_")) {
    glyph.kind = GlyphKind::Button;
    glyph.symbol = std::format("@{}", button);
    return glyph;
  }
  if (button.ends_with("_stick") || button.ends_with("_trigger")) {
    glyph.kind = GlyphKind::Stick;
    glyph.symbol = std::format("{}{}", button.starts_with("left") ? 'L' : 'R',
                               button.ends_with("_stick") ? 'S' : 'T');
    return glyph;
  }
  glyph.kind = GlyphKind::Button;
  glyph.symbol = symbolText(glyph.label);
  return glyph;
}

std::vector<std::string> promptClasses(const PromptGlyph& glyph) {
  std::vector<std::string> classes{"glyph", std::format("glyph-{}", kindName(glyph.kind))};
  if (glyph.kind == GlyphKind::Face) {
    classes.push_back(std::format("family-{}", faceStyleName(glyph.face)));
    classes.push_back(std::format("face-{}", glyph.position));
    if (!glyph.shape.empty()) {
      classes.push_back(std::format("shape-{}", glyph.shape));
    }
  }
  return classes;
}

bool isPromptClass(std::string_view name) {
  return std::ranges::any_of(PromptClassPrefixes,
                             [name](std::string_view prefix) { return name.starts_with(prefix); });
}

int classicGlyphWidth(const PromptGlyph& glyph) {
  if (glyph.kind == GlyphKind::None) {
    return 0;
  }
  return glyph.kind == GlyphKind::Face ? 16 : classicCapWidth(glyph.symbol);
}

std::string promptMarkup(const PromptGlyph& glyph, bool icon, std::string_view id) {
  std::string markup;
  if (glyph.kind == GlyphKind::None) {
    return markup;
  }
  if (glyph.kind == GlyphKind::Face && glyph.face == FaceStyle::Shapes) {
    markup = std::format(R"(<span class="glyph-char">{}</span><span class="glyph-art"></span>)",
                         originalShapeCharacter(glyph.shape));
  } else if (glyph.kind == GlyphKind::Face && glyph.face == FaceStyle::Positions) {
    markup = R"(<span class="glyph-art"></span>)";
  } else {
    markup = std::format(R"(<span class="glyph-label">{}</span>)", escapeRml(glyph.label));
  }
  if (icon) {
    // The shapes' drawings come from the skins' style sheets: the classic one has the original's
    // own button for the cross.
    const bool styled = glyph.kind == GlyphKind::Face && glyph.face == FaceStyle::Shapes;
    const int width = classicGlyphWidth(glyph) * 3;
    for (int frame = 0; frame < 3; ++frame) {
      const std::string frameId =
          id.empty() ? std::string{} : std::format(R"( id="{}-frame-{}")", escapeRml(id), frame);
      if (styled) {
        markup += std::format(R"(<span class="glyph-frame frame-{}"{}></span>)", frame, frameId);
      } else {
        markup += std::format(
            R"(<span class="glyph-frame frame-{}"{} style="width: {}dp; decorator: image({});"></span>)",
            frame, frameId, width, classicArtSource(glyph, frame));
      }
    }
  }
  return markup;
}

std::string promptSpan(const PromptGlyph& glyph) {
  std::string classes = "key";
  for (const std::string& name : promptClasses(glyph)) {
    classes += ' ';
    classes += name;
  }
  return std::format(R"(<span class="{}">{}</span>)", classes, promptMarkup(glyph, false));
}

std::string promptText(std::string_view text, bool originalButtons,
                       const std::function<PromptGlyph(std::string_view)>& glyphOf) {
  std::string out;
  std::string plain;
  const auto flush = [&] {
    out += escapeRml(plain);
    plain.clear();
  };
  while (!text.empty()) {
    if (text.starts_with(MarkerOpen)) {
      const std::size_t close = text.find(MarkerClose);
      if (close != std::string_view::npos) {
        const std::string_view action = text.substr(MarkerOpen.size(), close - MarkerOpen.size());
        flush();
        if (const PromptGlyph glyph = glyphOf(action); glyph.kind != GlyphKind::None) {
          out += promptSpan(glyph);
        }
        text.remove_prefix(close + 1);
        continue;
      }
    }
    if (originalButtons) {
      const auto named = std::ranges::find_if(
          OriginalButtons, [text](const OriginalButton& b) { return text.starts_with(b.text); });
      if (named != OriginalButtons.end()) {
        if (const PromptGlyph glyph = glyphOf(named->action); glyph.kind != GlyphKind::None) {
          flush();
          out += promptSpan(glyph);
        } else {
          plain += named->text;
        }
        text.remove_prefix(named->text.size());
        continue;
      }
    }
    plain += text.front();
    text.remove_prefix(1);
  }
  flush();
  return out;
}

} // namespace dwe::ui
