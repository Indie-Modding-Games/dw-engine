#include "game_saves.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::app {

using game::save::SlotInfo;

std::string_view saveResultName(SaveResult result) {
  switch (result) {
  case SaveResult::Saved:
    return "saved";
  case SaveResult::Failed:
    return "failed";
  case SaveResult::NotSaved:
    break;
  }
  return "not_saved";
}

GameSaves::GameSaves(const game::save::SaveSlots* slots, Write write)
    : slots_(slots), write_(std::move(write)) {}

SaveStep GameSaves::save(const Playthrough& playthrough) {
  pending_.reset();
  if (slots_ == nullptr || !playthrough.home) {
    return {.dialog = std::nullopt, .result = SaveResult::NotSaved};
  }
  const game::save::SlotId home = *playthrough.home;
  const std::vector<SlotInfo> slots = slots_->list();
  const auto info = std::ranges::find(slots, home, &SlotInfo::slot);
  const std::string file = game::save::slotFileName(home);
  if (info != slots.end()) {
    switch (info->state) {
    case SlotInfo::State::Newer:
      return {.dialog = SaveDialog{.text = "save.home_newer",
                                   .file = file,
                                   .question = false,
                                   .startOnYes = false},
              .result = SaveResult::NotSaved};
    case SlotInfo::State::Damaged:
      pending_ = home;
      return {.dialog = SaveDialog{.text = "save.home_damaged",
                                   .file = file,
                                   .question = true,
                                   .startOnYes = false},
              .result = std::nullopt};
    case SlotInfo::State::Saved:
      if (!info->header || info->header->created != playthrough.started) {
        pending_ = home;
        return {.dialog = SaveDialog{.text = "save.home_other",
                                     .file = file,
                                     .question = true,
                                     .startOnYes = false},
                .result = std::nullopt};
      }
      break;
    case SlotInfo::State::Empty:
      break;
    }
  }
  return write(home);
}

SaveStep GameSaves::answer(bool yes) {
  const std::optional<game::save::SlotId> slot = std::exchange(pending_, std::nullopt);
  if (!slot) {
    return {};
  }
  if (!yes) {
    return {.dialog = std::nullopt, .result = SaveResult::NotSaved};
  }
  return write(*slot);
}

SaveStep GameSaves::write(game::save::SlotId slot) {
  if (auto written = write_(slot); !written) {
    core::logError(messages::GameSaveFailed, written.error().message);
    return {.dialog = SaveDialog{.text = "save.failed",
                                 .file = game::save::slotFileName(slot),
                                 .question = false,
                                 .startOnYes = false},
            .result = SaveResult::Failed};
  }
  return {.dialog = std::nullopt, .result = SaveResult::Saved};
}

} // namespace dwe::app
