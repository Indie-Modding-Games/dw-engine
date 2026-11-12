#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ui/ui_contract.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::ui {

// The file that describes a UI addon, in its folder (docs/modding/interfaces.md), as an asset name.
inline constexpr std::string_view UiAddonManifest = "addon";
// The folder of the default UI's manifest in the game pack: ui/addon.yaml names the document of
// every screen.
inline constexpr std::string_view DefaultUiFolder = "ui";
inline constexpr std::string_view DefaultUiId = "default";

// What an addon is: a whole UI that shows the game's screens its own way, or a plugin that adds
// documents over whichever UI is on.
enum class UiAddonType : std::uint8_t {
  Ui,
  Plugin,
};

// The UI API versions an addon works with: from `minimum` up to, without, `below`.
struct UiApiRange {
  UiApiVersion minimum;
  UiApiVersion below;

  [[nodiscard]] bool contains(UiApiVersion version) const {
    return minimum <= version && version < below;
  }
};

// A UI addon's addon.yaml:
//
//   id: clock_corner           # lowercase letters, digits and underscores; the folder's name
//   name: Clock in the corner  # what Opciones -> Interfaz lists
//   version: 1.2.0             # the addon's own, shown next to its name
//   ui_api: "0.8"              # works with 0.8 and later 0.x; or ">=0.8 <0.12"
//   type: ui                   # or plugin
//   screens:                   # a UI: the document that shows each screen it covers
//     pause_menu: pause/document
//   documents:                 # a plugin: the documents it adds over the UI
//     - clock/document
//
// Documents are asset ids without the extension, relative to the manifest's folder.
struct UiAddon {
  std::string id;
  std::string name;
  std::string version;
  UiApiRange uiApi;
  UiAddonType type = UiAddonType::Ui;
  std::map<std::string, assets::AssetId, std::less<>> screens;
  std::vector<assets::AssetId> documents;
};

// Reads an addon.yaml whose folder is the asset folder `folder` (such as "ui" or
// "ui/addons/clock_corner"): its document ids come back under that folder.
[[nodiscard]] core::Result<UiAddon> parseUiAddon(std::string_view yaml, std::string_view folder);

// Reads the addon.yaml of an asset folder from the mounted packs.
[[nodiscard]] core::Result<UiAddon> readUiAddon(const assets::VirtualFileSystem& files,
                                                std::string_view folder);

// Reads a range of UI API versions: "0.8" (that version and every later one of its major) or one to
// two bounds, ">=0.8" and "<0.12", apart by a space.
[[nodiscard]] core::Result<UiApiRange> parseUiApiRange(std::string_view text);

} // namespace dwe::ui
