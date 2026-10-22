#include "dev_assets_banner.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/content/dev_assets.hpp>
#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>

#include <string_view>

#include "messages.hpp"

namespace dwe::app {
namespace {

constexpr std::string_view BannerDocument = "dev_assets_banner";
constexpr std::string_view BannerDocumentId = "ui/dev_assets/document";

} // namespace

core::Result<void> showDevAssetsBanner(ui::Ui& ui) {
  auto document = assets::AssetId::parse(BannerDocumentId);
  if (!document) {
    return std::unexpected{document.error()};
  }
  if (auto loaded = ui.loadDocument(BannerDocument, *document); !loaded) {
    return loaded;
  }
  ui.show(BannerDocument);
  ui.setSetting(DevAssetsBannerSetting, ui.isVisible(BannerDocument));
  core::logWarning(messages::DevAssetsBanner, localization::tr(content::DevAssetsBannerKey));
  return {};
}

} // namespace dwe::app
