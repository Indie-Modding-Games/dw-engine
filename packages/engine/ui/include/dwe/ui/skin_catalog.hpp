#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ui/skin.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ui {

// The look a skin gives one screen, with what it extends folded in.
struct SkinLook {
  // Style sheets in the order they apply: the theme and the screen's styles of each skin, the base
  // skin's first.
  std::vector<assets::AssetId> styles;
  // The document that replaces the screen's own, if any (the nearest skin that names one).
  std::optional<assets::AssetId> document;
};

// The mouse cursor a skin shows, with what it extends folded in (the nearest skin that names one).
struct SkinCursor {
  assets::AssetId image;
  std::uint32_t hotX = 0;
  std::uint32_t hotY = 0;
};

// The skins the mounted packs offer: every ui/skins/<id>/skin.yaml whose assets exist, so a mod
// adds a skin by adding a folder. A skin that cannot be read, names an asset the packs lack or
// extends a skin that is not offered is logged and left out.
class SkinCatalog {
public:
  static SkinCatalog load(const assets::VirtualFileSystem& files);

  // A fingerprint of what the catalog is read from (every skin.yaml and ui/ui_skin.yaml), so hot
  // reload can tell an edit of one of them.
  [[nodiscard]] static std::size_t sourceFingerprint(const assets::VirtualFileSystem& files);

  // The skins offered, by id.
  [[nodiscard]] const std::vector<Skin>& skins() const { return skins_; }
  [[nodiscard]] const Skin* find(std::string_view id) const;

  // The skin the packs start with (ui/ui_skin.yaml): its default, or its fallback when the default
  // is not offered, or an empty id when neither is.
  [[nodiscard]] const std::string& defaultSkin() const { return defaultSkin_; }

  // The look a skin gives a screen.
  [[nodiscard]] SkinLook lookOf(const Skin& skin, std::string_view screen) const;

  // The mouse cursor a skin shows: the nearest skin in the chain (this one first, then those it
  // extends) that names a `cursor`; nullopt when none does, so the system arrow stays.
  [[nodiscard]] std::optional<SkinCursor> cursorOf(const Skin& skin) const;

  // A skin's sprite sheets with those of the skins it extends, folded so the nearest skin wins: a
  // sheet replaces the one of the same name further down the chain, and a sprite name is kept only
  // in the nearest sheet that has it (RmlUi would keep the first one it reads).
  [[nodiscard]] std::vector<SkinSpriteSheet> spriteSheetsOf(const Skin& skin) const;

  // The classes a skin puts on the elements of a document, with those of the skins it extends:
  // every class any skin of the chain names, the base skin's first.
  [[nodiscard]] SkinElementClasses classesOf(const Skin& skin, std::string_view document) const;

  // Every class any skin offered puts on the elements of a document: what a switch takes off before
  // the new skin's go on.
  [[nodiscard]] SkinElementClasses declaredClassesOf(std::string_view document) const;

  // The layout options a skin sets on a document, with those of the skins it extends: the nearest
  // skin that sets an option wins.
  [[nodiscard]] SkinScreenLayout layoutOf(const Skin& skin, std::string_view document) const;

  // The documents a skin or a skin it extends puts classes on.
  [[nodiscard]] std::vector<std::string> classedDocumentsOf(const Skin& skin) const;

private:
  // The skin a choice starts with among those offered (see defaultSkin).
  [[nodiscard]] std::string startingSkin(const std::optional<SkinChoice>& choice) const;
  // The skin and those it extends, the base skin first; an error when one is missing or the chain
  // loops.
  [[nodiscard]] core::Result<std::vector<const Skin*>> chainOf(const Skin& skin) const;

  std::vector<Skin> skins_;
  std::string defaultSkin_;
};

} // namespace dwe::ui
