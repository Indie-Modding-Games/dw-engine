#include "save_flow.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <utility>

namespace dwe::app {
namespace {

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
// with a file can be continued from, or says why it cannot.
bool listedIn(SlotListMode mode, const SlotInfo& info) {
  if (mode == SlotListMode::NewGame) {
    return info.slot.kind == SlotKind::Manual && info.state == SlotInfo::State::Empty;
  }
  return info.state != SlotInfo::State::Empty;
}

std::string keyOf(const std::function<std::string(std::string_view)>& name, std::string_view id) {
  return name && !id.empty() ? name(id) : std::string{};
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
  add("species", header.summary.species);
  add("species_key", keyOf(names.species, header.summary.species));
  add("map", header.summary.map);
  add("map_key", keyOf(names.map, header.summary.map));
  add("play_seconds", static_cast<double>(header.playSeconds));
  add("updated", info.header ? std::format("{:%Y-%m-%d}", header.updated) : std::string{});
}

} // namespace

SaveFlow::SaveFlow(const game::save::SaveSlots* slots, SaveNames names)
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
  if (slots_ != nullptr) {
    for (SlotInfo& info : slots_->list()) {
      if (listedIn(mode, info)) {
        entries_.push_back(std::move(info));
      }
    }
  }
  screens::ScreenSettings settings{
      {.name = "save.slots.mode",
       .value = std::string{mode_ == SlotListMode::NewGame ? "new" : "continue"}},
      {.name = "save.slots", .value = static_cast<double>(entries_.size())}};
  for (std::size_t index = 0; index < entries_.size(); ++index) {
    addEntry(settings, index, entries_[index], names_);
  }
  return settings;
}

std::optional<SaveDialog> SaveFlow::choose(std::size_t entry) {
  pending_.reset();
  if (entry >= entries_.size()) {
    return std::nullopt;
  }
  const SlotInfo& info = entries_[entry];
  const std::string file = game::save::slotFileName(info.slot);
  switch (info.state) {
  case SlotInfo::State::Empty:
    pending_ = PlayStart{
        .load = std::nullopt, .backup = false, .saveTo = info.slot, .choice = std::nullopt};
    return SaveDialog{.text = "save.confirm_new", .file = {}, .question = true};
  case SlotInfo::State::Saved:
    pending_ = PlayStart{
        .load = info.slot, .backup = false, .saveTo = std::nullopt, .choice = std::nullopt};
    return SaveDialog{.text = "save.confirm_continue", .file = {}, .question = true};
  case SlotInfo::State::Damaged:
    if (!info.hasBackup) {
      return SaveDialog{.text = "save.damaged_no_backup", .file = file, .question = false};
    }
    pending_ = PlayStart{
        .load = info.slot, .backup = true, .saveTo = std::nullopt, .choice = std::nullopt};
    return SaveDialog{.text = "save.damaged_use_backup", .file = file, .question = true};
  case SlotInfo::State::Newer:
    break;
  }
  return SaveDialog{.text = "save.too_new", .file = file, .question = false};
}

SaveDialog SaveFlow::chooseWithoutSaving() {
  pending_ = PlayStart{};
  return {.text = "save.slots.no_save_warning", .file = {}, .question = true};
}

std::optional<PlayStart> SaveFlow::answer(bool yes) {
  std::optional<PlayStart> start = yes ? pending_ : std::nullopt;
  pending_.reset();
  return start;
}

SaveDialog SaveFlow::startFailed(const PlayStart& start, bool saveFailed) {
  if (saveFailed && start.saveTo) {
    return {
        .text = "save.failed", .file = game::save::slotFileName(*start.saveTo), .question = false};
  }
  if (!start.load) {
    return {.text = "save.start_failed", .file = {}, .question = false};
  }
  return {.text = "save.load_failed",
          .file = start.backup ? game::save::slotBackupName(*start.load)
                               : game::save::slotFileName(*start.load),
          .question = false};
}

screens::ScreenSettings dialogSettings(const SaveDialog& dialog) {
  return {{.name = "dialog.text", .value = dialog.text},
          {.name = "dialog.file", .value = dialog.file},
          {.name = "dialog.question", .value = dialog.question}};
}

} // namespace dwe::app
