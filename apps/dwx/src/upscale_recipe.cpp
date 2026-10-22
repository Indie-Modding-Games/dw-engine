#include "upscale_recipe.hpp"

#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "messages.hpp"

namespace dwe::dwx {

namespace {

constexpr std::string_view RecipeContext = "upscale recipe";
// The one built-in step this subset runs.
constexpr std::string_view IndexScaleStep = "index_scale";
// The asset types of the remaster recipe this subset accepts: images drawn by the UI.
constexpr std::array<std::string_view, 2> UpscaleAssetTypes{"ui", "icon"};
constexpr std::size_t MaxRecipeIdLength = 64;

bool isRecipeId(std::string_view id) {
    return !id.empty() && id.size() <= MaxRecipeIdLength && std::ranges::all_of(id, [](char letter) {
        return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '_';
    });
}

std::vector<assets::AssetId> readImages(assets::YamlReader& reader, const core::YamlNode& select) {
    reader.expectKeys(select, "select", {"include"}, {"exclude"});
    std::vector<assets::AssetId> images;
    for (const core::YamlNode& item : reader.sequence(reader.field(select, "include"), "select.include")) {
        auto id = assets::AssetId::parse(reader.text(item));
        if (!id) {
            reader.fail(item, id.error().message);
            continue;
        }
        if (std::ranges::contains(images, *id)) {
            reader.fail(item, core::formatMessage(messages::UpscaleImageTwice, id->str()));
            continue;
        }
        images.push_back(std::move(*id));
    }
    if (images.empty() && reader.ok()) {
        reader.fail(select, messages::UpscaleNoImages);
    }
    return images;
}

// The mode of the recipe's only step, which must be index_scale.
IndexScaleMode readStep(assets::YamlReader& reader, const core::YamlNode& steps, std::uint32_t scale) {
    const std::span<const core::YamlNode> items = reader.sequence(steps, "steps", 1);
    if (items.empty()) {
        return IndexScaleMode::Edge;
    }
    const core::YamlNode& step = items.front();
    reader.expectKeys(step, "steps[0]", {"id", "builtin"}, {"scale", "params"});
    if (reader.text(reader.field(step, "builtin")) != IndexScaleStep && reader.ok()) {
        reader.fail(step, core::formatMessage(messages::UpscaleOnlyIndexScale, IndexScaleStep));
    }
    if (const std::optional<std::uint32_t> stepScale = reader.optionalInteger<std::uint32_t>(step, "scale");
        stepScale && *stepScale != scale) {
        reader.fail(step, core::formatMessage(messages::UpscaleStepScale, *stepScale, scale));
    }
    IndexScaleMode mode = IndexScaleMode::Edge;
    if (const core::YamlNode* params = step.find("params"); params != nullptr) {
        reader.expectKeys(*params, "steps[0].params", {}, {"mode"});
        if (const core::YamlNode* named = params->find("mode"); named != nullptr) {
            const std::optional<IndexScaleMode> found = indexScaleModeNamed(reader.text(*named));
            if (!found) {
                reader.fail(*named, messages::UpscaleUnknownMode);
            }
            mode = found.value_or(IndexScaleMode::Edge);
        }
    }
    return mode;
}

} // namespace

core::Result<UpscaleRecipe> parseUpscaleRecipe(std::string_view yaml) {
    auto tree = core::parseYaml(yaml);
    if (!tree) {
        return std::unexpected{tree.error()};
    }
    assets::YamlReader reader;
    reader.expectKeys(*tree, RecipeContext, {"recipe", "id", "asset_type", "select", "scale", "steps"});
    if (reader.integer<std::int64_t>(reader.field(*tree, "recipe")) != UpscaleRecipeVersion && reader.ok()) {
        reader.fail(*tree, core::formatMessage(messages::UpscaleRecipeVersion, UpscaleRecipeVersion));
    }
    UpscaleRecipe recipe;
    recipe.id = reader.text(reader.field(*tree, "id"));
    if (!isRecipeId(recipe.id) && reader.ok()) {
        reader.fail(reader.field(*tree, "id"), messages::UpscaleBadId);
    }
    if (!std::ranges::contains(UpscaleAssetTypes, reader.text(reader.field(*tree, "asset_type"))) && reader.ok()) {
        reader.fail(reader.field(*tree, "asset_type"), messages::UpscaleAssetType);
    }
    recipe.images = readImages(reader, reader.field(*tree, "select"));
    recipe.scale = reader.integer<std::uint32_t>(reader.field(*tree, "scale"));
    recipe.mode = readStep(reader, reader.field(*tree, "steps"), recipe.scale);
    if (reader.ok() && !supportsIndexScale(recipe.mode, recipe.scale)) {
        reader.fail(reader.field(*tree, "scale"), core::formatMessage(messages::UpscaleScaleUnsupported, recipe.scale));
    }
    return reader.finish(std::move(recipe), RecipeContext);
}

} // namespace dwe::dwx
