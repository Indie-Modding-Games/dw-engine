#include "skin_styler.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/ui/image_reference.hpp>
#include <dwe/ui/sprite_sheet_rcss.hpp>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>

#include <algorithm>
#include <utility>

#include "messages.hpp"

namespace dwe::ui {
namespace {

// The path RmlUi reads an asset by: its id and the extension of the file that provides it.
std::string pathOf(const assets::VirtualFileSystem& files, const assets::AssetId& id) {
    const assets::AssetSource* source = files.resolve(id);
    return source == nullptr ? id.str() : id.str() + assets::pathText(source->file.extension());
}

} // namespace

void SkinStyler::use(const Skin* skin) {
    skin_ = skin;
    forget();
}

void SkinStyler::forget() {
    sheets_.clear();
    spriteSheets_ = skin_ != nullptr ? buildSpriteSheets() : nullptr;
}

assets::AssetId SkinStyler::documentOf(std::string_view screen, const assets::AssetId& own) const {
    if (skin_ == nullptr) {
        return own;
    }
    return catalog_->lookOf(*skin_, screen).document.value_or(own);
}

std::vector<std::string> SkinStyler::style(Rml::ElementDocument& document, std::string_view screen,
                                           const Rml::StyleSheetContainer& own) {
    const ScreenSheet& look = sheetOf(screen);
    document.SetStyleSheetContainer(own.CombineStyleSheetContainer(*look.sheet));
    applyClasses(document, screen);
    return look.paths;
}

void SkinStyler::applyClasses(Rml::ElementDocument& document, std::string_view screen) const {
    for (const auto& [id, classes] : catalog_->declaredClassesOf(screen)) {
        if (Rml::Element* element = document.GetElementById(id)) {
            for (const std::string& name : classes) {
                element->SetClass(name, false);
            }
        }
    }
    if (skin_ == nullptr) {
        return;
    }
    for (const auto& [id, classes] : catalog_->classesOf(*skin_, screen)) {
        Rml::Element* element = document.GetElementById(id);
        if (element == nullptr) {
            core::logWarning("{}", core::formatMessage(messages::SkinClassesUnknownElement, skin_->id, id, screen));
            continue;
        }
        for (const std::string& name : classes) {
            element->SetClass(name, true);
        }
    }
}

std::vector<std::string> SkinStyler::unknownClassedDocuments(std::span<const std::string> loaded) const {
    std::vector<std::string> unknown;
    if (skin_ == nullptr) {
        return unknown;
    }
    for (std::string& document : catalog_->classedDocumentsOf(*skin_)) {
        if (!std::ranges::contains(loaded, document)) {
            core::logWarning("{}", core::formatMessage(messages::SkinClassesUnknownDocument, skin_->id, document));
            unknown.push_back(std::move(document));
        }
    }
    return unknown;
}

const SkinStyler::ScreenSheet& SkinStyler::sheetOf(std::string_view screen) {
    if (const auto found = sheets_.find(screen); found != sheets_.end()) {
        return found->second;
    }
    ScreenSheet built{.sheet = Rml::MakeShared<Rml::StyleSheetContainer>(), .paths = {}};
    if (skin_ != nullptr) {
        if (spriteSheets_ != nullptr) {
            built.sheet->MergeStyleSheetContainer(*spriteSheets_);
        }
        for (const assets::AssetId& style : catalog_->lookOf(*skin_, screen).styles) {
            std::string path = pathOf(*files_, style);
            if (auto sheet = Rml::Factory::InstanceStyleSheetFile(path)) {
                built.sheet->MergeStyleSheetContainer(*sheet);
            } else {
                core::logWarning("{}", core::formatMessage(messages::SkinStyleUnreadable, path, skin_->id));
            }
            built.paths.push_back(std::move(path));
        }
    }
    return sheets_.emplace(std::string{screen}, std::move(built)).first->second;
}

Rml::SharedPtr<Rml::StyleSheetContainer> SkinStyler::buildSpriteSheets() const {
    std::string rcss;
    for (const SkinSpriteSheet& sheet : catalog_->spriteSheetsOf(*skin_)) {
        // Only the size: RmlUi reads the pixels itself when a sprite is drawn.
        auto size = imageSize(*files_, sheet.image);
        if (!size) {
            core::logWarning(
                "{}", core::formatMessage(messages::SpriteSheetUnusable, sheet.name, skin_->id, size.error().message));
            continue;
        }
        auto text = spriteSheetRcss(sheet, size->width, size->height);
        if (!text) {
            core::logWarning(
                "{}", core::formatMessage(messages::SpriteSheetUnusable, sheet.name, skin_->id, text.error().message));
            continue;
        }
        rcss += *text;
    }
    return rcss.empty() ? nullptr : Rml::Factory::InstanceStyleSheetString(rcss);
}

} // namespace dwe::ui
