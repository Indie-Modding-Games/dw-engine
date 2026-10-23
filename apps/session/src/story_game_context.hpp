#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/session/story_game_orders.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

// What the story's game orders share: the systems they change, the session of the tick, their
// rules, and the way they read their operands. Each area publishes its orders from its own file.
namespace dwe::session {

class GameSession;

// The names of the orders in the story namespace (story_lua.hpp: StoryNamespace), as the Lua
// emitter writes them (formats/script: storyFunctionName).
namespace story_game_api {
inline constexpr std::string_view GiveItem = "give_item";
inline constexpr std::string_view RemoveItem = "remove_item";
inline constexpr std::string_view SetInventorySize = "set_inventory_size";
inline constexpr std::string_view AddMoney = "add_money";
inline constexpr std::string_view ReduceMoney = "reduce_money";
inline constexpr std::string_view Items = "items";
inline constexpr std::string_view Money = "money";
inline constexpr std::string_view SetStat = "set_stat";
inline constexpr std::string_view AddStat = "add_stat";
inline constexpr std::string_view ReduceStat = "reduce_stat";
inline constexpr std::string_view Stat = "stat";
inline constexpr std::string_view RemoveCondition = "remove_condition";
inline constexpr std::string_view HasCondition = "has_condition";
inline constexpr std::string_view DealDamage = "deal_damage";
inline constexpr std::string_view LearnMove = "learn_move";
inline constexpr std::string_view KnowsMove = "knows_move";
inline constexpr std::string_view StorePartnerSpecies = "store_partner_species";
inline constexpr std::string_view StoreSpeciesType = "store_species_type";
inline constexpr std::string_view StoreDate = "store_date";
inline constexpr std::string_view AdvanceToDateAt = "advance_to_date_at";
inline constexpr std::string_view AddMinutesToDateAt = "add_minutes_to_date_at";
} // namespace story_game_api

struct StoryGameContext {
  StoryGameOrdersDesc desc;
  // The session of the last tick or new game; the orders run only inside a tick.
  GameSession* session = nullptr;
  StoryValueRules rules;
  // The species by the original's number, for the orders that store one in a variable.
  std::optional<assets::DigimonTable> digimon;

  [[nodiscard]] GameSession& game() const;
  // A story variable, and a write that keeps the value within a variable's byte, logged.
  [[nodiscard]] core::Result<std::int64_t> variable(std::int64_t variable) const;
  core::Result<void> setVariable(std::int64_t variable, std::int64_t value) const;
  core::Result<void> setTrigger(std::int64_t trigger, bool set) const;
};

// Each area's orders (story_orders_<area>.cpp).
core::Result<void> publishBagOrders(script::ApiRegistry& api, StoryGameContext& context);
core::Result<void> publishDateOrders(script::ApiRegistry& api, StoryGameContext& context);

// The story's change at the start of each day (script_common.c: dailyPStatTrigger).
void runDailyChange(const StoryGameContext& context);

// An order of N integer operands: reads them, then runs `run` with them as a std::array.
template <std::size_t N, typename Run>
script::ApiFunction integerOrder(Run run) {
  return [run = std::move(run)](const script::ApiCall& call) -> core::Result<script::ApiValue> {
    std::array<std::int64_t, N> operands{};
    for (std::size_t index = 0; index < N; ++index) {
      const auto operand = call.integer(index);
      if (!operand) {
        return std::unexpected{operand.error()};
      }
      operands.at(index) = *operand;
    }
    return run(operands);
  };
}

// Publishes each order in the story namespace; the first failure, if any.
core::Result<void>
publishOrders(script::ApiRegistry& api,
              std::initializer_list<std::pair<std::string_view, script::ApiFunction>> orders);

// The value an order that only changes the game returns to its script.
inline core::Result<script::ApiValue> done(const core::Result<void>& result) {
  if (!result) {
    return std::unexpected{result.error()};
  }
  return script::ApiValue{};
}

} // namespace dwe::session
