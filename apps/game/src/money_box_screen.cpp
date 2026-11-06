#include "money_box_screen.hpp"

#include <dwe/core/log.hpp>

#include <utility>

namespace dwe::app {

namespace {

// getShopkeeperLine (script_common.c:1174): operand 0xFF is a shop without an owner.
constexpr std::int64_t NoOwner = 0xFF;

} // namespace

screens::ScreenSettings moneyBoxSettings(std::string bitsKey, std::int64_t money) {
  return {{.name = "money_box.bits_key", .value = std::move(bitsKey)},
          {.name = "money_box.money", .value = static_cast<double>(money)}};
}

MoneyBoxScreen::MoneyBoxScreen(MoneyBoxScreenDesc desc) : desc_(desc) {}

bool MoneyBoxScreen::take(const session::StoryScreenRequest& request, const FieldScene& scene) {
  if (request.screen == session::StoryScreen::CloseTextBox) {
    // triggerBoxCloseFlag(2): the second text box is the money box.
    close();
    return true;
  }
  if (request.screen != session::StoryScreen::MoneyBox) {
    return false;
  }
  // createShopBitsBox(1): the keeper variable 254 names says its money line (getShopkeeperLine),
  // the map's NPC with that script id, by its species.
  session::ShopVisit keeper{
      .ownerless = request.first == NoOwner, .keeperSpecies = {}, .recycle = false};
  for (const auto& npc : scene.map().npcs) {
    if (!keeper.ownerless && npc.scriptId == request.first && npc.digimon) {
      keeper.keeperSpecies = *npc.digimon;
    }
  }
  std::string key;
  std::int64_t money = 0;
  if (desc_.shop != nullptr && desc_.shop->available()) {
    key = desc_.shop->keeperLineKey(keeper, desc_.shop->rules().lines.bits);
    money = desc_.shop->money();
  }
  core::logDebug("[port:script] createShopBitsBox (script_common.c:1757) and tickShopBitsBox: "
                 "keeper '{}', line '{}', money {}",
                 keeper.keeperSpecies, key, money);
  if (desc_.ui != nullptr) {
    screens::publish(*desc_.ui, moneyBoxSettings(std::move(key), money), MoneyBoxRevisionSetting);
    desc_.ui->show(MoneyBoxDocument);
  }
  shown_ = true;
  return true;
}

void MoneyBoxScreen::close() {
  if (shown_ && desc_.ui != nullptr) {
    desc_.ui->hide(MoneyBoxDocument);
  }
  shown_ = false;
}

} // namespace dwe::app
