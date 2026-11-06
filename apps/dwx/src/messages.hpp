#pragma once

#include <string_view>

// The messages of dwx. Templates take their arguments in the order of their placeholders.
namespace dwe::dwx::messages {

// dwx validate: a translation that differs from the reference language (assets::checkTranslations).
inline constexpr std::string_view TranslationProblem = "the translation {} {}";

// dwx extract: the engine cannot mount the pack just written, or open one of its assets.
inline constexpr std::string_view EngineCheckFailed = "engine check failed: {}";

// dwx extract --cache: the dev cache of extracted discs.
inline constexpr std::string_view CacheExtracting =
    "dev cache: extracting {} into {} (extractor version {}); the first time for a disc this takes a few minutes and "
    "about 1.5 GB for the whole game";
inline constexpr std::string_view CacheWritten = "dev cache: {} written (extractor version {})";
inline constexpr std::string_view CacheIncompleteEntry =
    "dev cache: {} is not a complete entry (a link, a file or a folder without its record); extracting it again";
inline constexpr std::string_view CacheCannotSync = "dev cache: cannot flush {} to the disk";
inline constexpr std::string_view CacheReady = "dev cache: {} is ready (extractor version {})";
inline constexpr std::string_view CacheRemovedStale =
    "dev cache: removed {}, made by an older extractor version";
inline constexpr std::string_view CacheOldPartials =
    "dev cache: temporary folders older than {} h, left by stopped extractions; delete them by hand while no "
    "extraction is running: {}";
inline constexpr std::string_view CacheRecordComment =
    "Written by dwx extract --cache: the disc and the extractor "
    "version of this entry.";
inline constexpr std::string_view CacheCannotCreate = "cannot create {}: {}";
inline constexpr std::string_view CacheCannotList = "cannot list {}: {}";
inline constexpr std::string_view CacheCannotWrite = "cannot write {}";
inline constexpr std::string_view CacheCannotRename = "cannot move {} to {}: {}";
inline constexpr std::string_view CacheCannotRemove = "cannot remove {}: {}";

// dwx validate: an id that no entry of the table it names has (the schema's entryReference), with
// the path and the table.
inline constexpr std::string_view EntryReferenceUnknown =
    "{}: '{}' is no entry of {}, as the packs compose it";

// dwx validate: a schema whose entryReference names no content table.
inline constexpr std::string_view EntryReferenceTable =
    "{}: the schema's entryReference '{}' is no content table";

// dwx export-images: the images a player can replace (export_images.hpp).
inline constexpr std::string_view ExportNotIndexed = "{} has palettes but is no indexed PNG: {}";
inline constexpr std::string_view ExportOutputTaken =
    "{} is neither empty nor an export of images; pick another folder";
inline constexpr std::string_view ExportCannotReplace = "cannot write the export to {}: {}";

// dwx evolution-guide: the requirements table of the user's content (evolution_guide.hpp).
inline constexpr std::string_view GuideOutputTaken = "{} already exists; pick another file";
inline constexpr std::string_view GuideCannotWrite = "cannot write {}";

// dwx upscale: the built-in index_scale step and the recipe it runs (upscale_recipe.hpp).
inline constexpr std::string_view IndexScaleUnsupported =
    "index_scale cannot grow an image {}x in {} mode";
inline constexpr std::string_view IndexScaleBadImage =
    "index_scale: an image of {}x{} pixels with {} indices";
inline constexpr std::string_view UpscaleRecipeVersion = "the recipe version must be {}";
inline constexpr std::string_view UpscaleBadId =
    "the id must be 1 to 64 lowercase letters, digits or underscores";
inline constexpr std::string_view UpscaleAssetType =
    "dwx upscale only runs recipes of asset_type ui or icon";
inline constexpr std::string_view UpscaleImageTwice = "{} is selected twice";
inline constexpr std::string_view UpscaleNoImages = "select.include names no image";
inline constexpr std::string_view UpscaleOnlyIndexScale = "dwx upscale runs one step, builtin {}";
inline constexpr std::string_view UpscaleStepScale =
    "the step's scale {} is not the recipe's scale {}";
inline constexpr std::string_view UpscaleUnknownMode = "the mode must be edge or nearest";
inline constexpr std::string_view UpscaleScaleUnsupported =
    "index_scale cannot reach scale {} in this mode (edge: 1, 2, 3, 4, 6 or 8; nearest: 1 to 8)";
inline constexpr std::string_view UpscaleNoPacks = "{} has no pack to read the images from";
inline constexpr std::string_view UpscaleImageMissing = "no pack provides {} as a PNG";
inline constexpr std::string_view UpscaleNotIndexed = "{} is not an indexed PNG: {}";
inline constexpr std::string_view UpscaleBadMetadata = "{} is not a YAML mapping";
inline constexpr std::string_view UpscalePackDescription =
    "Written by dwx upscale with the recipe {}: images grown {}x";
inline constexpr std::string_view UpscaleOutputTaken =
    "{} exists and is not the remaster pack {} that this recipe writes; remove it or pick another folder";
inline constexpr std::string_view BundledRecipeBroken = "a recipe built into dwx does not read: {}";
inline constexpr std::string_view UpscaleCannotReplace = "cannot put the upscaled pack at {}: {}";
inline constexpr std::string_view UpscaleCannotResolve = "cannot resolve {}: {}";
inline constexpr std::string_view UpscaleOutputOverlaps =
    "the output folder {} and the pack {} must not hold each other";
inline constexpr std::string_view UpscalePartialTaken =
    "{} exists and is not a run of this recipe left half written; remove it or pick another folder";

} // namespace dwe::dwx::messages
