#include <dwe/assets/file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/ui/skin_catalog.hpp>

#include <algorithm>
#include <map>
#include <span>
#include <string>
#include <utility>

#include "messages.hpp"

namespace dwe::ui {
namespace {

constexpr std::string_view StyleExtension = ".rcss";
constexpr std::string_view DocumentExtension = ".rml";

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// The folder name of a skin document's id, ui/skins/<folder>/skin, or nothing for any other id.
std::optional<std::string_view> skinFolderOf(const assets::AssetId& id) {
    std::string_view text = id.str();
    const std::string suffix = "/" + std::string{SkinDocumentName};
    if (!text.starts_with(SkinsFolder) || !text.ends_with(suffix)) {
        return std::nullopt;
    }
    text.remove_prefix(SkinsFolder.size());
    text.remove_suffix(suffix.size());
    if (text.empty() || text.contains('/')) {
        return std::nullopt;
    }
    return text;
}

// Checks that the packs provide an asset as a file of that type.
core::Result<void> checkAsset(const assets::VirtualFileSystem& files, const assets::AssetId& id,
                              std::string_view extension) {
    const assets::AssetSource* source = files.resolve(id);
    if (source == nullptr || (!extension.empty() && assets::pathText(source->file.extension()) != extension)) {
        return core::fail(core::formatMessage(messages::SkinAssetMissing, id.str()));
    }
    return {};
}

core::Result<void> checkAssets(const assets::VirtualFileSystem& files, const Skin& skin) {
    for (const assets::AssetId& style : skin.theme) {
        if (auto found = checkAsset(files, style, StyleExtension); !found) {
            return found;
        }
    }
    for (const auto& [name, screen] : skin.screens) {
        for (const assets::AssetId& style : screen.styles) {
            if (auto found = checkAsset(files, style, StyleExtension); !found) {
                return found;
            }
        }
        if (screen.document) {
            if (auto found = checkAsset(files, *screen.document, DocumentExtension); !found) {
                return found;
            }
        }
    }
    for (const SkinSpriteSheet& sheet : skin.spriteSheets) {
        if (auto found = checkAsset(files, sheet.image.asset, {}); !found) {
            return found;
        }
    }
    return {};
}

// A skin's sprite sheets folded along its chain (see SkinCatalog::spriteSheetsOf), and the sprite names that more
// than one sheet gives, as warnings.
struct FoldedSpriteSheets {
    std::vector<SkinSpriteSheet> sheets;
    std::vector<std::string> warnings;
};

FoldedSpriteSheets foldSpriteSheets(const Skin& skin, std::span<const Skin* const> chain) {
    // Each sheet with how near its skin is to `skin`: 0 for the skin itself.
    std::vector<std::pair<SkinSpriteSheet, std::size_t>> layered;
    for (std::size_t layer = 0; layer < chain.size(); ++layer) {
        const std::size_t nearness = chain.size() - 1 - layer;
        for (const SkinSpriteSheet& sheet : chain[layer]->spriteSheets) {
            const auto same =
                std::ranges::find(layered, sheet.name, [](const auto& entry) { return entry.first.name; });
            if (same != layered.end()) {
                *same = {sheet, nearness};
            } else {
                layered.emplace_back(sheet, nearness);
            }
        }
    }
    std::ranges::stable_sort(layered, {}, [](const auto& entry) { return entry.second; });

    FoldedSpriteSheets folded;
    std::map<std::string, std::string, std::less<>> owners; // sprite name -> the sheet that keeps it
    for (auto& [sheet, nearness] : layered) {
        std::erase_if(sheet.sprites, [&](const SkinSprite& sprite) {
            const auto [owner, added] = owners.try_emplace(sprite.name, sheet.name);
            if (!added) {
                folded.warnings.push_back(core::formatMessage(messages::DuplicateSprite, skin.id, sprite.name,
                                                              owner->second, sheet.name, owner->second));
            }
            return !added;
        });
        if (!sheet.sprites.empty()) {
            folded.sheets.push_back(std::move(sheet));
        }
    }
    return folded;
}

core::Result<Skin> readSkin(const assets::VirtualFileSystem& files, const assets::AssetId& id,
                            std::string_view folder) {
    auto bytes = files.read(id);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    auto skin = parseSkin(textOf(*bytes));
    if (!skin) {
        return skin;
    }
    if (skin->id != folder) {
        return core::fail(core::formatMessage(messages::SkinIdNotFolder, skin->id, folder));
    }
    if (auto checked = checkAssets(files, *skin); !checked) {
        return std::unexpected{checked.error()};
    }
    return skin;
}

std::optional<SkinChoice> readSkinChoice(const assets::VirtualFileSystem& files) {
    auto id = assets::AssetId::parse(SkinChoiceAsset);
    if (!id || files.resolve(*id) == nullptr) {
        core::logWarning("{}", core::formatMessage(messages::NoSkinChoice, SkinChoiceAsset));
        return std::nullopt;
    }
    auto bytes = files.read(*id);
    if (!bytes) {
        core::logWarning("{}", core::formatMessage(messages::BadSkinChoice, SkinChoiceAsset, bytes.error().message));
        return std::nullopt;
    }
    auto choice = parseSkinChoice(textOf(*bytes));
    if (!choice) {
        core::logWarning("{}", core::formatMessage(messages::BadSkinChoice, SkinChoiceAsset, choice.error().message));
        return std::nullopt;
    }
    return std::move(*choice);
}

} // namespace

SkinCatalog SkinCatalog::load(const assets::VirtualFileSystem& files) {
    SkinCatalog catalog;
    for (const assets::AssetId& id : files.ids()) {
        const std::optional<std::string_view> folder = skinFolderOf(id);
        if (!folder) {
            continue;
        }
        auto skin = readSkin(files, id, *folder);
        if (!skin) {
            core::logWarning("{}", core::formatMessage(messages::SkinLeftOut, *folder, skin.error().message));
            continue;
        }
        catalog.skins_.push_back(std::move(*skin));
    }
    std::ranges::sort(catalog.skins_, {}, &Skin::id);

    std::vector<std::string> broken;
    for (const Skin& skin : catalog.skins_) {
        auto chain = catalog.chainOf(skin);
        if (!chain) {
            core::logWarning("{}", core::formatMessage(messages::SkinLeftOut, skin.id, chain.error().message));
            broken.push_back(skin.id);
            continue;
        }
        for (const std::string& warning : foldSpriteSheets(skin, *chain).warnings) {
            core::logWarning("{}", warning);
        }
    }
    std::erase_if(catalog.skins_, [&](const Skin& skin) { return std::ranges::find(broken, skin.id) != broken.end(); });

    catalog.defaultSkin_ = catalog.startingSkin(readSkinChoice(files));
    return catalog;
}

std::string SkinCatalog::startingSkin(const std::optional<SkinChoice>& choice) const {
    if (!choice) {
        return {};
    }
    if (find(choice->preferred) != nullptr) {
        return choice->preferred;
    }
    if (choice->fallback && find(*choice->fallback) != nullptr) {
        core::logInfo("{}", core::formatMessage(messages::DefaultSkinFallback, choice->preferred, *choice->fallback));
        return *choice->fallback;
    }
    core::logWarning("{}", core::formatMessage(messages::DefaultSkinNotOffered, choice->preferred));
    return {};
}

const Skin* SkinCatalog::find(std::string_view id) const {
    const auto found = std::ranges::find(skins_, id, &Skin::id);
    return found == skins_.end() ? nullptr : &*found;
}

core::Result<std::vector<const Skin*>> SkinCatalog::chainOf(const Skin& skin) const {
    std::vector<const Skin*> chain{&skin};
    for (;;) {
        const std::optional<std::string>& extends = chain.front()->extends;
        if (!extends) {
            break;
        }
        const std::string& extended = *extends;
        const Skin* base = find(extended);
        if (base == nullptr) {
            return core::fail(core::formatMessage(messages::SkinBaseMissing, extended));
        }
        if (std::ranges::find(chain, base) != chain.end()) {
            return core::fail(core::formatMessage(messages::SkinExtendsLoop, extended));
        }
        chain.insert(chain.begin(), base);
    }
    return chain;
}

SkinLook SkinCatalog::lookOf(const Skin& skin, std::string_view screen) const {
    SkinLook look;
    for (const Skin* layer : chainOf(skin).value_or(std::vector<const Skin*>{})) {
        look.styles.insert(look.styles.end(), layer->theme.begin(), layer->theme.end());
        const auto found = layer->screens.find(screen);
        if (found == layer->screens.end()) {
            continue;
        }
        look.styles.insert(look.styles.end(), found->second.styles.begin(), found->second.styles.end());
        if (found->second.document) {
            look.document = found->second.document;
        }
    }
    return look;
}

namespace {

// Adds the classes of one skin's document to `into`, keeping each class once per element.
void mergeClasses(const Skin& skin, std::string_view document, SkinElementClasses& into) {
    const auto found = skin.documentClasses.find(document);
    if (found == skin.documentClasses.end()) {
        return;
    }
    for (const auto& [element, classes] : found->second) {
        std::vector<std::string>& names = into[element];
        for (const std::string& name : classes) {
            if (!std::ranges::contains(names, name)) {
                names.push_back(name);
            }
        }
    }
}

} // namespace

SkinElementClasses SkinCatalog::classesOf(const Skin& skin, std::string_view document) const {
    SkinElementClasses classes;
    for (const Skin* layer : chainOf(skin).value_or(std::vector<const Skin*>{})) {
        mergeClasses(*layer, document, classes);
    }
    return classes;
}

SkinElementClasses SkinCatalog::declaredClassesOf(std::string_view document) const {
    SkinElementClasses classes;
    for (const Skin& skin : skins_) {
        mergeClasses(skin, document, classes);
    }
    return classes;
}

std::vector<std::string> SkinCatalog::classedDocumentsOf(const Skin& skin) const {
    std::vector<std::string> documents;
    for (const Skin* layer : chainOf(skin).value_or(std::vector<const Skin*>{})) {
        for (const auto& [document, classes] : layer->documentClasses) {
            if (!std::ranges::contains(documents, document)) {
                documents.push_back(document);
            }
        }
    }
    return documents;
}

std::vector<SkinSpriteSheet> SkinCatalog::spriteSheetsOf(const Skin& skin) const {
    const std::vector<const Skin*> chain = chainOf(skin).value_or(std::vector<const Skin*>{});
    return foldSpriteSheets(skin, chain).sheets;
}

} // namespace dwe::ui
