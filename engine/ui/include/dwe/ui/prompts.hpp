#pragma once

#include <dwe/core/error.hpp>
#include <dwe/input/action_map.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/platform/events.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ui {

// The label of a control (see promptControl) as a family of gamepads labels it. Labels are
// player-facing text, so they come from the texts in the player's language
// (localization/<language>/prompt): "key:enter" is the key prompt.key.enter, and a family's own
// label of a gamepad button, such as "gamepad:south" on a PlayStation pad, is
// prompt.family.playstation.south, looked up first. Letters, digits and function keys without a
// label show as themselves ("A", "7", "F5"); any other control without one shows its name.
[[nodiscard]] std::string controlLabel(std::string_view control, platform::GamepadFamily family,
                                       const localization::Catalog& texts);

// The control that triggers an action on a kind of device, for button prompts: the first key (or
// mouse button) bound to it on keyboard and mouse, or the first gamepad button (or stick) —
// "key:enter", "mouse:left", "gamepad:south", "gamepad:left_stick". A modifier bound on both
// sides is named as the pair ("key:ctrl", "key:shift", "key:alt"). Empty when nothing on that
// device triggers the action.
[[nodiscard]] std::string promptControl(const input::ActionMap& map, input::DeviceKind device,
                                        std::string_view action);

// The label of the control that triggers an action: promptControl, then controlLabel. Empty when
// nothing on that device triggers the action. The labels are words, never a maker's symbols (ADR
// 0014).
[[nodiscard]] std::string promptLabel(const input::ActionMap& map, input::DeviceKind device,
                                      platform::GamepadFamily family, std::string_view action,
                                      const localization::Catalog& texts);

// The control names of a key ("key:a", "key:f5", "key:enter") and of a gamepad button
// ("gamepad:south").
[[nodiscard]] std::string keyControl(platform::KeyCode key);
[[nodiscard]] std::string gamepadButtonControl(platform::GamepadButton button);

// The name of a family of gamepads in the labels and in the scripts: "xbox", "playstation",
// "nintendo", "steam", "generic".
[[nodiscard]] std::string_view gamepadFamilyName(platform::GamepadFamily family);

// --- Glyphs: a prompt drawn as the control itself (docs/architecture/ui.md, «Glifos de botones»).

// What a prompt's glyph draws: a key cap, a mouse button, one of the four face buttons, another
// gamepad button (Start, a shoulder, the d-pad), or a stick or trigger. None when nothing on the
// device in use triggers the action.
enum class GlyphKind : std::uint8_t { None, Key, Mouse, Face, Button, Stick };

// How a family of gamepads marks its four face buttons: letters (each family its own, read from
// the labels: some put A at the bottom, others on the right), four shapes (cross, circle, square
// and triangle: the original's pad), or nothing but their position for a gamepad the game does not
// know.
enum class FaceStyle : std::uint8_t { Letters, Shapes, Positions };

[[nodiscard]] FaceStyle faceStyleOf(platform::GamepadFamily family);

struct PromptGlyph {
  GlyphKind kind = GlyphKind::None;
  // The control (promptControl) and its label in the player's language (controlLabel).
  std::string control;
  std::string label;
  // For a face button: its family's style, its position ("south", "east", "west", "north") and,
  // in the shapes style, its shape ("cross", "circle", "square", "triangle").
  FaceStyle face = FaceStyle::Letters;
  std::string position;
  std::string shape;
  // What the drawn glyph (glyph_art.hpp) shows on its button or cap: a named symbol ("@space",
  // "@enter", "@cross", "@south", "@dpad_up") or a short text in capitals ("A", "ESC", "F10",
  // "LMB" for the left mouse button).
  std::string symbol;
};

// The glyph of the control that triggers an action on the device in use.
[[nodiscard]] PromptGlyph promptGlyph(const input::ActionMap& map, input::DeviceKind device,
                                      platform::GamepadFamily family, std::string_view action,
                                      const localization::Catalog& texts);

// The classes a prompt's element takes for its glyph, so a skin styles each kind its own way:
// "glyph", "glyph-key" (or -mouse, -face, -button, -stick, -none) and for a face button
// "family-letters" (or -shapes, -positions), "face-south" and "shape-cross".
[[nodiscard]] std::vector<std::string> promptClasses(const PromptGlyph& glyph);

// Whether a class is one promptClasses gives, to take the last glyph's off an element.
[[nodiscard]] bool isPromptClass(std::string_view name);

// The RML inside a prompt's element: the label (.glyph-label), or for a shape the original font's
// character (.glyph-char, the classic skin draws it) and the drawn shape (.glyph-art, the modern
// skin's), or for a position the drawn position. An icon (the classic box's animated confirm
// button) also carries the three drawings of its animation (.glyph-frame.frame-0 to -2), with the
// ids <id>-frame-0 to -2 when the element has an id.
[[nodiscard]] std::string promptMarkup(const PromptGlyph& glyph, bool icon,
                                       std::string_view id = {});

// A whole glyph inside a text: a span with the classes, the "key" class of the button prompts and
// the markup.
[[nodiscard]] std::string promptSpan(const PromptGlyph& glyph);

// The drawn glyph's size in the classic skin: its width in the original's pixels (a round button
// is 16, a cap grows with its text); every drawing is 12 tall. Zero for no glyph.
[[nodiscard]] int classicGlyphWidth(const PromptGlyph& glyph);
inline constexpr int ClassicGlyphHeight = 12;

// A text with the active device's glyphs in it, as RML: every {prompt:<action>} marker becomes the
// glyph of that action, the rest is escaped text. With `originalButtons` (a text of the original
// game), the original font's button characters and its words "X button" are markers too: the cross
// is `confirm`, the triangle `menu` (the original's game menu and its cancel), the circle `alt` and
// the square `extra`, the buttons the game binds them to. A character whose action has no control
// on the device stays as written; such a marker shows nothing.
[[nodiscard]] std::string promptText(std::string_view text, bool originalButtons,
                                     const std::function<PromptGlyph(std::string_view)>& glyphOf);

// The characters the original's font draws its pad's shapes with: the extracted texts write the
// cross's cell as "△" and the triangle's as "×" (font/main_meta.yaml, cells 77 and 78).
[[nodiscard]] std::string_view originalShapeCharacter(std::string_view shape);

} // namespace dwe::ui
