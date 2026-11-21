#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/content_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/signs/map_signs.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <utility>

namespace dwe::signs {
namespace {

constexpr std::string_view FontFolder = "ui/fonts/";
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> CjkFallbacks{{
    {"zh", "cjk/noto_sans_cjk_sc"},
    {"ja", "cjk/noto_sans_cjk_jp"},
    {"ko", "cjk/noto_sans_cjk_kr"},
}};

std::optional<SignOverlay> readOverlay(const assets::VirtualFileSystem& files,
                                       const std::string& id) {
  const auto assetId = assets::AssetId::parse(id);
  if (!assetId || files.resolve(*assetId) == nullptr) {
    return std::nullopt;
  }
  const auto bytes = files.read(*assetId);
  if (!bytes) {
    core::logWarning("signs: {}: {}", id, bytes.error().message);
    return std::nullopt;
  }
  auto overlay = parseSignOverlay(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  if (!overlay) {
    core::logWarning("signs: {}: {}", id, overlay.error().message);
    return std::nullopt;
  }
  return std::move(*overlay);
}

} // namespace

std::string signOverlayId(std::string_view mapId) {
  return std::format("signs/{}", assets::contentAssetPath(mapId));
}

std::string signOverlayId(std::string_view mapId, std::string_view language) {
  return std::format("signs/{}/{}", language, assets::contentAssetPath(mapId));
}

MapSigns::MapSigns(const assets::VirtualFileSystem& files) : files_(&files) {}

SignOverlay MapSigns::overlay(std::string_view mapId, std::string_view language) const {
  SignOverlay common = readOverlay(*files_, signOverlayId(mapId)).value_or(SignOverlay{});
  if (const auto own = readOverlay(*files_, signOverlayId(mapId, language))) {
    return mergeSignOverlays(std::move(common), *own);
  }
  return common;
}

const PaintedSigns& MapSigns::painted(const assets::MapImages& original,
                                      const assets::MapObjects* objects, std::string_view mapId,
                                      std::string_view language, std::uint32_t textRevision,
                                      const std::function<std::string(std::string_view)>& text) {
  const std::string key = std::format("{}|{}|{}", mapId, language, textRevision);
  if (const auto found = painted_.find(key); found != painted_.end()) {
    return found->second;
  }
  const SignOverlay signs = overlay(mapId, language);
  PaintedSigns painted;
  if (!signs.signs.empty()) {
    painted = paintSigns(
        original, signs.signs,
        {.text = text,
         .font = [this, language](std::string_view name) { return font(name, language); }},
        objects);
    for (const std::string& warning : painted.warnings) {
      core::logWarning("signs: map {}: {}", mapId, warning);
    }
  }
  return painted_.emplace(key, std::move(painted)).first->second;
}

std::vector<SignTextLayout>
MapSigns::layouts(std::string_view mapId, std::string_view language,
                  const std::function<std::string(std::string_view)>& text) {
  const SignOverlay signs = overlay(mapId, language);
  return layOutSigns(signs.signs, {.text = text, .font = [this, language](std::string_view name) {
                                     return font(name, language);
                                   }});
}

std::optional<SignFont> MapSigns::loadFont(std::string_view name) const {
  const auto id = assets::AssetId::parse(std::format("{}{}", FontFolder, name));
  if (!id || files_->resolve(*id) == nullptr) {
    return std::nullopt;
  }
  auto bytes = files_->read(*id);
  if (!bytes) {
    core::logWarning("signs: font {}: {}", name, bytes.error().message);
    return std::nullopt;
  }
  auto font = SignFont::load(std::move(*bytes));
  if (!font) {
    core::logWarning("signs: font {}: {}", name, font.error().message);
    return std::nullopt;
  }
  return std::move(*font);
}

const SignFont* MapSigns::font(std::string_view name, std::string_view language) {
  const std::string key = std::format("{}|{}", name, language);
  if (const auto found = fonts_.find(key); found != fonts_.end()) {
    return found->second.get();
  }
  std::unique_ptr<SignFont> loaded;
  if (auto font = loadFont(name)) {
    loaded = std::make_unique<SignFont>(std::move(*font));
    auto order = CjkFallbacks;
    std::ranges::stable_partition(
        order, [&](const auto& face) { return language.starts_with(face.first); });
    for (const auto& [faceLanguage, file] : order) {
      if (auto fallback = loadFont(file)) {
        loaded->addFallback(std::move(*fallback));
      }
    }
  }
  return fonts_.emplace(key, std::move(loaded)).first->second.get();
}

} // namespace dwe::signs
