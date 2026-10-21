#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ui/skin.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ui {

// The look a skin gives one screen, with what it extends folded in.
struct SkinLook {
    // Style sheets in the order they apply: the theme and the screen's styles of each skin, the base skin's first.
    std::vector<assets::AssetId> styles;
    // The document that replaces the screen's own, if any (the nearest skin that names one).
    std::optional<assets::AssetId> document;
};

// The skins the mounted packs offer: every ui/skins/<id>/skin.yaml whose assets exist, so a mod adds a skin by adding
// a folder. A skin that cannot be read, names an asset the packs lack or extends a skin that is not offered is logged
// and left out.
class SkinCatalog {
public:
    static SkinCatalog load(const assets::VirtualFileSystem& files);

    // The skins offered, by id.
    [[nodiscard]] const std::vector<Skin>& skins() const { return skins_; }
    [[nodiscard]] const Skin* find(std::string_view id) const;

    // The skin the packs start with (ui/ui_skin.yaml), or an empty id when they name none that is offered.
    [[nodiscard]] const std::string& defaultSkin() const { return defaultSkin_; }

    // The look a skin gives a screen.
    [[nodiscard]] SkinLook lookOf(const Skin& skin, std::string_view screen) const;

    // A skin's sprite sheets with those of the skins it extends, folded so the nearest skin wins: a sheet replaces
    // the one of the same name further down the chain, and a sprite name is kept only in the nearest sheet that has
    // it (RmlUi would keep the first one it reads).
    [[nodiscard]] std::vector<SkinSpriteSheet> spriteSheetsOf(const Skin& skin) const;

private:
    // The skin and those it extends, the base skin first; an error when one is missing or the chain loops.
    [[nodiscard]] core::Result<std::vector<const Skin*>> chainOf(const Skin& skin) const;

    std::vector<Skin> skins_;
    std::string defaultSkin_;
};

} // namespace dwe::ui
