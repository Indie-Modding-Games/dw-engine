#include <dwe/core/log.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/game_clear.hpp>
#include <dwe/session/game_session.hpp>

#include <cstddef>
#include <utility>

namespace dwe::session {

RegisteredPartner endingGiftRecord(RegisteredPartner partner, const EndingGift& gift) {
  partner.species = gift.species;
  for (std::size_t place = 0; place < RegisteredPartner::Techniques; ++place) {
    partner.techniques.at(place) = place < gift.techniques.size()
                                       ? std::optional<std::string>{gift.techniques[place]}
                                       : std::nullopt;
  }
  return partner;
}

core::Result<void> clearGame(GameSession& session, BattleRegistry& registry,
                             const BattleAdapter& battle, const std::optional<EndingGift>& gift,
                             std::optional<std::string> continueMap) {
  session.markGameCleared(std::move(continueMap));
  if (!gift) {
    return {};
  }
  auto partner = registeredPartnerOf(session, battle);
  if (!partner) {
    return std::unexpected{partner.error()};
  }
  core::logDebug("[port:save] awardMachinedramonData (main_menu.c:5299): record {} is {} as {}",
                 gift->slot, partner->name, gift->species);
  return registry.registerAt(gift->slot, endingGiftRecord(std::move(*partner), *gift));
}

} // namespace dwe::session
