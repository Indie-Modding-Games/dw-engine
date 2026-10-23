#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/inventory/use.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>

#include "field_scene.hpp"

namespace dwe::app {

// Giving the partner an item from the bag in the field (game_menu.c: startFeedingItem; partner.c:
// partnerTickFeedItem). The player stands while the game time runs; the partner walks to the
// distance it takes the item from, the player holds it out and the partner eats it, which then
// applies, or refuses it, and the item stays in the bag. Without the ruleset's feed rules the bag
// uses the item at once.
class FeedScene {
public:
  // Everything must outlive this object.
  FeedScene(session::GameSession& session, session::InventoryAdapter& inventory,
            const session::FieldItemsAdapter& fieldItems, FieldScene& scene);

  [[nodiscard]] bool active() const { return step_ != Step::None; }

  // Keeps the pause menu shut while the scene plays; false for any other command.
  [[nodiscard]] bool handle(const ui::UiCommand& command) const;

  // Once per frame: whether the bag's Use could start the scene, which needs the field idle.
  void update(bool fieldIdle) { fieldIdle_ = fieldIdle; }
  [[nodiscard]] bool canStart() const;

  // The bag's Use (startFeedingItem): offers the item to the partner, which decides whether it
  // takes it. A refusal or a taken item starts the scene; any other outcome starts nothing and is
  // for the bag to show.
  core::Result<game::inventory::UseOutcome> start(assets::ItemHandle item);

  // One tick while active, the field's and the game time's; true on the tick it ends, when the
  // item applied or the partner refused it.
  bool tick(float seconds);

private:
  enum class Step : std::uint8_t { None, Walk, Offer, Eat, Refuse, Shrug };

  void offer();
  void finish();
  [[nodiscard]] float frames(std::int32_t count) const;

  session::GameSession* session_;
  session::InventoryAdapter* inventory_;
  const session::FieldItemsAdapter* fieldItems_;
  FieldScene* scene_;
  Step step_ = Step::None;
  // The ruleset's feed rules while the scene plays.
  const session::FeedRules* feed_ = nullptr;
  // The item held out; the partner took it when the offer was Accepted.
  std::optional<assets::ItemHandle> item_;
  bool accepted_ = false;
  bool fieldIdle_ = false;
  // The time since the step's clip started.
  float seconds_ = 0.0F;
};

} // namespace dwe::app
