#include "graphics_options.hpp"

#include <dwe/core/log.hpp>

#include <ranges>
#include <utility>
#include <variant>

#include "messages.hpp"

namespace dwe::app {

namespace {

constexpr std::string_view ShadowsOff = "off";
constexpr std::string_view ShadowsRound = "round";
constexpr std::string_view ShadowsSun = "sun";

constexpr std::string_view QualitySmooth = "smooth";
constexpr std::string_view QualityUpscaled = "upscaled";
constexpr std::string_view FilterNearest = "nearest";

std::optional<bool> onOff(std::string_view value) {
  if (value == "on") {
    return true;
  }
  if (value == "off") {
    return false;
  }
  return std::nullopt;
}

} // namespace

std::optional<ShadowMode> parseShadowMode(std::string_view name) {
  if (name == ShadowsOff) {
    return ShadowMode::Off;
  }
  if (name == ShadowsRound) {
    return ShadowMode::Round;
  }
  if (name == ShadowsSun) {
    return ShadowMode::Sun;
  }
  return std::nullopt;
}

std::string_view shadowModeName(ShadowMode mode) {
  switch (mode) {
  case ShadowMode::Off:
    return ShadowsOff;
  case ShadowMode::Round:
    return ShadowsRound;
  case ShadowMode::Sun:
    return ShadowsSun;
  }
  return ShadowsSun;
}

std::optional<SceneryQuality> parseSceneryQuality(std::string_view name) {
  if (name == QualitySmooth) {
    return SceneryQuality::Smooth;
  }
  if (name == QualityUpscaled) {
    return SceneryQuality::Upscaled;
  }
  return std::nullopt;
}

std::string_view sceneryQualityName(SceneryQuality quality) {
  return quality == SceneryQuality::Smooth ? QualitySmooth : QualityUpscaled;
}

render::TextureFilter renderTextureFilter(TextureFilter filter) {
  switch (filter) {
  case TextureFilter::Nearest:
    return render::TextureFilter::Nearest;
  case TextureFilter::Smooth:
    return render::TextureFilter::Smooth;
  case TextureFilter::Upscaled:
    return render::TextureFilter::Upscaled;
  }
  return render::TextureFilter::Upscaled;
}

void applyTextureFilter(render::ModelRenderer& models, const GraphicsOptions& options) {
  models.setTextureFilter(renderTextureFilter(options.textureFilter));
}

void prepareModels(render::ModelRenderer& models, const GraphicsOptions& options,
                   std::shared_ptr<const render::UpscaleCache> cache) {
  models.setUpscaleCache(std::move(cache));
  applyTextureFilter(models, options);
}

std::optional<TextureFilter> parseTextureFilter(std::string_view name) {
  if (name == FilterNearest) {
    return TextureFilter::Nearest;
  }
  if (name == QualitySmooth) {
    return TextureFilter::Smooth;
  }
  if (name == QualityUpscaled) {
    return TextureFilter::Upscaled;
  }
  return std::nullopt;
}

std::string_view textureFilterName(TextureFilter filter) {
  switch (filter) {
  case TextureFilter::Nearest:
    return FilterNearest;
  case TextureFilter::Smooth:
    return QualitySmooth;
  case TextureFilter::Upscaled:
    return QualityUpscaled;
  }
  return QualityUpscaled;
}

GraphicsOptions startGraphicsOptions(const UserSettings& settings) {
  GraphicsOptions options;
  if (settings.shadows) {
    if (const auto mode = parseShadowMode(*settings.shadows)) {
      options.shadows = *mode;
    } else {
      core::logWarning(messages::UnknownSavedShadows, *settings.shadows);
    }
  }
  options.enhancedLighting = settings.enhancedLighting.value_or(true);
  options.vividColors = settings.vividColors.value_or(true);
  if (settings.sceneryQuality) {
    if (const auto quality = parseSceneryQuality(*settings.sceneryQuality)) {
      options.sceneryQuality = *quality;
    } else {
      core::logWarning(messages::UnknownSavedGraphicsValue, SceneryQualitySetting,
                       *settings.sceneryQuality);
    }
  }
  options.antialiasing = settings.antialiasing.value_or(true);
  if (settings.textureFilter) {
    if (const auto filter = parseTextureFilter(*settings.textureFilter)) {
      options.textureFilter = *filter;
    } else {
      core::logWarning(messages::UnknownSavedGraphicsValue, TextureFilterSetting,
                       *settings.textureFilter);
    }
  }
  return options;
}

std::optional<GraphicsOptions> withGraphicsFlag(GraphicsOptions options, std::string_view text) {
  for (const auto part : std::views::split(text, ',')) {
    const std::string_view item{part};
    const std::size_t equals = item.find('=');
    if (equals == std::string_view::npos) {
      return std::nullopt;
    }
    const std::string_view key = item.substr(0, equals);
    const std::string_view value = item.substr(equals + 1);
    if (key == "shadows") {
      const auto mode = parseShadowMode(value);
      if (!mode) {
        return std::nullopt;
      }
      options.shadows = *mode;
    } else if (key == "lighting" && onOff(value)) {
      options.enhancedLighting = *onOff(value);
    } else if (key == "colors" && onOff(value)) {
      options.vividColors = *onOff(value);
    } else if (key == "scenery" && parseSceneryQuality(value)) {
      options.sceneryQuality = *parseSceneryQuality(value);
    } else if (key == "aa" && onOff(value)) {
      options.antialiasing = *onOff(value);
    } else if (key == "textures" && parseTextureFilter(value)) {
      options.textureFilter = *parseTextureFilter(value);
    } else {
      return std::nullopt;
    }
  }
  return options;
}

GraphicsChanger::GraphicsChanger(UserSettings& settings,
                                 std::optional<std::filesystem::path> settingsFile,
                                 GraphicsOptions start)
    : settings_{&settings}, settingsFile_{std::move(settingsFile)}, options_{start} {}

bool GraphicsChanger::handle(const ui::UiCommand& command, ui::Ui& ui) {
  if (command.name == ShadowsSetting) {
    const std::string* name = std::get_if<std::string>(&command.value);
    if (const auto mode = name == nullptr ? std::nullopt : parseShadowMode(*name)) {
      options_.shadows = *mode;
      settings_->shadows = std::string{shadowModeName(*mode)};
      save();
    }
  } else if (command.name == EnhancedLightingSetting) {
    if (const bool* on = std::get_if<bool>(&command.value)) {
      options_.enhancedLighting = *on;
      settings_->enhancedLighting = *on;
      save();
    }
  } else if (command.name == VividColorsSetting) {
    if (const bool* on = std::get_if<bool>(&command.value)) {
      options_.vividColors = *on;
      settings_->vividColors = *on;
      save();
    }
  } else if (command.name == SceneryQualitySetting) {
    const std::string* name = std::get_if<std::string>(&command.value);
    if (const auto quality = name == nullptr ? std::nullopt : parseSceneryQuality(*name)) {
      options_.sceneryQuality = *quality;
      settings_->sceneryQuality = std::string{sceneryQualityName(*quality)};
      save();
    }
  } else if (command.name == AntialiasingSetting) {
    if (const bool* on = std::get_if<bool>(&command.value)) {
      options_.antialiasing = *on;
      settings_->antialiasing = *on;
      save();
    }
  } else if (command.name == TextureFilterSetting) {
    const std::string* name = std::get_if<std::string>(&command.value);
    if (const auto filter = name == nullptr ? std::nullopt : parseTextureFilter(*name)) {
      options_.textureFilter = *filter;
      settings_->textureFilter = std::string{textureFilterName(*filter)};
      save();
    }
  } else {
    return false;
  }
  publish(ui);
  return true;
}

void GraphicsChanger::publish(ui::Ui& ui) const {
  ui.setSetting(ShadowsSetting, std::string{shadowModeName(options_.shadows)});
  ui.setSetting(EnhancedLightingSetting, options_.enhancedLighting);
  ui.setSetting(VividColorsSetting, options_.vividColors);
  ui.setSetting(SceneryQualitySetting, std::string{sceneryQualityName(options_.sceneryQuality)});
  ui.setSetting(AntialiasingSetting, options_.antialiasing);
  ui.setSetting(TextureFilterSetting, std::string{textureFilterName(options_.textureFilter)});
  // The skins' pictures that may be smoothed (the title's) go through the models' upscaler with it.
  ui.setSmoothPictures(options_.textureFilter == TextureFilter::Upscaled);
}

void GraphicsChanger::save() {
  if (settingsFile_) {
    saveUserSettings(*settingsFile_, *settings_);
  }
}

} // namespace dwe::app
