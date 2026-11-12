#include "code_trust.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/script/engine_code.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

#include "messages.hpp"
#include "rml_interfaces.hpp"

namespace dwe::ui {
namespace {

constexpr std::string_view DocumentExtension = ".rml";

} // namespace

void CodeTrust::beginLoad(std::string_view documentPath) {
  loading_ = true;
  reported_ = false;
  sandboxed_ = inAddon(documentPath);
  verdict_ = checkDocument(documentPath);
}

core::Result<void> CodeTrust::endLoad() {
  loading_ = false;
  return verdict_;
}

void CodeTrust::noteRead(std::string_view path) {
  if (!loading_ || !verdict_ || std::filesystem::path{path}.extension() != DocumentExtension) {
    return;
  }
  if (!sandboxed_ && inAddon(path)) {
    // The document's environment is the engine's: an addon's template would run in it.
    verdict_ = core::fail(core::formatMessage(messages::AddonTemplateInEngineDocument, path));
    return;
  }
  verdict_ = checkDocument(path);
}

core::Result<void> CodeTrust::loadVerdict() const {
  if (!loading_) {
    return core::fail(std::string{messages::CodeOutsideLoad});
  }
  return verdict_;
}

core::Result<void> CodeTrust::checkInlineSource(std::string_view path) const {
  if (auto load = loadVerdict(); !load) {
    return load;
  }
  return checkDocument(path);
}

core::Result<void> CodeTrust::checkScript(std::string_view path) const {
  if (loading_ && sandboxed_ && inAddon(path)) {
    return {};
  }
  auto id = assetIdOfPath(*files_, path);
  if (!id) {
    return std::unexpected{id.error()};
  }
  return script::checkEngineScript(*files_, *id);
}

void CodeTrust::reportRefusedInline() {
  if (reported_ || verdict_) {
    return;
  }
  reported_ = true;
  core::logError("ui: {}", verdict_.error().message);
}

void CodeTrust::allowAddon(std::string_view folder) {
  if (std::ranges::find(addonFolders_, folder) == addonFolders_.end()) {
    addonFolders_.emplace_back(folder);
  }
}

void CodeTrust::forgetAddons() {
  addonFolders_.clear();
}

bool CodeTrust::inAddon(std::string_view path) const {
  if (addonFolders_.empty()) {
    return false;
  }
  auto id = assetIdOfPath(*files_, path);
  if (!id) {
    return false;
  }
  return std::ranges::any_of(
      addonFolders_, [&](const std::string& folder) { return id->str().starts_with(folder); });
}

core::Result<void> CodeTrust::checkDocument(std::string_view path) const {
  if (inAddon(path)) {
    return {};
  }
  auto id = assetIdOfPath(*files_, path);
  if (!id) {
    return std::unexpected{id.error()};
  }
  return script::checkEngineDocument(*files_, *id);
}

} // namespace dwe::ui
