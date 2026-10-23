#include <dwe/core/log.hpp>
#include <dwe/session/cards_adapter.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "story_game_context.hpp"

// The story's orders and read of the player's cards (script_interp.c:1143-1157: give_card,
// take_card; script_value.c:291: scriptCompareCard).
namespace dwe::session {

namespace {

using script::ApiValue;

// The card number of an operand; a number outside a card's byte is no card of the table.
std::int32_t cardOf(std::int64_t card) {
  return std::in_range<std::int32_t>(card) ? static_cast<std::int32_t>(card) : -1;
}

core::Result<ApiValue> changeCard(const StoryGameContext& context, std::int64_t card, bool gives) {
  CardsAdapter* cards = context.desc.cards;
  if (cards == nullptr) {
    core::logDebug("[port:story] {} {}: the story orders have no cards, nothing",
                   gives ? "give_card" : "take_card", card);
    return ApiValue{};
  }
  return done(gives ? cards->giveCopy(cardOf(card)) : cards->takeCopy(cardOf(card)));
}

core::Result<ApiValue> cardCount(const StoryGameContext& context, std::int64_t card) {
  const CardsAdapter* cards = context.desc.cards;
  // scriptCompareCard compares getCardAmount; a card the player never had counts 0.
  return ApiValue{std::int64_t{cards == nullptr ? 0 : cards->copies(cardOf(card))}};
}

} // namespace

core::Result<void> publishCardOrders(script::ApiRegistry& api, StoryGameContext& context) {
  namespace names = story_game_api;
  const StoryGameContext* game = &context;
  using Operands1 = std::array<std::int64_t, 1>;
  return publishOrders(api, {{names::GiveCard, integerOrder<1>([game](const Operands1& operands) {
                                return changeCard(*game, operands[0], true);
                              })},
                             {names::TakeCard, integerOrder<1>([game](const Operands1& operands) {
                                return changeCard(*game, operands[0], false);
                              })},
                             {names::Cards, integerOrder<1>([game](const Operands1& operands) {
                                return cardCount(*game, operands[0]);
                              })}});
}

} // namespace dwe::session
