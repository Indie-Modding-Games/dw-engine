#include <dwe/game/save/slots.hpp>

#include <format>
#include <utility>

#include "messages.hpp"

namespace dwe::game::save {

std::string slotFileName(SlotId slot) {
  return std::format("{}_{:02}{}",
                     slot.kind == SlotKind::Manual ? ManualSlotPrefix : AutosaveSlotPrefix,
                     slot.index, SlotFileExtension);
}

std::string slotBackupName(SlotId slot) {
  return std::format("{}{}", slotFileName(slot), BackupSuffix);
}

SaveSlots::SaveSlots(SaveStorage& storage, const SaveSettings& settings)
    : storage_{&storage}, manualSlots_{settings.manualSlots} {}

std::vector<SlotId> SaveSlots::slots() const {
  std::vector<SlotId> all;
  for (std::int32_t index = 1; index <= manualSlots_; ++index) {
    all.push_back({.kind = SlotKind::Manual, .index = index});
  }
  // The storage lists no folder: the old autosaves are found by name, each a missing file at most.
  for (std::int32_t index = 1; index <= MaxSlots; ++index) {
    const SlotId old{.kind = SlotKind::Autosave, .index = index};
    const auto text = storage_->read(slotFileName(old));
    if (!text || text->has_value()) {
      all.push_back(old);
    }
  }
  return all;
}

SlotInfo SaveSlots::info(SlotId slot) const {
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
    const bool newer = text && text->has_value() && isNewerSave(**text);
    info.state = newer ? SlotInfo::State::Newer : SlotInfo::State::Damaged;
    info.problem = std::format(messages::InFile, slotFileName(slot), file.error().message);
    return info;
  }
  info.state = SlotInfo::State::Saved;
  if (file->header.summary == SaveSummary{} && summaryReader_) {
    file->header.summary = summaryReader_(*file);
  }
  info.header = std::move(file->header);
  return info;
}

std::vector<SlotInfo> SaveSlots::list() const {
  std::vector<SlotInfo> infos;
  for (const SlotId slot : slots()) {
    infos.push_back(info(slot));
  }
  return infos;
}

void SaveSlots::setSummaryReader(SummaryReader reader) {
  summaryReader_ = std::move(reader);
}

core::Result<void> SaveSlots::checkSlot(SlotId slot) const {
  const std::int32_t count = slot.kind == SlotKind::Manual ? manualSlots_ : MaxSlots;
  if (slot.index < 1 || slot.index > count) {
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
  if (slot.kind == SlotKind::Autosave) {
    return core::fail(std::format(messages::OldAutosaveReadOnly, slotFileName(slot)));
  }
  auto text = writeSaveFile(file);
  if (!text) {
    return std::unexpected{text.error()};
  }
  // A damaged save never replaces a good backup: the backup may be all that is left of the slot.
  const SlotInfo current = info(slot);
  const bool keepBackup = current.state == SlotInfo::State::Damaged && current.hasBackup;
  return storage_->write(slotFileName(slot), *text, keepBackup ? "" : slotBackupName(slot));
}

core::Result<void> SaveSlots::erase(SlotId slot) {
  if (auto valid = checkSlot(slot); !valid) {
    return valid;
  }
  // A slot's erase leaves nothing behind: its save and its .bak both go, so no backup of a save the
  // player deleted is kept.
  const auto eraseFiles = [this](SlotId target) -> core::Result<void> {
    if (auto removed = storage_->remove(slotFileName(target), ""); !removed) {
      return removed;
    }
    return storage_->remove(slotBackupName(target), "");
  };
  if (auto removed = eraseFiles(slot); !removed) {
    return removed;
  }
  // An old autosave names the manual slot of its playthrough in its summary; erasing that
  // playthrough erases its old autosaves too, so no autosave of a deleted save is left behind.
  if (slot.kind == SlotKind::Manual) {
    std::string erased = slotFileName(slot);
    erased.erase(erased.size() - SlotFileExtension.size());
    for (const SlotId old : slots()) {
      if (old.kind != SlotKind::Autosave) {
        continue;
      }
      const SlotInfo oldInfo = info(old);
      if (oldInfo.header && oldInfo.header->summary.slot == erased) {
        if (auto removed = eraseFiles(old); !removed) {
          return removed;
        }
      }
    }
  }
  return {};
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

std::optional<SlotId> SaveSlots::firstEmptySlot() const {
  for (std::int32_t index = 1; index <= manualSlots_; ++index) {
    const SlotId slot{.kind = SlotKind::Manual, .index = index};
    if (info(slot).state == SlotInfo::State::Empty) {
      return slot;
    }
  }
  return std::nullopt;
}

} // namespace dwe::game::save
