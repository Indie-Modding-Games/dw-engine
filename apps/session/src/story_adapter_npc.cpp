#include <dwe/core/log.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_adapter.hpp>

#include <cstdint>
#include <limits>
#include <memory>

namespace dwe::session {

core::Result<void> StoryAdapter::wireNpcs() {
  npcs_ = std::make_unique<StoryNpcs>();
  // script_interp.c: spawn_npc and unload_entity call scriptPauseGame(0xff) first.
  npcOrders_ = std::make_unique<game::story::NpcOrders>(*npcs_, [this] {
    if (runner_) {
      runner_->freezeWorld();
    }
  });
  if (auto published = host_->publishNpcApi(*npcOrders_); !published) {
    return core::fail(messages::formatMessage(messages::StoryApiFailed, published.error().message));
  }
  return {};
}

void StoryAdapter::attachNpcs(game::field::FieldNpcs* npcs) {
  if (npcs_ == nullptr) {
    return;
  }
  npcs_->attach(npcs, npcs != nullptr ? &digimon_ : nullptr);
}

void StoryAdapter::followNpcHolds() {
  const bool runs = runner_ && runner_->busy();
  // script_interp.c:638: when the script ends, every NPC a pause held moves again.
  if (scriptRan_ && !runs) {
    npcOrders_->releaseAll();
  }
  scriptRan_ = runs;
}

void StoryAdapter::runNpcContacts(std::span<const game::field::NpcContact> contacts) {
  if (!runner_) {
    return;
  }
  for (const game::field::NpcContact& contact : contacts) {
    if (!std::in_range<std::uint16_t>(contact.scriptId)) {
      core::logError("[port:npc] callScriptSection (tamer.c:677): script id {} is no section",
                     contact.scriptId);
      continue;
    }
    const auto outcome = runner_->requestMapSection(static_cast<std::uint16_t>(contact.scriptId),
                                                    game::story::StartTiming::NextFrame,
                                                    game::story::SectionSource::Event);
    core::logDebug("[port:npc] callScriptSection (tamer.c:677, overworld.c:1333): NPC section {} "
                   "({}): {}",
                   contact.scriptId,
                   contact.cause == game::field::NpcContactCause::Touch ? "touch" : "confirm",
                   outcome == game::story::RequestOutcome::Accepted ? "accepted"
                                                                    : "dropped, a script runs");
  }
}

} // namespace dwe::session
