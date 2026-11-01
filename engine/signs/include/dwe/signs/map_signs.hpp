#pragma once

#include <dwe/assets/map_images.hpp>
#include <dwe/signs/sign_font.hpp>
#include <dwe/signs/sign_overlay.hpp>
#include <dwe/signs/sign_painter.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::signs {

// The asset ids of a map's overlays: every language's and one language's.
std::string signOverlayId(std::string_view mapId);
std::string signOverlayId(std::string_view mapId, std::string_view language);

// The signs of the packs (signs/<map> and signs/<language>/<map>), their fonts (ui/fonts/) and the
// patches painted for each map and language, kept until the language's texts change.
class MapSigns {
public:
  explicit MapSigns(const assets::VirtualFileSystem& files);

  // The signs a map has in a language; none when no pack describes any. A document that does not
  // read is logged and left out.
  [[nodiscard]] SignOverlay overlay(std::string_view mapId, std::string_view language) const;

  // The map's signs painted into `original` (the map's images as loaded) and into the sprites of
  // `objects` cut from the background over them, in a language, with the texts `text` gives.
  // Painted once per map, language and `textRevision`.
  const PaintedSigns& painted(const assets::MapImages& original, const assets::MapObjects* objects,
                              std::string_view mapId, std::string_view language,
                              std::uint32_t textRevision,
                              const std::function<std::string(std::string_view)>& text);

private:
  const SignFont* font(std::string_view name);

  const assets::VirtualFileSystem* files_;
  std::map<std::string, std::unique_ptr<SignFont>, std::less<>> fonts_;
  std::map<std::string, PaintedSigns, std::less<>> painted_;
};

} // namespace dwe::signs
