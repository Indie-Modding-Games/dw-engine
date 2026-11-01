#pragma once

#include <dwe/assets/indexed_image.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A sign overlay: the signs painted in a map's images that the engine writes again in the player's
// language (docs/modulos/engine/signs.md). The pack file `signs/<map>.yaml` holds the signs of a
// map for every language; `signs/<language>/<map>.yaml` replaces signs of the same id, adds signs
// only that language needs and can remove some. A sign names its text by a localization key, so the
// words live in the text tables with the rest of the translation.
namespace dwe::signs {

// A rectangle of pixels of a map image.
struct PixelRect {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;

  friend bool operator==(const PixelRect&, const PixelRect&) = default;
};

// A point of a map image, in pixels (fractions allowed).
struct PixelPoint {
  float x = 0.0F;
  float y = 0.0F;

  friend bool operator==(const PixelPoint&, const PixelPoint&) = default;
};

// Which image of the map a sign is painted in.
struct SignTarget {
  enum class Kind : std::uint8_t {
    // The background: one image for every time of day, each with its palette.
    Background,
    // A texture page of the map's sprites, as a sprite names it: its bits per pixel and its page
    // number (the sprite's texture X / 256).
    Page,
  };
  Kind kind = Kind::Background;
  std::int32_t bitsPerPixel = 8;
  std::int32_t page = 0;
  // The palette the sign's colours are matched against, as the sprite that shows it names it
  // (MapSprite::clut): -1, the default, is the background's palette of the day.
  std::int32_t clut = -1;

  friend bool operator==(const SignTarget&, const SignTarget&) = default;
};

// How the old text is removed before the new one is drawn.
struct SignClear {
  enum class Mode : std::uint8_t {
    // Each pixel mixes the pixels just outside the rectangle on its row and its column, by
    // distance: the board's shading is kept, the old letters go.
    Inpaint,
    // One colour: `color`, or the average of the pixels just outside the rectangle.
    Fill,
    // Nothing is removed: the text is drawn over the image.
    None,
    // Only the pixels that differ from the board's colour (`color`, or the commonest colour of the
    // area) by more than `tolerance` are removed, and filled from the board around them, ring by
    // ring: the board's frame and texture stay where they are.
    Diffuse,
  };
  Mode mode = Mode::Inpaint;
  // The rectangle cleared; the sign's region when missing.
  std::optional<PixelRect> rect;
  // For fill and diffuse, only the pixels inside this quadrilateral of the rectangle (a board seen
  // in perspective).
  std::optional<std::array<PixelPoint, 4>> quad;
  std::optional<assets::PaletteColor> color;
  // Diffuse: how far from the board's colour a pixel may be and still be board (0-255, by the
  // eye's weights), and how many pixels the removed letters grow by to take their soft edges.
  float tolerance = 40.0F;
  std::int32_t grow = 1;

  friend bool operator==(const SignClear&, const SignClear&) = default;
};

// A line drawn around the letters.
struct SignOutline {
  assets::PaletteColor color{};
  // In pixels of the image.
  float width = 1.0F;

  friend bool operator==(const SignOutline&, const SignOutline&) = default;
};

// A soft halo around the letters (a neon's light).
struct SignGlow {
  assets::PaletteColor color{};
  // In pixels of the image.
  float radius = 2.0F;
  // How strong the halo is at the letters, 0 to 1.
  float strength = 0.8F;

  friend bool operator==(const SignGlow&, const SignGlow&) = default;
};

enum class HorizontalAlign : std::uint8_t { Left, Center, Right };
enum class VerticalAlign : std::uint8_t { Top, Middle, Bottom };

// Which palette colours the painted pixels may take.
enum class SignColors : std::uint8_t {
  // Any opaque colour of the palette.
  All,
  // Only colours the original already uses in the cleared rectangle and around it: the sign keeps
  // its look at every time of day.
  Region,
};

struct Sign {
  std::string id;
  SignTarget target;
  // The pixels the sign may change; the text box when no corners are given.
  PixelRect region;
  // Where the corners of the text box land (top left, top right, bottom right, bottom left), for a
  // sign seen in perspective; the drawing stays inside region.
  std::optional<std::array<PixelPoint, 4>> corners;
  // The text box's size in pixels when it is not region's (a rotated sign); with corners, its
  // proportions.
  std::optional<PixelPoint> box;
  SignClear clear;
  // The localization key of the text; a line break in the text starts a new line.
  std::string text;
  // A font of ui/fonts/ (its asset id's last part, such as noto_sans_bold).
  std::string font = "noto_sans_bold";
  // The height of the font in pixels of the image.
  float size = 10.0F;
  // Extra pixels between letters and a factor on the line height.
  float letterSpacing = 0.0F;
  float lineSpacing = 1.0F;
  HorizontalAlign align = HorizontalAlign::Center;
  VerticalAlign valign = VerticalAlign::Middle;
  // Degrees, clockwise, about the region's centre.
  float rotation = 0.0F;
  // Degrees the letters lean to the right.
  float skew = 0.0F;
  // One letter under another (a vertical banner).
  bool vertical = false;
  // Shrinks the text until it fits the box.
  bool fit = true;
  assets::PaletteColor color{.red = 255, .green = 255, .blue = 255, .alpha = 255};
  std::optional<SignOutline> outline;
  std::optional<SignGlow> glow;
  SignColors colors = SignColors::All;

  friend bool operator==(const Sign&, const Sign&) = default;
};

struct SignOverlay {
  std::vector<Sign> signs;
  // Ids of signs of the overlay below that this one takes out (a language whose original already
  // reads right).
  std::vector<std::string> remove;
};

// Reads an overlay document (content/schemas/sign_overlay.schema.json).
core::Result<SignOverlay> parseSignOverlay(std::string_view yaml);

// The overlay a language gets: `common`'s signs with `language`'s laid over them by id, minus the
// ones `language` removes.
SignOverlay mergeSignOverlays(SignOverlay common, const SignOverlay& language);

// A colour written "#rrggbb" or "#rrggbbaa".
std::optional<assets::PaletteColor> parseSignColor(std::string_view text);

} // namespace dwe::signs
