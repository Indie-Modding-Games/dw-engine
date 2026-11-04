#include "save_flow.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::app {
namespace {

using game::save::SlotId;
using game::save::SlotInfo;
using game::save::SlotKind;

constexpr std::string_view SlotList = "save.slot";

std::string_view stateName(SlotInfo::State state) {
  switch (state) {
  case SlotInfo::State::Empty:
    return "empty";
  case SlotInfo::State::Damaged:
    return "damaged";
  case SlotInfo::State::Newer:
    return "newer";
  case SlotInfo::State::Saved:
    break;
  }
  return "saved";
}

// Whether a list shows a slot: a new game is started only in an empty manual slot; any other slot
// with a file can be continued from, or says why it cannot; any slot with a file this game can
// write can be erased (a newer game's save is neither written over nor erased).
bool listedIn(SlotListMode mode, const SlotInfo& info) {
  switch (mode) {
  case SlotListMode::NewGame:
    return info.slot.kind == SlotKind::Manual && info.state == SlotInfo::State::Empty;
  case SlotListMode::Erase:
    return info.state != SlotInfo::State::Empty && info.state != SlotInfo::State::Newer;
  case SlotListMode::Continue:
    break;
  }
  return info.state != SlotInfo::State::Empty;
}

std::string_view modeName(SlotListMode mode) {
  switch (mode) {
  case SlotListMode::NewGame:
    return "new";
  case SlotListMode::Erase:
    return "erase";
  case SlotListMode::Continue:
    break;
  }
  return "continue";
}

std::string keyOf(const std::function<std::string(std::string_view)>& name, std::string_view id) {
  return name && !id.empty() ? name(id) : std::string{};
}

// The listed entry saved last, the one continuing starts on; nothing when none holds a save.
std::optional<std::size_t> newestEntry(const std::vector<SlotInfo>& listed) {
  std::optional<std::size_t> newest;
  for (std::size_t index = 0; index < listed.size(); ++index) {
    const SlotInfo& info = listed[index];
    if (info.state == SlotInfo::State::Saved && info.header &&
        (!newest || info.header->updated > listed[*newest].header->updated)) {
      newest = index;
    }
  }
  return newest;
}

void addEntry(screens::ScreenSettings& settings, std::size_t index, const SlotInfo& info,
              const SaveNames& names) {
  const auto add = [&](std::string_view field, ui::UiValue value) {
    settings.push_back(
        {.name = screens::listEntryName(SlotList, index, field), .value = std::move(value)});
  };
  add("kind", std::string{info.slot.kind == SlotKind::Manual ? "slot" : "autosave"});
  add("number", static_cast<double>(info.slot.index));
  add("state", std::string{stateName(info.state)});
  const game::save::SaveHeader header = info.header.value_or(game::save::SaveHeader{});
  // main_menu.c: drawMainMenuStrings shows the partner's own name beside its species.
  add("nickname", header.summary.nickname);
  add("species", header.summary.species);
  add("species_key", keyOf(names.species, header.summary.species));
  add("map", header.summary.map);
  add("map_key", keyOf(names.map, header.summary.map));
  add("play_seconds", static_cast<double>(header.playSeconds));
  add("updated", info.header ? std::format("{:%Y-%m-%d}", header.updated) : std::string{});
}

} // namespace

SaveFlow::SaveFlow(game::save::SaveSlots* slots, SaveNames names)
    : slots_(slots), names_(std::move(names)) {}

screens::ScreenSettings SaveFlow::titleSettings() const {
  bool saved = false;
  if (slots_ != nullptr) {
    const auto infos = slots_->list();
    saved = std::ranges::any_of(
        infos, [](const SlotInfo& info) { return info.state != SlotInfo::State::Empty; });
  }
  return {{.name = "save.can_continue", .value = saved}};
}

screens::ScreenSettings SaveFlow::open(SlotListMode mode) {
  mode_ = mode;
  entries_.clear();
  pending_.reset();
  pendingErase_.reset();
  double manual = 0.0;
  double autosave = 0.0;
  if (slots_ != nullptr) {
    for (SlotInfo& info : slots_->list()) {
      (info.slot.kind == SlotKind::Manual ? manual : autosave) += 1.0;
      if (listedIn(mode, info)) {
        entries_.push_back(std::move(info));
      }
    }
  }
  // How many slots of each kind there are, listed or not: a list that shows every slot (the
  // original look's, ui/main_menu) greys the ones with no entry. The autosave ones are the old
  // autosaves still on disk. Continuing starts on the save written last (save.slots.newest, -1 for
  // none).
  const std::optional<std::size_t> newest =
      mode_ == SlotListMode::Continue ? newestEntry(entries_) : std::nullopt;
  screens::ScreenSettings settings{
      {.name = "save.slots.mode", .value = std::string{modeName(mode_)}},
      {.name = "save.slots", .value = static_cast<double>(entries_.size())},
      {.name = "save.slots.manual", .value = manual},
      {.name = "save.slots.autosave", .value = autosave},
      {.name = "save.slots.newest", .value = newest ? static_cast<double>(*newest) : -1.0}};
  for (std::size_t index = 0; index < entries_.size(); ++index) {
    addEntry(settings, index, entries_[index], names_);
  }
  return settings;
}

std::optional<SaveDialog> SaveFlow::choose(std::size_t entry) {
  pending_.reset();
  pendingErase_.reset();
  if (entry >= entries_.size()) {
    return std::nullopt;
  }
  const SlotInfo& info = entries_[entry];
  const std::string file = game::save::slotFileName(info.slot);
  if (mode_ == SlotListMode::Erase) {
    // Erasing starts on no, so that confirming in a hurry does not lose a save.
    pendingErase_ = info.slot;
    return SaveDialog{
        .text = "save.confirm_erase", .file = file, .question = true, .startOnYes = false};
  }
  switch (info.state) {
  case SlotInfo::State::Empty:
    pending_ = PlayStart{
        .load = std::nullopt, .backup = false, .saveTo = info.slot, .choice = std::nullopt};
    // The original asks "Record new adventure?" with the cursor on yes (capture
    // cp01-record-new-adventure).
    return SaveDialog{.text = "save.confirm_new", .file = {}, .question = true, .startOnYes = true};
  case SlotInfo::State::Saved:
    pending_ = PlayStart{
        .load = info.slot, .backup = false, .saveTo = std::nullopt, .choice = std::nullopt};
    // On yes, as the new game's question: the player chose this save a moment ago, and starting on
    // no sent a hurried confirm back to the list.
    return SaveDialog{
        .text = "save.confirm_continue", .file = {}, .question = true, .startOnYes = true};
  case SlotInfo::State::Damaged:
    if (!info.hasBackup) {
      return SaveDialog{
          .text = "save.damaged_no_backup", .file = file, .question = false, .startOnYes = false};
    }
    pending_ = PlayStart{
        .load = info.slot, .backup = true, .saveTo = std::nullopt, .choice = std::nullopt};
    return SaveDialog{
        .text = "save.damaged_use_backup", .file = file, .question = true, .startOnYes = false};
  case SlotInfo::State::Newer:
    break;
  }
  return SaveDialog{.text = "save.too_new", .file = file, .question = false, .startOnYes = false};
}

SaveDialog SaveFlow::chooseWithoutSaving() {
  pending_ = PlayStart{.load = std::nullopt,
                       .backup = false,
                       .saveTo = std::nullopt,
                       .choice = std::nullopt,
                       .withoutSaving = true};
  return {.text = "save.slots.no_save_warning", .file = {}, .question = true, .startOnYes = false};
}

std::optional<PlayStart> SaveFlow::answer(bool yes) {
  pendingErase_.reset();
  std::optional<PlayStart> start = yes ? pending_ : std::nullopt;
  pending_.reset();
  return start;
}

std::optional<SaveDialog> SaveFlow::answerErase(bool yes) {
  const std::optional<SlotId> slot = std::exchange(pendingErase_, std::nullopt);
  pending_.reset();
  if (!yes || !slot || slots_ == nullptr) {
    return std::nullopt;
  }
  if (auto erased = slots_->erase(*slot); !erased) {
    core::logError(messages::CannotEraseSave, erased.error().message);
    return SaveDialog{.text = "save.erase_failed",
                      .file = game::save::slotFileName(*slot),
                      .question = false,
                      .startOnYes = false};
  }
  core::logInfo(messages::SaveErased, game::save::slotFileName(*slot));
  return std::nullopt;
}

SaveDialog SaveFlow::startFailed(const PlayStart& start, bool saveFailed) {
  if (saveFailed && start.saveTo) {
    return {.text = "save.failed",
            .file = game::save::slotFileName(*start.saveTo),
            .question = false,
            .startOnYes = false};
  }
  if (!start.load) {
    return {.text = "save.start_failed", .file = {}, .question = false, .startOnYes = false};
  }
  return {.text = "save.load_failed",
          .file = start.backup ? game::save::slotBackupName(*start.load)
                               : game::save::slotFileName(*start.load),
          .question = false,
          .startOnYes = false};
}

screens::ScreenSettings dialogSettings(const SaveDialog& dialog, bool atTitle) {
  return {{.name = "dialog.text", .value = dialog.text},
          {.name = "dialog.file", .value = dialog.file},
          {.name = "dialog.question", .value = dialog.question},
          {.name = "dialog.start_on_yes", .value = dialog.startOnYes},
          {.name = "dialog.at_title", .value = atTitle}};
}

} // namespace dwe::app
