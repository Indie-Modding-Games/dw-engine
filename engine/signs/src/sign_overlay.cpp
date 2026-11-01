#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/signs/sign_overlay.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <span>
#include <utility>

namespace dwe::signs {
namespace {

constexpr std::string_view BackgroundTarget = "background";

template <typename T>
struct Named {
  std::string_view name;
  T value;
};

template <typename T>
T readNamed(assets::YamlReader& reader, const core::YamlNode& node, std::string_view what,
            std::initializer_list<Named<T>> names, T fallback) {
  const std::string text = reader.text(node);
  for (const Named<T>& named : names) {
    if (named.name == text) {
      return named.value;
    }
  }
  if (reader.ok()) {
    std::string known;
    for (const Named<T>& named : names) {
      known += known.empty() ? "" : ", ";
      known += named.name;
    }
    reader.fail(node, std::format("{} must be one of {}", what, known));
  }
  return fallback;
}

assets::PaletteColor readColor(assets::YamlReader& reader, const core::YamlNode& node) {
  const std::string text = reader.text(node);
  const auto color = parseSignColor(text);
  if (!color) {
    reader.fail(node, "a colour is written #rrggbb or #rrggbbaa");
    return {};
  }
  return *color;
}

PixelRect readRect(assets::YamlReader& reader, const core::YamlNode& node) {
  const auto items = reader.sequence(node, "a rectangle [x, y, width, height]", 4);
  if (items.size() != 4) {
    return {};
  }
  PixelRect rect{
      .x = reader.integer<std::int32_t>(items[0]),
      .y = reader.integer<std::int32_t>(items[1]),
      .width = reader.integer<std::int32_t>(items[2]),
      .height = reader.integer<std::int32_t>(items[3]),
  };
  if (reader.ok() && (rect.width <= 0 || rect.height <= 0)) {
    reader.fail(node, "a rectangle needs a width and a height above 0");
  }
  return rect;
}

PixelPoint readPoint(assets::YamlReader& reader, const core::YamlNode& node) {
  const auto items = reader.sequence(node, "a point [x, y]", 2);
  if (items.size() != 2) {
    return {};
  }
  return {.x = reader.number(items[0]), .y = reader.number(items[1])};
}

std::array<PixelPoint, 4> readQuad(assets::YamlReader& reader, const core::YamlNode& node,
                                   std::string_view what) {
  std::array<PixelPoint, 4> read{};
  const auto points = reader.sequence(node, what, 4);
  if (points.size() == 4) {
    for (std::size_t index = 0; index < read.size(); ++index) {
      read.at(index) = readPoint(reader, points[index]);
    }
  }
  return read;
}

SignTarget readTarget(assets::YamlReader& reader, const core::YamlNode& node) {
  if (node.isScalar()) {
    if (reader.text(node) != BackgroundTarget) {
      reader.fail(node, "target is background or a mapping {page, bits, clut}");
    }
    return {};
  }
  if (!reader.expectKeys(node, "target", {"page"}, {"bits", "clut"})) {
    return {};
  }
  SignTarget target{.kind = SignTarget::Kind::Page,
                    .bitsPerPixel = 8,
                    .page = reader.integer<std::int32_t>(reader.field(node, "page")),
                    .clut = -1};
  if (const core::YamlNode* bits = node.find("bits")) {
    target.bitsPerPixel = reader.integer<std::int32_t>(*bits);
    if (reader.ok() && target.bitsPerPixel != 4 && target.bitsPerPixel != 8) {
      reader.fail(*bits, "bits is 4 or 8");
    }
  }
  if (const core::YamlNode* clut = node.find("clut")) {
    target.clut = reader.integer<std::int32_t>(*clut);
  }
  if (reader.ok() && target.page < 0) {
    reader.fail(node, "page is 0 or more");
  }
  return target;
}

SignClear readClear(assets::YamlReader& reader, const core::YamlNode& node) {
  static constexpr std::initializer_list<Named<SignClear::Mode>> Modes{
      {.name = "inpaint", .value = SignClear::Mode::Inpaint},
      {.name = "fill", .value = SignClear::Mode::Fill},
      {.name = "none", .value = SignClear::Mode::None},
      {.name = "diffuse", .value = SignClear::Mode::Diffuse},
  };
  if (node.isScalar()) {
    SignClear clear;
    clear.mode = readNamed(reader, node, "clear", Modes, SignClear::Mode::Inpaint);
    return clear;
  }
  if (!reader.expectKeys(node, "clear", {"mode"}, {"rect", "quad", "color", "tolerance", "grow"})) {
    return {};
  }
  SignClear clear;
  clear.mode =
      readNamed(reader, reader.field(node, "mode"), "clear.mode", Modes, SignClear::Mode::Inpaint);
  if (const core::YamlNode* rect = node.find("rect")) {
    clear.rect = readRect(reader, *rect);
  }
  if (const core::YamlNode* quad = node.find("quad")) {
    clear.quad = readQuad(reader, *quad, "clear.quad (four points)");
  }
  if (const core::YamlNode* color = node.find("color")) {
    clear.color = readColor(reader, *color);
  }
  if (const core::YamlNode* tolerance = node.find("tolerance")) {
    clear.tolerance = reader.number(*tolerance);
  }
  if (const core::YamlNode* grow = node.find("grow")) {
    clear.grow = reader.integer<std::int32_t>(*grow);
    if (reader.ok() && (clear.grow < 0 || clear.grow > 8)) {
      reader.fail(*grow, "clear.grow is 0 to 8");
    }
  }
  return clear;
}

float readPositive(assets::YamlReader& reader, const core::YamlNode& node, std::string_view what) {
  const float value = reader.number(node);
  if (reader.ok() && value <= 0.0F) {
    reader.fail(node, std::format("{} must be above 0", what));
  }
  return value;
}

Sign readSign(assets::YamlReader& reader, const core::YamlNode& node) {
  Sign sign;
  if (!reader.expectKeys(node, "a sign", {"id", "region", "text"},
                         {"target", "corners", "box", "clear", "font", "size", "letter_spacing",
                          "line_spacing", "align", "valign", "rotation", "skew", "vertical", "fit",
                          "color", "outline", "glow", "colors"})) {
    return sign;
  }
  sign.id = reader.text(reader.field(node, "id"));
  sign.region = readRect(reader, reader.field(node, "region"));
  sign.text = reader.text(reader.field(node, "text"));
  if (const core::YamlNode* target = node.find("target")) {
    sign.target = readTarget(reader, *target);
  }
  if (const core::YamlNode* corners = node.find("corners")) {
    sign.corners = readQuad(reader, *corners, "corners (four points)");
  }
  if (const core::YamlNode* box = node.find("box")) {
    const PixelPoint size = readPoint(reader, *box);
    if (reader.ok() && (size.x <= 0.0F || size.y <= 0.0F)) {
      reader.fail(*box, "box needs a width and a height above 0");
    }
    sign.box = size;
  }
  if (const core::YamlNode* clear = node.find("clear")) {
    sign.clear = readClear(reader, *clear);
  }
  if (const core::YamlNode* font = node.find("font")) {
    sign.font = reader.text(*font);
  }
  if (const core::YamlNode* size = node.find("size")) {
    sign.size = readPositive(reader, *size, "size");
  }
  if (const core::YamlNode* spacing = node.find("letter_spacing")) {
    sign.letterSpacing = reader.number(*spacing);
  }
  if (const core::YamlNode* spacing = node.find("line_spacing")) {
    sign.lineSpacing = readPositive(reader, *spacing, "line_spacing");
  }
  if (const core::YamlNode* align = node.find("align")) {
    sign.align = readNamed(reader, *align, "align",
                           {{.name = "left", .value = HorizontalAlign::Left},
                            {.name = "center", .value = HorizontalAlign::Center},
                            {.name = "right", .value = HorizontalAlign::Right}},
                           HorizontalAlign::Center);
  }
  if (const core::YamlNode* align = node.find("valign")) {
    sign.valign = readNamed(reader, *align, "valign",
                            {{.name = "top", .value = VerticalAlign::Top},
                             {.name = "middle", .value = VerticalAlign::Middle},
                             {.name = "bottom", .value = VerticalAlign::Bottom}},
                            VerticalAlign::Middle);
  }
  if (const core::YamlNode* rotation = node.find("rotation")) {
    sign.rotation = reader.number(*rotation);
  }
  if (const core::YamlNode* skew = node.find("skew")) {
    sign.skew = reader.number(*skew);
    if (reader.ok() && (sign.skew <= -80.0F || sign.skew >= 80.0F)) {
      reader.fail(*skew, "skew is between -80 and 80 degrees");
    }
  }
  if (const core::YamlNode* vertical = node.find("vertical")) {
    sign.vertical = reader.boolean(*vertical);
  }
  if (const core::YamlNode* fit = node.find("fit")) {
    sign.fit = reader.boolean(*fit);
  }
  if (const core::YamlNode* color = node.find("color")) {
    sign.color = readColor(reader, *color);
  }
  if (const core::YamlNode* outline = node.find("outline")) {
    if (reader.expectKeys(*outline, "outline", {"color"}, {"width"})) {
      SignOutline read;
      read.color = readColor(reader, reader.field(*outline, "color"));
      if (const core::YamlNode* width = outline->find("width")) {
        read.width = readPositive(reader, *width, "outline.width");
      }
      sign.outline = read;
    }
  }
  if (const core::YamlNode* glow = node.find("glow")) {
    if (reader.expectKeys(*glow, "glow", {"color"}, {"radius", "strength"})) {
      SignGlow read;
      read.color = readColor(reader, reader.field(*glow, "color"));
      if (const core::YamlNode* radius = glow->find("radius")) {
        read.radius = readPositive(reader, *radius, "glow.radius");
      }
      if (const core::YamlNode* strength = glow->find("strength")) {
        read.strength = std::clamp(reader.number(*strength), 0.0F, 1.0F);
      }
      sign.glow = read;
    }
  }
  if (const core::YamlNode* colors = node.find("colors")) {
    sign.colors = readNamed(reader, *colors, "colors",
                            {{.name = "all", .value = SignColors::All},
                             {.name = "region", .value = SignColors::Region}},
                            SignColors::All);
  }
  return sign;
}

std::optional<std::uint8_t> hexByte(std::string_view digits) {
  std::uint8_t value = 0;
  const auto [end, error] =
      std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
  if (error != std::errc{} || end != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return value;
}

} // namespace

std::optional<assets::PaletteColor> parseSignColor(std::string_view text) {
  if (!text.starts_with('#') || (text.size() != 7 && text.size() != 9)) {
    return std::nullopt;
  }
  std::array<std::uint8_t, 4> channels{0, 0, 0, 255};
  for (std::size_t channel = 0; (channel * 2) + 1 < text.size(); ++channel) {
    const auto value = hexByte(text.substr(1 + (channel * 2), 2));
    if (!value) {
      return std::nullopt;
    }
    channels.at(channel) = *value;
  }
  return assets::PaletteColor{
      .red = channels[0], .green = channels[1], .blue = channels[2], .alpha = channels[3]};
}

core::Result<SignOverlay> parseSignOverlay(std::string_view yaml) {
  auto document = core::parseYaml(yaml);
  if (!document) {
    return std::unexpected{document.error()};
  }
  assets::YamlReader reader;
  SignOverlay overlay;
  if (reader.expectKeys(*document, "a sign overlay", {}, {"signs", "remove"})) {
    if (const core::YamlNode* signs = document->find("signs")) {
      for (const core::YamlNode& node : reader.sequence(*signs, "signs")) {
        Sign sign = readSign(reader, node);
        if (reader.ok() && std::ranges::any_of(overlay.signs, [&](const Sign& other) {
              return other.id == sign.id;
            })) {
          reader.fail(node, std::format("two signs are called {}", sign.id));
        }
        overlay.signs.push_back(std::move(sign));
      }
    }
    if (const core::YamlNode* remove = document->find("remove")) {
      for (const core::YamlNode& id : reader.sequence(*remove, "remove")) {
        overlay.remove.push_back(reader.text(id));
      }
    }
  }
  return reader.finish(std::move(overlay), "sign overlay");
}

SignOverlay mergeSignOverlays(SignOverlay common, const SignOverlay& language) {
  std::erase_if(common.signs,
                [&](const Sign& sign) { return std::ranges::contains(language.remove, sign.id); });
  for (const Sign& sign : language.signs) {
    const auto same =
        std::ranges::find_if(common.signs, [&](const Sign& other) { return other.id == sign.id; });
    if (same != common.signs.end()) {
      *same = sign;
    } else {
      common.signs.push_back(sign);
    }
  }
  common.remove.clear();
  return common;
}

} // namespace dwe::signs
