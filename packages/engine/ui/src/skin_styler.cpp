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
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
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
  applyLayout(document, screen);
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
      core::logWarning(
          "{}", core::formatMessage(messages::SkinClassesUnknownElement, skin_->id, id, screen));
      continue;
    }
    for (const std::string& name : classes) {
      element->SetClass(name, true);
    }
  }
}

void SkinStyler::applyLayout(Rml::ElementDocument& document, std::string_view screen) const {
  const Rml::String present = document.GetClassNames();
  for (const auto part : std::views::split(std::string_view{present}, ' ')) {
    const std::string_view name{part};
    if (name.starts_with(LayoutClassPrefix)) {
      document.SetClass(Rml::String{name}, false);
    }
  }
  if (skin_ == nullptr) {
    return;
  }
  for (const auto& [option, value] : catalog_->layoutOf(*skin_, screen)) {
    document.SetClass(std::format("{}{}-{}", LayoutClassPrefix, option, value), true);
  }
}

std::optional<std::string> SkinStyler::layoutOption(std::string_view screen,
                                                    std::string_view option) const {
  if (skin_ == nullptr) {
    return std::nullopt;
  }
  const SkinScreenLayout options = catalog_->layoutOf(*skin_, screen);
  const auto found = options.find(option);
  return found == options.end() ? std::nullopt : std::optional<std::string>{found->second};
}

std::vector<std::string>
SkinStyler::unknownClassedDocuments(std::span<const std::string> loaded) const {
  std::vector<std::string> unknown;
  if (skin_ == nullptr) {
    return unknown;
  }
  for (std::string& document : catalog_->classedDocumentsOf(*skin_)) {
    if (!std::ranges::contains(loaded, document)) {
      core::logWarning(
          "{}", core::formatMessage(messages::SkinClassesUnknownDocument, skin_->id, document));
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

std::vector<std::string> SkinStyler::spriteImages() const {
  std::vector<std::string> images;
  if (skin_ == nullptr) {
    return images;
  }
  for (const SkinSpriteSheet& sheet : catalog_->spriteSheetsOf(*skin_)) {
    images.push_back(imageReferenceText(imageOf(sheet)));
  }
  return images;
}

bool SkinStyler::setSmoothPictures(bool smooth) {
  if (smooth == smoothPictures_) {
    return false;
  }
  smoothPictures_ = smooth;
  forget();
  return true;
}

ImageReference SkinStyler::imageOf(const SkinSpriteSheet& sheet) const {
  ImageReference image = sheet.image;
  image.smooth = sheet.smooth && smoothPictures_;
  return image;
}

Rml::SharedPtr<Rml::StyleSheetContainer> SkinStyler::buildSpriteSheets() const {
  std::string rcss;
  for (const SkinSpriteSheet& sheet : catalog_->spriteSheetsOf(*skin_)) {
    // Only the size: RmlUi reads the pixels itself when a sprite is drawn.
    auto size = imageSize(*files_, sheet.image);
    if (!size) {
      core::logWarning("{}", core::formatMessage(messages::SpriteSheetUnusable, sheet.name,
                                                 skin_->id, size.error().message));
      continue;
    }
    SkinSpriteSheet drawn = sheet;
    drawn.image = imageOf(sheet);
    auto text = spriteSheetRcss(drawn, size->width, size->height);
    if (!text) {
      core::logWarning("{}", core::formatMessage(messages::SpriteSheetUnusable, sheet.name,
                                                 skin_->id, text.error().message));
      continue;
    }
    rcss += *text;
  }
  return rcss.empty() ? nullptr : Rml::Factory::InstanceStyleSheetString(rcss);
}

} // namespace dwe::ui
