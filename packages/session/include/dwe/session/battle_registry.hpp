#pragma once

#include <dwe/core/error.hpp>
#include <dwe/session/session_system.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dwe::game::save {
struct SaveFile;
} // namespace dwe::game::save

namespace dwe::session {

class BattleAdapter;
class GameSession;

// A partner registered for versus battles at the save machine (main_menu.c: registerBattleData,
// the registry's record): its name, species, stats, discipline and three techniques. The
// fourth equipped move, the finisher, is not kept, as in the original.
struct RegisteredPartner {
  static constexpr std::size_t Techniques = 3;

  std::string name;
  // The species: its id in the species table.
  std::string species;
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brains = 0;
  std::int32_t discipline = 0;
  // Move ids from the moves table; an empty place has none.
  std::array<std::optional<std::string>, Techniques> techniques{};

  friend bool operator==(const RegisteredPartner&, const RegisteredPartner&) = default;
};

// The partners registered for versus battles, saved with the game (the save file's
// battleRegistrationData). The versus mode that fights them comes with phase 5; the save machine
// fills them.
class BattleRegistry final : public SessionSystem {
public:
  // The original's 40 records.
  static constexpr std::size_t Slots = 40;
  // The machine's list reaches the first 39: ten rows that scroll at most 29 (MENU_HIGHLIGHTS[15]);
  // the last is the ending's gift (main_menu.c: awardMachinedramonData).
  static constexpr std::size_t MachineSlots = 39;

  BattleRegistry();
  ~BattleRegistry() override;
  BattleRegistry(const BattleRegistry&) = delete;
  BattleRegistry& operator=(const BattleRegistry&) = delete;
  BattleRegistry(BattleRegistry&&) = delete;
  BattleRegistry& operator=(BattleRegistry&&) = delete;

  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  // A new game has none registered.
  core::Result<void> startNewGame(GameSession& session) override;

  // The partner registered at `slot` (from 0), if any; none past the last slot.
  [[nodiscard]] const std::optional<RegisteredPartner>& at(std::size_t slot) const;
  // Registers a partner at `slot` (from 0), over whatever was there. Fails past the last slot.
  core::Result<void> registerAt(std::size_t slot, RegisteredPartner partner);
  // Empties `slot`; nothing past the last slot.
  void clear(std::size_t slot);
  // How many slots hold a partner (main_menu.c: countRegisteredDigimon): the versus mode refuses a
  // registry with none.
  [[nodiscard]] std::size_t registeredCount() const;

private:
  class Serializer;

  std::array<std::optional<RegisteredPartner>, Slots> slots_{};
  std::unique_ptr<Serializer> serializer_;
};

// The session's partner as the save machine registers it (main_menu.c: registerBattleData): the
// name the player gave it, its maximum stats, its discipline and its first three equipped moves.
[[nodiscard]] core::Result<RegisteredPartner> registeredPartnerOf(const GameSession& session,
                                                                  const BattleAdapter& battle);

// The partners a save file registered, in slot order, without starting its game: what a versus
// player picks its lineup from (main_menu.c: drawRegisteredDigimonSlots lists a card's records).
[[nodiscard]] core::Result<std::vector<RegisteredPartner>>
registeredPartnersIn(const game::save::SaveFile& file);

} // namespace dwe::session
