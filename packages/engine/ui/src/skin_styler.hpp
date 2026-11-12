#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/ui/skin_catalog.hpp>

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StyleSheetContainer.h>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ui {

// Gives documents the look of the player's skin: the skin's style sheets for each document's
// screen, after the document's own, and the skin's sprite sheets. A document keeps its elements,
// scripts and state: only its style sheet changes, so a switch shows at once.
class SkinStyler {
public:
  SkinStyler(const assets::VirtualFileSystem& files, const SkinCatalog& catalog)
      : files_{&files}, catalog_{&catalog} {}

  // Switches to a skin of the catalog, or to none (nullptr), and forgets the sheets built for the
  // previous one.
  void use(const Skin* skin);
  [[nodiscard]] const Skin* skin() const { return skin_; }

  // The document a screen shows: the skin's replacement, or the screen's own.
  [[nodiscard]] assets::AssetId documentOf(std::string_view screen,
                                           const assets::AssetId& own) const;

  // Styles a document of a screen with the skin's look on top of `own`, the document's own style
  // sheet. Returns the paths of the files the look was read from, so the caller can watch them.
  std::vector<std::string> style(Rml::ElementDocument& document, std::string_view screen,
                                 const Rml::StyleSheetContainer& own);

  // A layout option the current skin sets on a screen (skin.yaml `layout:`); nullopt when no skin
  // of its chain sets it, or with no skin.
  [[nodiscard]] std::optional<std::string> layoutOption(std::string_view screen,
                                                        std::string_view option) const;

  // Of the documents the current skin puts classes on (document_classes), those not among `loaded`,
  // each logged as a warning: a name the game never loads is likely a typo.
  [[nodiscard]] std::vector<std::string>
  unknownClassedDocuments(std::span<const std::string> loaded) const;

  // Forgets the sheets built so far, so the next style() reads the files again (hot reload).
  void forget();

  // The images of the skin's sprite sheets, by the path RmlUi reads each one with.
  [[nodiscard]] std::vector<std::string> spriteImages() const;

  // Whether the sprite sheets a skin lets smooth (SkinSpriteSheet::smooth) are drawn smoothed by
  // the texture upscaler; off, they draw as the skin's other sheets. Forgets the sheets built so
  // far when it changes; true then.
  bool setSmoothPictures(bool smooth);
  [[nodiscard]] bool smoothPictures() const { return smoothPictures_; }

private:
  struct ScreenSheet {
    Rml::SharedPtr<Rml::StyleSheetContainer> sheet;
    std::vector<std::string> paths;
  };

  const ScreenSheet& sheetOf(std::string_view screen);
  // Takes off the document's elements every class a skin of the catalog puts on them, then puts on
  // the current skin's, warning about the ids the document lacks.
  void applyClasses(Rml::ElementDocument& document, std::string_view screen) const;
  // Mirrors the screen's layout options as classes on the document's body
  // (layout-<option>-<value>), taking off those of the previous skin.
  void applyLayout(Rml::ElementDocument& document, std::string_view screen) const;
  // A sheet's image as RmlUi reads it: smoothed when the sheet lets it and smoothing is on.
  [[nodiscard]] ImageReference imageOf(const SkinSpriteSheet& sheet) const;
  // The skin's sprite sheets as one style sheet, or nullptr for none.
  [[nodiscard]] Rml::SharedPtr<Rml::StyleSheetContainer> buildSpriteSheets() const;

  const assets::VirtualFileSystem* files_;
  const SkinCatalog* catalog_;
  const Skin* skin_ = nullptr;
  Rml::SharedPtr<Rml::StyleSheetContainer> spriteSheets_;
  std::map<std::string, ScreenSheet, std::less<>> sheets_;
  bool smoothPictures_ = false;
};

} // namespace dwe::ui
