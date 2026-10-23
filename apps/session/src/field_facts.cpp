#include <dwe/assets/item_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/session/field_facts.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_game_orders.hpp>

#include <cstdint>
#include <optional>

#include "story_game_context.hpp"

namespace dwe::session {

namespace {

// The partner's conditions as the original's bits (params.h: CONDITION_*), which game/partner's
// Condition follows.
std::uint32_t conditionBits(const game::partner::Conditions& conditions) {
  std::uint32_t bits = 0;
  for (std::size_t condition = 0; condition < game::partner::ConditionCount; ++condition) {
    if (conditions.has(static_cast<game::partner::Condition>(condition))) {
      bits |= 1U << condition;
    }
  }
  return bits;
}

} // namespace

game::field::GuardFacts fieldGuardFacts(const GameSession& session,
                                        const InventoryAdapter* inventory,
                                        const StoryAdapter& story) {
  game::field::GuardFacts facts{[&story](std::int32_t trigger) -> std::optional<bool> {
    const auto set = story.state().trigger(trigger);
    return set ? std::optional<bool>{*set} : std::nullopt;
  }};
  if (inventory != nullptr) {
    facts.itemCount = [inventory](std::int32_t item) -> std::optional<std::int64_t> {
      const assets::ItemEntry* entry = inventory->items().findByOriginalIndex(item);
      if (entry == nullptr) {
        return std::nullopt;
      }
      return inventory->inventory().countOf(inventory->items().handleOf(*entry));
    };
    facts.money = [inventory]() -> std::optional<std::int64_t> {
      return inventory->inventory().money();
    };
  }
  facts.stat = [&session](std::int32_t stat) -> std::optional<std::int64_t> {
    if (!session.playing() || stat < 0 || stat > static_cast<std::int32_t>(ScriptStat::Lives)) {
      return std::nullopt;
    }
    return scriptStatOf(session, static_cast<ScriptStat>(stat));
  };
  facts.conditions = [&session]() -> std::optional<std::uint32_t> {
    if (!session.playing()) {
      return std::nullopt;
    }
    return conditionBits(session.partner().state().conditions);
  };
  return facts;
}

game::field::TriggerWriter fieldTriggerWriter(StoryAdapter& story) {
  return [&story](std::int32_t trigger, bool set) {
    if (auto written = story.state().setTrigger(trigger, set); !written) {
      core::logWarning("{}", written.error().message);
    }
  };
}

} // namespace dwe::session
