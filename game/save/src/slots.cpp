#include <dwe/game/save/slots.hpp>

#include <format>
#include <utility>

#include "messages.hpp"

namespace dwe::game::save {

namespace {

// How a slot ranks as the next autosave target: lower goes first.
int autosavePriority(const SlotInfo& info) {
  switch (info.state) {
  case SlotInfo::State::Empty:
    return 0;
  case SlotInfo::State::Damaged:
    return 1;
  case SlotInfo::State::Saved:
    break;
  }
  return 2;
}

bool goesBefore(const SlotInfo& candidate, const SlotInfo& best) {
  const int candidatePriority = autosavePriority(candidate);
  const int bestPriority = autosavePriority(best);
  if (candidatePriority != bestPriority) {
    return candidatePriority < bestPriority;
  }
  return candidate.header && best.header && candidate.header->updated < best.header->updated;
}

} // namespace

std::string slotFileName(SlotId slot) {
  return std::format("{}_{:02}{}",
                     slot.kind == SlotKind::Manual ? ManualSlotPrefix : AutosaveSlotPrefix,
                     slot.index, SlotFileExtension);
}

std::string slotBackupName(SlotId slot) {
  return std::format("{}{}", slotFileName(slot), BackupSuffix);
}

SaveSlots::SaveSlots(SaveStorage& storage, const SaveSettings& settings)
    : storage_{&storage}, manualSlots_{settings.manualSlots},
      autosaveSlots_{settings.autosaveSlots} {}

std::int32_t SaveSlots::slotCount(SlotKind kind) const {
  return kind == SlotKind::Manual ? manualSlots_ : autosaveSlots_;
}

std::vector<SlotId> SaveSlots::slots() const {
  std::vector<SlotId> all;
  for (const SlotKind kind : {SlotKind::Manual, SlotKind::Autosave}) {
    for (std::int32_t index = 1; index <= slotCount(kind); ++index) {
      all.push_back({.kind = kind, .index = index});
    }
  }
  return all;
}

SlotInfo SaveSlots::readInfo(SlotId slot) const {
  SlotInfo info;
  info.slot = slot;
  const auto backup = storage_->read(slotBackupName(slot));
  info.hasBackup = backup && backup->has_value();
  const auto text = storage_->read(slotFileName(slot));
  if (text && !text->has_value()) {
    return info;
  }
  auto file =
      text.and_then([](const std::optional<std::string>& saved) { return parseSaveFile(*saved); });
  if (!file) {
    info.state = SlotInfo::State::Damaged;
    info.problem = std::format(messages::InFile, slotFileName(slot), file.error().message);
    return info;
  }
  info.state = SlotInfo::State::Saved;
  info.header = std::move(file->header);
  return info;
}

std::vector<SlotInfo> SaveSlots::list() const {
  std::vector<SlotInfo> infos;
  for (const SlotId slot : slots()) {
    infos.push_back(readInfo(slot));
  }
  return infos;
}

core::Result<void> SaveSlots::checkSlot(SlotId slot) const {
  if (slot.index < 1 || slot.index > slotCount(slot.kind)) {
    const std::string_view kind =
        slot.kind == SlotKind::Manual ? messages::ManualSlotKind : messages::AutosaveSlotKind;
    return core::fail(std::format(messages::NoSuchSlot, kind, slot.index));
  }
  return {};
}

core::Result<void> SaveSlots::save(SlotId slot, const SaveFile& file) {
  if (auto valid = checkSlot(slot); !valid) {
    return valid;
  }
  auto text = writeSaveFile(file);
  if (!text) {
    return std::unexpected{text.error()};
  }
  return storage_->write(slotFileName(slot), *text, slotBackupName(slot));
}

core::Result<SaveFile> SaveSlots::readFile(SlotId slot, const std::string& name) const {
  if (auto valid = checkSlot(slot); !valid) {
    return std::unexpected{valid.error()};
  }
  auto text = storage_->read(name);
  if (!text) {
    return std::unexpected{text.error()};
  }
  if (!text->has_value()) {
    return core::fail(std::format(messages::SlotEmpty, name));
  }
  auto file = parseSaveFile(**text);
  if (!file) {
    return core::fail(std::format(messages::InFile, name, file.error().message));
  }
  return file;
}

core::Result<SaveFile> SaveSlots::load(SlotId slot) const {
  return readFile(slot, slotFileName(slot));
}

core::Result<SaveFile> SaveSlots::loadBackup(SlotId slot) const {
  return readFile(slot, slotBackupName(slot));
}

SlotId SaveSlots::nextAutosaveSlot() const {
  SlotInfo best = readInfo({.kind = SlotKind::Autosave, .index = 1});
  for (std::int32_t index = 2; index <= autosaveSlots_; ++index) {
    SlotInfo info = readInfo({.kind = SlotKind::Autosave, .index = index});
    if (goesBefore(info, best)) {
      best = std::move(info);
    }
  }
  return best.slot;
}

core::Result<SlotId> SaveSlots::autosave(const SaveFile& file) {
  if (autosaveSlots_ == 0) {
    return core::fail(std::string{messages::AutosaveOff});
  }
  const SlotId slot = nextAutosaveSlot();
  if (auto saved = save(slot, file); !saved) {
    return std::unexpected{saved.error()};
  }
  return slot;
}

} // namespace dwe::game::save
