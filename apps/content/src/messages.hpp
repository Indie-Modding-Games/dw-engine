#pragma once

#include <string_view>

// The log and error messages of content, as std::format templates for core::formatMessage. Templates take their
// arguments in the order of their placeholders.
namespace dwe::content::messages {

// The mounted stack (game_content.cpp).
inline constexpr std::string_view NoPacks = "no packs in {}";
inline constexpr std::string_view MountedPack = "content: pack {} {} ({}) at {}";
inline constexpr std::string_view MountedDevPack =
    "content: pack {} {} ({}) at {}, from the dev assets, under every other pack";
inline constexpr std::string_view OverriddenId = "content: {} comes from pack {}";
inline constexpr std::string_view ShadowedDevPack = "content: dev assets pack {} at {} left out: {} has its id";
inline constexpr std::string_view UnknownPack = "?";

// The dev assets (dev_assets.cpp).
inline constexpr std::string_view InFile = "{}: {}";
inline constexpr std::string_view DevAssetsConfigWhat = "the dev assets config";
inline constexpr std::string_view DevAssetsConfigEmpty =
    "names no roots: give disc_image, extracted_content or decomp_root";
inline constexpr std::string_view NoDevAssetsConfig = "{} is on, but none of these files exists: {}";
inline constexpr std::string_view CannotReadDevAssetsConfig = "cannot read the dev assets config {}: {}";
inline constexpr std::string_view DevAssetsOn = "dev assets: ON, from {} (never for players; read only)";
inline constexpr std::string_view DevAssetsSource = "dev assets: {} {}";
inline constexpr std::string_view ListSeparator = ", ";
inline constexpr std::string_view NoCurrentFolder =
    "dev assets: the current folder is unknown ({}): its config is not read";

} // namespace dwe::content::messages
