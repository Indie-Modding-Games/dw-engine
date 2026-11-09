#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ui/skin.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>

#include "messages.hpp"

namespace dwe::ui {
namespace {

constexpr std::size_t MaxNameLength = 64;
constexpr std::size_t RectangleValues = 4;
constexpr std::size_t SizeValues = 2;

// A skin id, a screen name: lowercase letters, digits and underscores, as the asset ids they become
// part of.
bool isPlainName(std::string_view name) {
  return !name.empty() && name.size() <= MaxNameLength && std::ranges::all_of(name, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

// A name the style sheets use (a sprite sheet, a sprite): an RCSS identifier.
bool isStyleName(std::string_view name) {
  return !name.empty() && name.size() <= MaxNameLength && name.front() >= 'a' &&
         name.front() <= 'z' && std::ranges::all_of(name, [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
         });
}

std::string plainName(assets::YamlReader& reader, const core::YamlNode& node) {
  std::string name = reader.text(node);
  if (reader.ok() && !isPlainName(name)) {
    reader.fail(node, messages::BadSkinName);
  }
  return name;
}

std::string styleName(assets::YamlReader& reader, const core::YamlNode& node) {
  std::string name = reader.text(node);
  if (reader.ok() && !isStyleName(name)) {
    reader.fail(node, messages::BadStyleName);
  }
  return name;
}

std::optional<assets::AssetId> assetId(assets::YamlReader& reader, const core::YamlNode& node) {
  auto id = assets::AssetId::parse(reader.text(node));
  if (!id) {
    reader.fail(node, id.error().message);
    return std::nullopt;
  }
  return std::move(*id);
}

std::vector<assets::AssetId> assetIds(assets::YamlReader& reader, const core::YamlNode& node,
                                      std::string_view what) {
  std::vector<assets::AssetId> ids;
  for (const core::YamlNode& item : reader.sequence(node, what)) {
    if (auto id = assetId(reader, item)) {
      ids.push_back(std::move(*id));
    }
  }
  return ids;
}

SkinScreen readScreen(assets::YamlReader& reader, const core::YamlNode& node) {
  SkinScreen screen;
  reader.expectKeys(node, messages::SkinScreenWhat, {}, {"styles", "document"});
  if (const core::YamlNode* styles = node.find("styles")) {
    screen.styles = assetIds(reader, *styles, messages::SkinStylesWhat);
  }
  if (const core::YamlNode* document = node.find("document")) {
    screen.document = assetId(reader, *document);
  }
  return screen;
}

SkinSprite readSprite(assets::YamlReader& reader, const core::YamlEntry& entry) {
  const auto values = reader.sequence(entry.value, messages::SpriteRectangleWhat, RectangleValues);
  SkinSprite sprite{.name = entry.key, .x = 0, .y = 0, .width = 0, .height = 0};
  if (!isStyleName(entry.key)) {
    reader.fail(entry.value, messages::BadStyleName);
  }
  if (values.size() == RectangleValues) {
    sprite.x = reader.integer<std::uint32_t>(values[0]);
    sprite.y = reader.integer<std::uint32_t>(values[1]);
    sprite.width = reader.integer<std::uint32_t>(values[2]);
    sprite.height = reader.integer<std::uint32_t>(values[3]);
  }
  if (reader.ok() && (sprite.width == 0 || sprite.height == 0)) {
    reader.fail(entry.value, messages::EmptySprite);
  }
  return sprite;
}

void readOriginalSize(assets::YamlReader& reader, const core::YamlNode& node,
                      SkinSpriteSheet& sheet) {
  const auto values = reader.sequence(node, messages::OriginalSizeWhat, SizeValues);
  if (values.size() != SizeValues) {
    return;
  }
  sheet.originalWidth = reader.integer<std::uint32_t>(values[0]);
  sheet.originalHeight = reader.integer<std::uint32_t>(values[1]);
  if (reader.ok() && (*sheet.originalWidth == 0 || *sheet.originalHeight == 0)) {
    reader.fail(node, messages::EmptyOriginalSize);
  }
}

// The columns mirrored past the image's left and right edges (ImageReference::mirrorLeft).
void readMirrorSides(assets::YamlReader& reader, const core::YamlNode& node,
                     SkinSpriteSheet& sheet) {
  const auto values = reader.sequence(node, messages::MirrorSidesWhat, SizeValues);
  if (values.size() != SizeValues) {
    return;
  }
  const auto left = reader.integer<std::uint32_t>(values[0]);
  const auto right = reader.integer<std::uint32_t>(values[1]);
  if (reader.ok() && (left > MaxMirroredColumns || right > MaxMirroredColumns)) {
    reader.fail(node, core::formatMessage(messages::TooManyMirroredColumns, MaxMirroredColumns));
    return;
  }
  sheet.image.mirrorLeft = left;
  sheet.image.mirrorRight = right;
}

std::optional<SkinSpriteSheet> readSpriteSheet(assets::YamlReader& reader,
                                               const core::YamlNode& node) {
  reader.expectKeys(node, messages::SpriteSheetWhat, {"name", "image", "sprites"},
                    {"palette", "original_size", "dp_per_pixel", "upscale", "mirror_sides"});
  std::string name = styleName(reader, reader.field(node, "name"));
  std::optional<assets::AssetId> image = assetId(reader, reader.field(node, "image"));
  if (!image) {
    return std::nullopt;
  }
  SkinSpriteSheet sheet{.name = std::move(name),
                        .image = {.asset = std::move(*image), .palette = std::nullopt},
                        .originalWidth = std::nullopt,
                        .originalHeight = std::nullopt,
                        .dpPerPixel = DefaultDpPerPixel,
                        .sprites = {}};
  if (const core::YamlNode* palette = node.find("palette")) {
    sheet.image.palette = reader.integer<std::uint32_t>(*palette);
  }
  if (const core::YamlNode* upscale = node.find("upscale")) {
    const std::string mode = reader.text(*upscale);
    if (reader.ok() && mode != SmoothUpscaleName) {
      reader.fail(*upscale, core::formatMessage(messages::UnknownSheetUpscale, mode));
    }
    sheet.smooth = reader.ok();
  }
  if (const core::YamlNode* sides = node.find("mirror_sides")) {
    readMirrorSides(reader, *sides, sheet);
  }
  if (const core::YamlNode* size = node.find("original_size")) {
    readOriginalSize(reader, *size, sheet);
  }
  if (const core::YamlNode* scale = node.find("dp_per_pixel")) {
    sheet.dpPerPixel = reader.number(*scale);
    if (reader.ok() && (!std::isfinite(sheet.dpPerPixel) || sheet.dpPerPixel < MinDpPerPixel ||
                        sheet.dpPerPixel > MaxDpPerPixel)) {
      reader.fail(*scale, core::formatMessage(messages::DpPerPixelOutOfRange, MinDpPerPixel,
                                              MaxDpPerPixel));
    }
  }
  const core::YamlNode& sprites = reader.field(node, "sprites");
  if (reader.ok() && (!sprites.isMapping() || sprites.entries().empty())) {
    reader.fail(sprites, messages::NoSprites);
  }
  for (const core::YamlEntry& entry :
       reader.ok() ? sprites.entries() : std::span<const core::YamlEntry>{}) {
    sheet.sprites.push_back(readSprite(reader, entry));
  }
  return sheet;
}

// An element id of a document: what RML accepts in id="..." and the scripts look up.
bool isElementId(std::string_view id) {
  return !id.empty() && id.size() <= MaxNameLength && std::ranges::all_of(id, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
  });
}

SkinElementClasses readElementClasses(assets::YamlReader& reader, const core::YamlNode& node) {
  SkinElementClasses classes;
  if (!node.isMapping()) {
    reader.fail(node, messages::DocumentClassesNotMapping);
    return classes;
  }
  for (const core::YamlEntry& entry : node.entries()) {
    if (!isElementId(entry.key)) {
      reader.fail(entry.value, messages::BadElementId);
    }
    std::vector<std::string>& names = classes[entry.key];
    const std::span<const core::YamlNode> items =
        reader.sequence(entry.value, messages::ElementClassesWhat);
    if (reader.ok() && items.empty()) {
      reader.fail(entry.value, messages::NoElementClasses);
    }
    for (const core::YamlNode& item : items) {
      std::string name = styleName(reader, item);
      if (!std::ranges::contains(names, name)) {
        names.push_back(std::move(name));
      }
    }
  }
  return classes;
}

void readDocumentClasses(assets::YamlReader& reader, const core::YamlNode& node, Skin& skin) {
  if (!node.isMapping()) {
    reader.fail(node, messages::DocumentClassesNotMapping);
    return;
  }
  for (const core::YamlEntry& entry : node.entries()) {
    if (!isPlainName(entry.key)) {
      reader.fail(entry.value, messages::BadSkinName);
    }
    skin.documentClasses.insert_or_assign(entry.key, readElementClasses(reader, entry.value));
  }
}

void readScreens(assets::YamlReader& reader, const core::YamlNode& node, Skin& skin) {
  if (!node.isMapping()) {
    reader.fail(node, messages::ScreensNotMapping);
    return;
  }
  for (const core::YamlEntry& entry : node.entries()) {
    if (!isPlainName(entry.key)) {
      reader.fail(entry.value, messages::BadSkinName);
    }
    skin.screens.insert_or_assign(entry.key, readScreen(reader, entry.value));
  }
}

} // namespace

core::Result<Skin> parseSkin(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  assets::YamlReader reader;
  Skin skin;
  reader.expectKeys(*tree, messages::SkinWhat, {"id", "name"},
                    {"extends", "theme", "screens", "sprite_sheets", "document_classes"});
  skin.id = plainName(reader, reader.field(*tree, "id"));
  skin.nameKey = reader.text(reader.field(*tree, "name"));
  if (const core::YamlNode* extends = tree->find("extends")) {
    skin.extends = plainName(reader, *extends);
  }
  if (const core::YamlNode* theme = tree->find("theme")) {
    skin.theme = assetIds(reader, *theme, messages::SkinThemeWhat);
  }
  if (const core::YamlNode* screens = tree->find("screens")) {
    readScreens(reader, *screens, skin);
  }
  if (const core::YamlNode* sheets = tree->find("sprite_sheets")) {
    for (const core::YamlNode& sheet : reader.sequence(*sheets, messages::SpriteSheetsWhat)) {
      if (auto read = readSpriteSheet(reader, sheet)) {
        skin.spriteSheets.push_back(std::move(*read));
      }
    }
  }
  if (const core::YamlNode* classes = tree->find("document_classes")) {
    readDocumentClasses(reader, *classes, skin);
  }
  return reader.finish(std::move(skin), messages::SkinContext);
}

core::Result<SkinChoice> parseSkinChoice(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  assets::YamlReader reader;
  reader.expectKeys(*tree, messages::SkinChoiceContext, {"default"}, {"fallback"});
  SkinChoice choice{.preferred = plainName(reader, reader.field(*tree, "default")),
                    .fallback = std::nullopt};
  if (const core::YamlNode* fallback = tree->find("fallback")) {
    choice.fallback = plainName(reader, *fallback);
  }
  return reader.finish(std::move(choice), messages::SkinChoiceContext);
}

} // namespace dwe::ui
