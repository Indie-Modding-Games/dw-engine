#pragma once

#include <string_view>

// The error messages of assets. Templates take their arguments in the order of their placeholders.
namespace dwe::assets::messages {

// Pack manifests (pack_manifest.cpp).
inline constexpr std::string_view BadApiRequirement =
    "api '{}' is not MAJOR.x or MAJOR.MINOR, such as {}.x or {}.{}";
inline constexpr std::string_view ModInEngineLayer =
    "pack '{}' is a mod and declares the {} layer, which only the engine's own packs have: a mod's layer is mods or "
    "user";

// The Lua API a pack asks for (lua_api.cpp).
inline constexpr std::string_view IncompatibleApi =
    "the scripts need the Lua API {}, and this engine has {}";
inline constexpr std::string_view PackNeedsOtherApi = "pack '{}': {}";
inline constexpr std::string_view ModWithoutApi =
    "pack '{}' is in the {} layer and declares no api: a mod says the Lua API it is written for, such as "
    "api: \"{}.x\" or api: \"{}.{}\"";

// Palettes of extracted images (image_palettes.cpp, map_images.cpp).
inline constexpr std::string_view PaletteColorNotHex =
    "texture metadata: a palette colour is not a 16-bit hexadecimal number";
inline constexpr std::string_view PaletteNotText = "texture metadata: a palette is not a string";
inline constexpr std::string_view NoImagePalettes = "texture metadata: palettes is missing";

// Files and assets (file_system.cpp, virtual_file_system.cpp).
inline constexpr std::string_view CannotReadFile = "cannot read '{}': {}";
inline constexpr std::string_view FileMissing = "cannot read '{}': it does not exist";
inline constexpr std::string_view NoPackHasAsset = "no pack has the asset '{}'";

// KTX2 textures (texture.cpp).
inline constexpr std::string_view Ktx2TooShort =
    "not a KTX2 texture: {} bytes is shorter than its header";
inline constexpr std::string_view NotKtx2 = "not a KTX2 texture: {}";
inline constexpr std::string_view Ktx2LevelIndexCut =
    "not a KTX2 texture: the index of its {} mip levels runs past its {} bytes";
inline constexpr std::string_view Ktx2RegionOutsideFile =
    "not a KTX2 texture: its {} ({} bytes at {}) runs past its {} bytes";
inline constexpr std::string_view Ktx2LevelOutsideFile =
    "not a KTX2 texture: mip level {} runs past its {} bytes";
inline constexpr std::string_view Ktx2NotPlain2d =
    "the KTX2 texture is not a plain 2D texture: {} dimensions, depth {}, {} layers, {} faces";
inline constexpr std::string_view Ktx2BadSize = "the KTX2 texture is {}x{}, not 1 to {} on a side";
inline constexpr std::string_view Ktx2BadLevels = "a {}x{} texture cannot have {} mip levels";
inline constexpr std::string_view Ktx2BadSupercompression =
    "the KTX2 texture is supercompressed with a scheme other than BasisLZ or Zstd";
inline constexpr std::string_view Ktx2LevelTooLarge =
    "mip level {} of the KTX2 texture takes {} bytes once loaded, more than the {} a {}x{} level holds";
inline constexpr std::string_view Ktx2NotBasis =
    "the KTX2 texture does not hold Basis Universal data";
inline constexpr std::string_view Ktx2LoadFailed = "cannot load the texels of the KTX2 texture: {}";
inline constexpr std::string_view Ktx2TranscodeFailed = "cannot transcode the KTX2 texture: {}";
inline constexpr std::string_view Ktx2LevelOutOfData =
    "mip level {} of the KTX2 texture is out of its data";

// Images the game reads from their PNG (png_only_images.cpp).
inline constexpr std::string_view PngOnlyImageNotPng =
    "the game reads this image from a PNG, as {}, so a .{} file cannot replace it: keep it as PNG";
inline constexpr std::string_view PngOnlyImageNotIndexed =
    "the game reads this image's palette indices: {}";
inline constexpr std::string_view PngOnlyImageUnreadable =
    "the game reads this image's exact pixels: {}";
inline constexpr std::string_view IndexedReadingName = "palette indices";
inline constexpr std::string_view ExactPixelsReadingName = "exact pixels";

// Text tables (text_table.cpp).
inline constexpr std::string_view TableContext = "text table {}";
inline constexpr std::string_view GameTableWhat = "the text table";
inline constexpr std::string_view StringsWhat = "a block of strings";
inline constexpr std::string_view NotText = "must be a text";
inline constexpr std::string_view NotMapping = "must map keys to texts";
inline constexpr std::string_view BadKey =
    "'{}' is not a key: use segments of a-z, 0-9 and _ joined by dots";
inline constexpr std::string_view UnclosedPlaceholder =
    "a {{ at byte {} has no }}: write {{{{ for a brace";
inline constexpr std::string_view InFile = "{}: {}";
inline constexpr std::string_view NoPackOfFile =
    "{} belongs to the pack '{}', which is not mounted";
inline constexpr std::string_view LoneClosingBrace =
    "a }} at byte {} closes nothing: write }}}} for a brace";

// The translation check (checkTranslations).
inline constexpr std::string_view MissingTable =
    "has no {} translation ({}): its {} texts show in {}";
inline constexpr std::string_view NoReferenceTable = "has no {} table to translate ({})";
inline constexpr std::string_view MissingKey = "misses the key {}, which shows in {}";
inline constexpr std::string_view ExtraKey = "has the key {}, which {} does not have";
inline constexpr std::string_view PlaceholderMismatch =
    "has the key {} with the placeholders {{{}}}, where {} has {{{}}}";
inline constexpr std::string_view KeyInTwoTables = "has the key {}, which {} has too";

// The lock of a translation (checkSourcesLocks).
inline constexpr std::string_view LockContext = "the lock {}";
inline constexpr std::string_view LockWhat = "a translation's lock";
inline constexpr std::string_view LockSourcesWhat = "the sources of a lock";
inline constexpr std::string_view LockKeptWhat = "the strings kept in English";
inline constexpr std::string_view BadHash =
    "'{}' is not a SHA-1: write its 40 hex digits in lowercase";
inline constexpr std::string_view NotInEnglish = "names {}, which the English text does not have";
inline constexpr std::string_view StaleTranslation =
    "{} was translated from another English text: check it and write the new hash, {}";
inline constexpr std::string_view UnhashedTranslation =
    "{} is translated, but the lock has no hash of its English text";
inline constexpr std::string_view KeptAndTranslated =
    "{} is kept as it is, but the translation has it";

// The development sources (dev_asset_sources.cpp, dev_pack_selection.cpp).
inline constexpr std::string_view DiscImageKind = "disc image";
inline constexpr std::string_view ExtractedContentKind = "extracted content folder";
inline constexpr std::string_view DecompRootKind = "decomp root";
inline constexpr std::string_view DevSourceMissing = "the dev assets' {} {} does not exist";
inline constexpr std::string_view DevSourceNotFile = "the dev assets' {} {} is not a file";
inline constexpr std::string_view DevSourceNotFolder = "the dev assets' {} {} is not a folder";
inline constexpr std::string_view DevSourceNoPacks = "the dev assets' {} {} has no packs";
inline constexpr std::string_view DevSourceNoDecompLayout =
    "the dev assets' {} {} has no include/dw and src folders: it is not a checkout of the decompilation";
inline constexpr std::string_view DevSourceUnreadable = "the dev assets' {} {} cannot be read: {}";

// The player's images of a map (map_images.cpp: loadMapImages).
inline constexpr std::string_view ReplacementUnreadable =
    "map {}: the image '{}' of pack '{}' is not used, the game's is drawn: {}";
inline constexpr std::string_view ReplacementWrongSize =
    "map {}: the image '{}' of pack '{}' is {}x{}, not {}x{} or a whole number of times larger with "
    "the same aspect ratio (up to {}x): the game's is drawn";
inline constexpr std::string_view ReplacementOtherScale =
    "map {}: the image '{}' of pack '{}' is {}x larger, but the map's other images are {}x: the "
    "game's is drawn";
inline constexpr std::string_view ReplacementOfPage =
    "map {}: the image '{}' of pack '{}' is not used: only a map's backgrounds can be replaced";
inline constexpr std::string_view ReplacementUsed =
    "map {}: drawing the player's background images, {}x, {} of {} variants";

} // namespace dwe::assets::messages
