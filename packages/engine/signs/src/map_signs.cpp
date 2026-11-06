#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/content_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/signs/map_signs.hpp>

#include <format>
#include <utility>

namespace dwe::signs {
namespace {

constexpr std::string_view FontFolder = "ui/fonts/";

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
        {.text = text, .font = [this](std::string_view name) { return font(name); }}, objects);
    for (const std::string& warning : painted.warnings) {
      core::logWarning("signs: map {}: {}", mapId, warning);
    }
  }
  return painted_.emplace(key, std::move(painted)).first->second;
}

const SignFont* MapSigns::font(std::string_view name) {
  if (const auto found = fonts_.find(name); found != fonts_.end()) {
    return found->second.get();
  }
  std::unique_ptr<SignFont> loaded;
  const auto id = assets::AssetId::parse(std::format("{}{}", FontFolder, name));
  if (id && files_->resolve(*id) != nullptr) {
    if (auto bytes = files_->read(*id)) {
      if (auto font = SignFont::load(std::move(*bytes))) {
        loaded = std::make_unique<SignFont>(std::move(*font));
      } else {
        core::logWarning("signs: font {}: {}", name, font.error().message);
      }
    }
  }
  return fonts_.emplace(std::string{name}, std::move(loaded)).first->second.get();
}

} // namespace dwe::signs
