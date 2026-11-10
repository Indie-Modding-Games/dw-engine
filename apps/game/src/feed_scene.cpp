#include "feed_scene.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace dwe::app {

namespace {

using game::inventory::UseOutcome;

constexpr std::string_view OpenMenuCommand = "open_menu";
constexpr std::string_view NoFeedRules = "the ruleset has no field_items.feed";

const session::FeedRules* feedRulesOf(const session::FieldItemsAdapter& fieldItems) {
  const auto& rules = fieldItems.rules();
  if (rules.has_value() && rules->feed.has_value()) {
    return &*rules->feed;
  }
  return nullptr;
}

} // namespace

FeedScene::FeedScene(session::GameSession& session, session::InventoryAdapter& inventory,
                     const session::FieldItemsAdapter& fieldItems, FieldScene& scene)
    : session_{&session}, inventory_{&inventory}, fieldItems_{&fieldItems}, scene_{&scene} {}

bool FeedScene::handle(const ui::UiCommand& command) const {
  return command.name == OpenMenuCommand && active();
}

bool FeedScene::canStart() const {
  return fieldIdle_ && !active() && feedRulesOf(*fieldItems_) != nullptr && session_->playing() &&
         session_->partner().alive();
}

core::Result<UseOutcome> FeedScene::start(assets::ItemHandle item) {
  const session::FeedRules* feed = feedRulesOf(*fieldItems_);
  if (feed == nullptr) {
    return core::fail(std::string{NoFeedRules});
  }
  auto offered = inventory_->offer(*session_, item);
  if (!offered) {
    return std::unexpected{offered.error()};
  }
  const UseOutcome outcome = offered->outcome;
  if (outcome != UseOutcome::Accepted && outcome != UseOutcome::Refused) {
    return outcome;
  }
  // game_menu.c: startFeedingItem: tamer state 6, partner state 5, the menu closes and the game
  // time runs. The engine asks the partner here, not as the player holds the item out (sub state
  // 4): the same roll, a moment earlier.
  item_ = item;
  feed_ = feed;
  accepted_ = outcome == UseOutcome::Accepted;
  game::field::Field& field = scene_->field();
  field.holdForCare(true);
  field.walkPartnerToRange(feed->rangeOf(session_->partnerView().species));
  step_ = Step::Walk;
  core::logDebug("[port:feed] startFeedingItem (game_menu.c:2228): partner state 5, {}",
                 accepted_ ? "it takes it" : "it refuses it");
  return outcome;
}

float FeedScene::frames(std::int32_t count) const {
  return static_cast<float>(count) * scene_->field().rules().transition.frameSeconds;
}

void FeedScene::offer() {
  // partner.c: partnerTickFeedItem sub state 3: the player holds the item out.
  const session::FeedRules& feed = *feed_;
  scene_->setPlayerClip(feed.clips.playerOffer);
  scene_->playPartnerClip(feed.clips.partnerIdle);
  if (session_->sound() != nullptr) {
    session_->sound()->playSound(feed.sound.slot, feed.sound.sound);
  }
  core::logDebug("[port:feed] partnerTickFeedItem (partner.c:459): the player holds it out "
                 "(renderFeedingItem)");
  seconds_ = 0.0F;
  step_ = Step::Offer;
  showItem();
}

void FeedScene::showItem() {
  FeedItemPhase phase = FeedItemPhase::Walking;
  switch (step_) {
  case Step::Offer:
    phase = FeedItemPhase::Offering;
    break;
  case Step::Eat:
    phase = FeedItemPhase::Eating;
    break;
  case Step::Refuse:
    phase = FeedItemPhase::Refusing;
    break;
  case Step::Shrug:
    phase = FeedItemPhase::Shrugging;
    break;
  case Step::None:
  case Step::Walk:
    break;
  }
  const float frame = seconds_ / frames(1);
  std::optional<HeldItemSprite> held;
  if (item_ && feedItemHeld(phase, static_cast<std::int32_t>(frame))) {
    const assets::ItemEntry& entry = inventory_->items().at(*item_);
    if (const auto icon = entry.icon()) {
      held = HeldItemSprite{.node = FeedItemNode, .icon = icon->cell, .palette = icon->palette};
    }
  }
  scene_->setHeldItem(held);
}

// item.c: removeTamerItem: the item leaves the player's hand (TAMER_ITEM back to 0xFF) once the
// partner ate it or turned it down (partner.c, states 6 and 8).
void FeedScene::finish() {
  scene_->setHeldItem(std::nullopt);
  scene_->setPlayerClip(std::nullopt);
  scene_->playPartnerClip(std::nullopt);
  scene_->field().holdForCare(false);
  step_ = Step::None;
  item_.reset();
}

bool FeedScene::tick(float seconds) {
  if (!active() || feed_ == nullptr) {
    return false;
  }
  scene_->tick({}, seconds);
  session_->tick(seconds);
  scene_->finishTick();
  seconds_ += seconds;
  showItem();
  const session::FeedRules& feed = *feed_;
  switch (step_) {
  case Step::Walk:
    if (!scene_->field().partnerWalking()) {
      offer();
    }
    return false;
  case Step::Offer:
    // Sub state 4: past the offer clip's frame the partner eats it or refuses it.
    if (seconds_ > frames(feed.offerFrame)) {
      scene_->playPartnerClip(accepted_ ? feed.clips.partnerEat : feed.clips.partnerRefuse);
      seconds_ = 0.0F;
      step_ = accepted_ ? Step::Eat : Step::Refuse;
    }
    return false;
  case Step::Eat:
    // Sub states 5 and 6: the player lowers the hand on the eat clip's frame; the item applies
    // once the clip ends.
    if (seconds_ >= frames(feed.handDownFrame)) {
      scene_->setPlayerClip(std::nullopt);
    }
    if (scene_->partnerClipFinished() && item_.has_value()) {
      if (auto given = inventory_->give(*session_, *item_); !given) {
        core::logWarning("[port:feed] partnerTickFeedItem: {}", given.error().message);
      }
      core::logDebug("[port:feed] partnerTickFeedItem (partner.c:491): the partner ate it");
      finish();
      return true;
    }
    return false;
  case Step::Refuse:
    // Sub state 7: after the refusal the player shrugs.
    if (scene_->partnerClipFinished()) {
      scene_->playPartnerClip(feed.clips.partnerIdle);
      scene_->setPlayerClip(feed.clips.playerRefused);
      step_ = Step::Shrug;
    }
    return false;
  case Step::Shrug:
    // Sub state 8: the item stays in the bag.
    // A clip that repeats ends after its first pass, as anim.c's frame count does.
    if (scene_->playerClipPlayedThrough()) {
      core::logDebug("[port:feed] partnerTickFeedItem (partner.c:515): refused, kept");
      finish();
      return true;
    }
    return false;
  case Step::None:
    break;
  }
  return false;
}

void FeedScene::cheer(std::span<const game::partner::PartnerEvent> events) {
  const session::FeedRules* feed = feedRulesOf(*fieldItems_);
  const bool cheered = std::ranges::contains(events, game::partner::EventKind::Cheered,
                                             &game::partner::PartnerEvent::kind);
  if (feed == nullptr || !cheered) {
    return;
  }
  core::logDebug("[port:feed] partnerHandleFoodFeed (partner_impl.c:1641): glad, clip {}",
                 feed->clips.partnerCheer);
  scene_->playPartnerClipOnce(feed->clips.partnerCheer);
}

} // namespace dwe::app
