#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/game/save/storage.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::save {

// The names of slot files: `slot_01.yaml`, and their backups `slot_01.yaml.bak`. The autosave
// writes the playthrough's own slot (GameSession); `autosave_01.yaml` are old autosaves, from when
// the autosave had slots of its own: listed and loaded while they are on disk, never written.
inline constexpr std::string_view ManualSlotPrefix = "slot";
inline constexpr std::string_view AutosaveSlotPrefix = "autosave";
inline constexpr std::string_view SlotFileExtension = ".yaml";
inline constexpr std::string_view BackupSuffix = ".bak";

// Autosave: an old autosave file, read-only.
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
  // Newer: written by a newer game, so it can be neither loaded nor written over.
  enum class State : std::uint8_t { Empty, Saved, Damaged, Newer };

  SlotId slot;
  State state = State::Empty;
  // The header of a saved slot.
  std::optional<SaveHeader> header;
  // Why a damaged or newer slot cannot be read.
  std::string problem;
  // Whether the slot has a backup of its previous save, which loadBackup reads.
  bool hasBackup = false;
};

// Reads the summary of a save that has none (one written before format 3) from its sections.
// game/save does not know the sections, so the game gives it (session::summaryFromSections).
using SummaryReader = std::function<SaveSummary(const SaveFile&)>;

// The save slots of the game in a storage: the manual ones, and the old autosaves still on disk.
class SaveSlots {
public:
  // The storage must outlive this object.
  SaveSlots(SaveStorage& storage, const SaveSettings& settings);

  // Every slot: the manual ones, then the old autosaves on disk (autosave_01.yaml to
  // autosave_99.yaml, whichever exist).
  [[nodiscard]] std::vector<SlotId> slots() const;

  // Every slot with its state; a slot that cannot be read is Damaged with the reason, not an error.
  [[nodiscard]] std::vector<SlotInfo> list() const;

  // Fills, in list(), the summary of the saves that have none. Without one they list with an empty
  // summary.
  void setSummaryReader(SummaryReader reader);

  // Writes a save to a manual slot, keeping the slot's previous save as its backup; a damaged one
  // is not kept over a backup it already has. An old autosave is never written.
  core::Result<void> save(SlotId slot, const SaveFile& file);

  // Erases a slot's save: the slot reads as empty after it. The save is kept as the slot's backup
  // (replacing the one it had), until the next save to the slot replaces that backup in turn.
  core::Result<void> erase(SlotId slot);

  // Reads a slot's save, its header already migrated; SaveSections::read loads the systems from it.
  // Fails on an empty slot.
  [[nodiscard]] core::Result<SaveFile> load(SlotId slot) const;

  // Reads a slot's backup, for when its save is damaged.
  [[nodiscard]] core::Result<SaveFile> loadBackup(SlotId slot) const;

  // The first empty manual slot, where a playthrough with no slot of its own autosaves; nothing
  // when every slot holds a save (another game's, or one that cannot be read).
  [[nodiscard]] std::optional<SlotId> firstEmptySlot() const;

  // One slot with its state, as list() reads it.
  [[nodiscard]] SlotInfo info(SlotId slot) const;

private:
  [[nodiscard]] core::Result<void> checkSlot(SlotId slot) const;
  [[nodiscard]] core::Result<SaveFile> readFile(SlotId slot, const std::string& name) const;

  SaveStorage* storage_;
  std::int32_t manualSlots_;
  SummaryReader summaryReader_;
};

} // namespace dwe::game::save
