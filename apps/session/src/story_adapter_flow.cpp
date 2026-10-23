#include <dwe/core/log.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_adapter.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace dwe::session {

core::Result<void> StoryAdapter::wireFlow() {
  mapChanges_ = std::make_unique<StoryMapChanges>();
  fieldControl_ = std::make_unique<StoryFieldControl>();
  if (auto published =
          host_->publishFlowApi(*orders_, [this] { return runner_ ? &*runner_ : nullptr; });
      !published) {
    return core::fail(messages::formatMessage(messages::StoryApiFailed, published.error().message));
  }
  return {};
}

// The hold on the player (to the field the app sets, setFieldHoldTarget) and the map changes
// (StoryFlowPorts). The story's state comes from startRunner, which knows its port.
game::story::StoryFlowPorts StoryAdapter::flowPorts() const {
  return {.state = nullptr, .control = fieldControl_.get(), .mapChange = mapChanges_.get()};
}

void StoryAdapter::setMapChangeTarget(StoryMapChangeTarget target) {
  if (mapChanges_ != nullptr) {
    mapChanges_->setTarget(std::move(target));
  }
}

void StoryAdapter::setFieldHoldTarget(StoryHoldTarget target) {
  if (fieldControl_ != nullptr) {
    fieldControl_->setTarget(std::move(target));
  }
}

void StoryAdapter::mapEntered(std::int32_t map) {
  enteredMap_ = map;
  if (!runner_) {
    return;
  }
  core::logDebug("[port:story] changeMap (map.c: changeMap): map {} entered", map);
  runner_->enterMap(map);
}

void StoryAdapter::playUnplayedChange() {
  if (mapChanges_ == nullptr || !mapChanges_->takeUnplayed() || !enteredMap_) {
    return;
  }
  // The change the field could not play ends on the map the player is on.
  runner_->enterMap(*enteredMap_);
}

void StoryAdapter::runCellSections(std::span<const std::int32_t> sections) {
  if (!runner_) {
    return;
  }
  for (const std::int32_t section : sections) {
    if (section < 0 || section > std::numeric_limits<std::uint16_t>::max()) {
      core::logError("[port:story] checkMapInteraction (tamer.c:660): cell {} is no section",
                     section);
      continue;
    }
    // A step cell asks every frame: only the accepted ones are logged.
    const auto outcome = runner_->requestMapSection(static_cast<std::uint16_t>(section),
                                                    game::story::StartTiming::NextFrame,
                                                    game::story::SectionSource::Plain);
    if (outcome == game::story::RequestOutcome::Accepted) {
      core::logDebug("[port:story] checkMapInteraction (tamer.c:660): cell section {}", section);
    }
  }
}

bool StoryAdapter::scriptsMap(std::int32_t map) const {
  return runner_ && runner_->scriptsMap(map);
}

} // namespace dwe::session
