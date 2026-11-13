#pragma once

#include <string_view>

// The log and error messages of content, as std::format templates for core::formatMessage.
// Templates take their arguments in the order of their placeholders.
namespace dwe::content::messages {

// The mounted stack (game_content.cpp).
inline constexpr std::string_view NoPacks = "no packs in {}";
inline constexpr std::string_view MountedPack = "content: pack {} {} ({}) at {}";
inline constexpr std::string_view MountedDevPack =
    "content: pack {} {} ({}) at {}, from the dev assets, under every other pack";
inline constexpr std::string_view OverriddenId = "content: {} comes from pack {}";
inline constexpr std::string_view ShadowedDevPack =
    "content: dev assets pack {} at {} left out: {} has its id";
inline constexpr std::string_view UnknownPack = "?";
inline constexpr std::string_view ModSkipped = "content: left out the mod at {}: {}";
inline constexpr std::string_view ModWithoutApi =
    "content: mod {} declares no api; it has no Lua files, so it still mounts, but add api: \"1.x\" to "
    "its pack.yaml: a mod without it will be left out";
inline constexpr std::string_view LooseTeamPack =
    "content: left out {}: a package loads the team's packs only from their .pak archives";

// The dev assets (dev_assets.cpp).
inline constexpr std::string_view InFile = "{}: {}";
inline constexpr std::string_view DevAssetsConfigWhat = "the dev assets config";
inline constexpr std::string_view DevAssetsConfigEmpty =
    "names no roots: give disc_image, extracted_content or decomp_root";
inline constexpr std::string_view NoDevAssetsConfig =
    "{} is on, but none of these files exists: {}";
inline constexpr std::string_view CannotReadDevAssetsConfig =
    "cannot read the dev assets config {}: {}";
inline constexpr std::string_view DevAssetsOn =
    "dev assets: ON, from {} (never for players; read only)";
inline constexpr std::string_view DevAssetsSource = "dev assets: {} {}";
inline constexpr std::string_view ListSeparator = ", ";
inline constexpr std::string_view NoCurrentFolder =
    "dev assets: the current folder is unknown ({}): its config is not read";

inline constexpr std::string_view NoDevAssetsDataFolder =
    "dev assets: no user data folder ({}): only the current folder's config is read";
inline constexpr std::string_view NoDevCacheFolder =
    "dev assets: the disc image needs the user's data folder, which holds its dev cache, and there is none";
inline constexpr std::string_view NoDwx =
    "dev assets: the disc image is read with dwx, which is in none of: {}";
inline constexpr std::string_view ReadingDevDisc =
    "dev assets: reading the disc image {} with {}; the first time for a disc, dwx extracts it into {}, which takes a "
    "few minutes and about 1.5 GB for the whole game";
inline constexpr std::string_view DwxFailed =
    "dev assets: dwx could not give the extracted content of {} (exit code {}); its messages are above";
inline constexpr std::string_view DwxGaveNoFolder =
    "dev assets: dwx did not print one content folder: \"{}\"";
inline constexpr std::string_view DevDiscContent =
    "dev assets: disc image {} read as extracted content {}";

// The .zip archives (zip_archive.cpp) and the player's UI addons in them (user_ui_addons.cpp).
inline constexpr std::string_view ZipInFile = "{}: {}";
inline constexpr std::string_view ZipNotFile = "not a plain file";
inline constexpr std::string_view ZipCannotOpen = "cannot read it";
inline constexpr std::string_view ZipTooBig = "bigger than {} bytes";
inline constexpr std::string_view ZipNotZip = "not a .zip archive";
inline constexpr std::string_view ZipSpanned = "split over several files";
inline constexpr std::string_view ZipTooManyEntries = "more than {} entries";
inline constexpr std::string_view ZipBrokenDirectory = "its list of entries is broken";
inline constexpr std::string_view ZipEncrypted = "{} is encrypted";
inline constexpr std::string_view ZipMethod =
    "{} is compressed in a way the game does not read ({})";
inline constexpr std::string_view ZipUnsafePath = "{} is not a safe path inside the archive";
inline constexpr std::string_view ZipTooDeep = "{} is more than {} folders deep";
inline constexpr std::string_view ZipSpecialEntry = "{} is a link or another special file";
inline constexpr std::string_view ZipTwice = "{} is in it twice";
inline constexpr std::string_view ZipExpandsTooMuch = "it expands to more than {} bytes";
inline constexpr std::string_view ZipBomb = "{} expands {} times its size";
inline constexpr std::string_view ZipBrokenEntry = "{} is broken: {}";
inline constexpr std::string_view ZipBadHeader = "its header is not where the archive says";
inline constexpr std::string_view ZipBadData = "its bytes do not expand";
inline constexpr std::string_view ZipBadSize = "it expands to another size than the archive says";
inline constexpr std::string_view ZipBadCrc = "its CRC-32 does not match";
inline constexpr std::string_view ZipNoEntry = "{} has no file {}";
inline constexpr std::string_view ZipNoManifest = "no addon.yaml at its root or in its only folder";
inline constexpr std::string_view ZipNoAddonId =
    "its addon.yaml has no id of lowercase letters, digits and underscores";
inline constexpr std::string_view ZipBadFileName = "{} cannot be an asset: {}";
inline constexpr std::string_view ZipSameAsFolder =
    "the folder ui/addons/{} has its id, and it is used instead";
inline constexpr std::string_view ZipSameAsZip = "{} has its id {}, and it is used instead";
inline constexpr std::string_view ZipShadowed = "content: left out the UI addon in {}";

} // namespace dwe::content::messages
