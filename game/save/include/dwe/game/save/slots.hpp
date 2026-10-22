#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/game/save/storage.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::save {

// The names of slot files: `slot_01.yaml`, `autosave_01.yaml`, and their backups
// `slot_01.yaml.bak`.
inline constexpr std::string_view ManualSlotPrefix = "slot";
inline constexpr std::string_view AutosaveSlotPrefix = "autosave";
inline constexpr std::string_view SlotFileExtension = ".yaml";
inline constexpr std::string_view BackupSuffix = ".bak";

enum class SlotKind : std::uint8_t { Manual, Autosave };

// A slot, counted from 1 within its kind.
struct SlotId {
  SlotKind kind = SlotKind::Manual;
  std::int32_t index = 1;

  friend bool operator==(const SlotId&, const SlotId&) = default;
};

// The file of a slot.
std::string slotFileName(SlotId slot);

// The backup of a slot's file: the file as it was before the last save.
std::string slotBackupName(SlotId slot);

// What the slot list shows of one slot.
struct SlotInfo {
  enum class State : std::uint8_t { Empty, Saved, Damaged };

  SlotId slot;
  State state = State::Empty;
  // The header of a saved slot.
  std::optional<SaveHeader> header;
  // Why a damaged slot cannot be read.
  std::string problem;
  // Whether the slot has a backup of its previous save, which loadBackup reads.
  bool hasBackup = false;
};

// The save slots of the game in a storage: the manual ones and the autosave ones, written in turn.
class SaveSlots {
public:
  // The storage must outlive this object.
  SaveSlots(SaveStorage& storage, const SaveSettings& settings);

  // Every slot: the manual ones, then the autosave ones.
  [[nodiscard]] std::vector<SlotId> slots() const;

  // Every slot with its state; a slot that cannot be read is Damaged with the reason, not an error.
  [[nodiscard]] std::vector<SlotInfo> list() const;

  // Writes a save to a slot, keeping the slot's previous save as its backup.
  core::Result<void> save(SlotId slot, const SaveFile& file);

  // Reads a slot's save, its header already migrated; SaveSections::read loads the systems from it.
  // Fails on an empty slot.
  [[nodiscard]] core::Result<SaveFile> load(SlotId slot) const;

  // Reads a slot's backup, for when its save is damaged.
  [[nodiscard]] core::Result<SaveFile> loadBackup(SlotId slot) const;

  // Writes a save to the next autosave slot: the first empty one, else a damaged one, else the one
  // saved longest ago. Returns the slot written.
  core::Result<SlotId> autosave(const SaveFile& file);

private:
  [[nodiscard]] std::int32_t slotCount(SlotKind kind) const;
  [[nodiscard]] core::Result<void> checkSlot(SlotId slot) const;
  [[nodiscard]] SlotInfo readInfo(SlotId slot) const;
  [[nodiscard]] core::Result<SaveFile> readFile(SlotId slot, const std::string& name) const;
  [[nodiscard]] SlotId nextAutosaveSlot() const;

  SaveStorage* storage_;
  std::int32_t manualSlots_;
  std::int32_t autosaveSlots_;
};

} // namespace dwe::game::save
