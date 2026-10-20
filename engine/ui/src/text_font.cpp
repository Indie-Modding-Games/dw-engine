#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ui/text_font.hpp>

#include <utility>

#include "messages.hpp"

namespace dwe::ui {
namespace {

constexpr std::string_view ModernName = "modern";
constexpr std::string_view OriginalName = "original";

} // namespace

std::string_view textFontName(TextFont font) {
    return font == TextFont::Original ? OriginalName : ModernName;
}

std::optional<TextFont> textFontNamed(std::string_view name) {
    if (name == ModernName) {
        return TextFont::Modern;
    }
    if (name == OriginalName) {
        return TextFont::Original;
    }
    return std::nullopt;
}

core::Result<OriginalLook> parseOriginalLook(std::string_view yaml) {
    auto tree = core::parseYaml(yaml);
    if (!tree) {
        return std::unexpected{tree.error()};
    }
    assets::YamlReader reader;
    OriginalLook look;
    reader.expectKeys(*tree, "original_look", {"family", "default", "modern", "original"});
    look.family = reader.text(reader.field(*tree, "family"));
    const core::YamlNode& choice = reader.field(*tree, "default");
    if (const auto font = textFontNamed(reader.text(choice))) {
        look.defaultFont = *font;
    } else {
        reader.fail(choice, messages::UnknownTextFont);
    }
    look.modernFamily = reader.text(reader.field(*tree, "modern"));
    const core::YamlNode& original = reader.field(*tree, "original");
    reader.expectKeys(original, "original", {"atlas", "meta", "baseline", "fallback_size"});
    look.atlas = reader.text(reader.field(original, "atlas"));
    look.meta = reader.text(reader.field(original, "meta"));
    look.baseline = reader.integer<int>(reader.field(original, "baseline"));
    const core::YamlNode& fallbackSize = reader.field(original, "fallback_size");
    look.fallbackSize = reader.number(fallbackSize);
    if (reader.ok() && !(look.fallbackSize > 0.0F)) {
        reader.fail(fallbackSize, messages::FallbackSizeNotPositive);
    }
    return reader.finish(std::move(look), messages::OriginalLookContext);
}

} // namespace dwe::ui
