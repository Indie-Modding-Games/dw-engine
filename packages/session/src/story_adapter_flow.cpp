#include <dwe/core/log.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_adapter.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace dwe::session {

core::Result<void> StoryAdapter::wireFlow() {
  mapChanges_ = std::make_unique<StoryMapChanges>();
  fieldControl_ = std::make_unique<StoryFieldControl>();
  actors_ = std::make_unique<StoryActorsLink>();
  motion_.emplace(*actors_, game::story::MotionRules{});
  if (auto published =
          host_->publishFlowApi(*orders_, [this] { return runner_ ? &*runner_ : nullptr; });
      !published) {
    return core::fail(messages::formatMessage(messages::StoryApiFailed, published.error().message));
  }
  return {};
}

// The hold on the player (to the field the app sets, setFieldHoldTarget), the map changes and the
// scripted moves (StoryFlowPorts). The story's state comes from startRunner, which knows its port.
game::story::StoryFlowPorts StoryAdapter::flowPorts() {
  return {.state = nullptr,
          .control = fieldControl_.get(),
          .mapChange = mapChanges_.get(),
          .frameSystems = motion_ ? std::vector<game::story::ScriptFrameSystem*>{&*motion_}
                                  : std::vector<game::story::ScriptFrameSystem*>{}};
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

void StoryAdapter::mapEntered(std::int32_t map, std::optional<std::int32_t> arrivalSection) {
  enteredMap_ = map;
  if (!runner_) {
    return;
  }
  core::logDebug("[port:story] changeMap (map.c: changeMap): map {} entered", map);
  if (arrivalSection) {
    if (std::in_range<std::uint16_t>(*arrivalSection)) {
      runner_->arriveWith(static_cast<std::uint16_t>(*arrivalSection));
    } else {
      core::logError("[port:story] warp_to (script_interp.c:1402): arrival {} is no section",
                     *arrivalSection);
    }
  }
  runMapHead(map);
}

void StoryAdapter::playUnplayedChange() {
  if (mapChanges_ == nullptr || !mapChanges_->takeUnplayed() || !enteredMap_) {
    return;
  }
  if (!runner_) {
    return;
  }
  // The change the field could not play ends on the map the player is on, without the arrival
  // section meant for the other map.
  runner_->dropMapChangeArrival();
  runMapHead(*enteredMap_);
}

void StoryAdapter::runCellSections(std::span<const std::int32_t> sections) {
  if (!runner_) {
    return;
  }
  // tamer.c: tamerTickWalkingState calls checkMapInteraction once a frame, and tickScript runs the
  // section it asked for in that same frame: a section that ends at once has ended before the next
  // frame's menu check (IS_SCRIPT_PAUSED is 1 again), so Triangle opens the menu on the cell. The
  // field ticks several times a frame here; asking only on the tick the frame runs keeps the
  // request from waiting between frames, where it shut the menu (and the talk) on the cell.
  if (!runner_->frameRunsNextTick()) {
    return;
  }
  std::vector<std::int32_t> asked;
  for (const std::int32_t section : sections) {
    if (!std::in_range<std::uint16_t>(section)) {
      core::logError("[port:story] checkMapInteraction (tamer.c:660): cell {} is no section",
                     section);
      continue;
    }
    // A step cell asks every frame: only an accepted one the last frame did not ask is logged.
    const auto outcome = runner_->requestMapSection(static_cast<std::uint16_t>(section),
                                                    game::story::StartTiming::NextFrame,
                                                    game::story::SectionSource::Cell);
    if (outcome == game::story::RequestOutcome::Accepted) {
      if (!std::ranges::contains(askedCells_, section)) {
        core::logDebug("[port:story] checkMapInteraction (tamer.c:660): cell section {}", section);
      }
      asked.push_back(section);
    }
  }
  askedCells_ = std::move(asked);
}

bool StoryAdapter::scriptsMap(std::int32_t map) const {
  return runner_ && runner_->scriptsMap(map);
}

} // namespace dwe::session
