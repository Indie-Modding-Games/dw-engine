#pragma once

#include <dwe/core/error.hpp>
#include <dwe/session/map_head_lines.hpp>
#include <dwe/session/session_system.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::game::story {
class StoryState;
} // namespace dwe::game::story

namespace dwe::session {

class InventoryAdapter;
class StoryAdapter;

// The ruleset section of the bird transport (rules/<id>/bird_transport.yaml).
inline constexpr std::string_view BirdTransportSection = "bird_transport";

// What the carrier says, as line numbers of its MAPHEAD section (shop/script_menu.c:
// tickBirdraTransport, tickBirdraTransportMenu).
struct BirdTransportLines {
  std::int32_t greeting = 0;
  std::int32_t farewell = 0;
  std::int32_t confirm = 0;
  std::int32_t choices = 0;
  // The money box's line (tickShopBitsBox: line 6 of the keeper's section).
  std::int32_t bits = 0;
};

// A place the carrier flies to (BIRDRA_TRANSPORT_TARGETS): the original's map index and entry, the
// trigger that puts it on the list and its price in bits.
struct BirdDestination {
  std::int32_t map = 0;
  std::int32_t entry = 0;
  std::int32_t trigger = 0;
  std::int32_t cost = 0;
};

// A sound of the original's: sound `sound` of slot `slot` (sound.c: playSound).
struct BirdSound {
  std::int64_t slot = 0;
  std::int64_t sound = 0;
};

struct BirdTransportRules {
  // showMapHeadTextbox(..., 0x4d6): the MAPHEAD section of the carrier's lines.
  std::int32_t linesSection = 0;
  // createShopBitsBox -> getShopkeeperLine: the section of the money box's line. The carrier is no
  // shopkeeper of the keepers table, so it is the first keeper's (0x4c4).
  std::int32_t bitsSection = 0;
  BirdTransportLines lines;
  // tickBirdraTransport state 5: the destination's map and entry go to these variables, and the
  // script goes on at this MAPHEAD section ("Hold on tight!", then teleport from the map variable).
  std::int32_t mapVariable = 0;
  std::int32_t entryVariable = 0;
  std::int32_t travelSection = 0;
  // tickSelectionDialogue: while this trigger is set, cancel does not answer the yes-or-no.
  std::int32_t choiceLockTrigger = 0;
  // tickBirdraTransportMenu: a place the player cannot pay for.
  BirdSound refuseSound;
  std::vector<BirdDestination> destinations;
};

core::Result<BirdTransportRules> readBirdTransportRules(const ruleset::Section& section);

// A row of the menu (birdraFillTargetList, calculateBirdramonMenuStrings): the destination's place
// in the rules and whether the money pays for it (the 0x80 flag; red otherwise).
struct BirdRow {
  std::size_t destination = 0;
  bool affordable = false;
};

// birdraFillTargetList: the destinations whose trigger is set, in the rules' order.
[[nodiscard]] std::vector<BirdRow>
birdRows(const BirdTransportRules& rules, const game::story::StoryState& story, std::int32_t money);

struct BirdTransportDesc {
  // The mounted packs, for script/maphead; they must outlive the transport.
  const assets::VirtualFileSystem* files = nullptr;
  // The money and the story's triggers and variables; they must outlive the transport.
  InventoryAdapter* inventory = nullptr;
  StoryAdapter* story = nullptr;
};

// The bird transport the story opens (script_interp.c: CALL_ROUTINE 10 -> shop/script_menu.c:
// tickBirdraTransport): the carrier lists the places whose trigger is set with their price; the
// player picks one he can pay for and says yes, the money goes and the script goes on at the
// MAPHEAD section that flies him there. The rows are taken when the visit opens, as the original
// fills the list once.
class BirdTransport final : public SessionSystem {
public:
  explicit BirdTransport(BirdTransportDesc desc);
  ~BirdTransport() override;
  BirdTransport(const BirdTransport&) = delete;
  BirdTransport& operator=(const BirdTransport&) = delete;
  BirdTransport(BirdTransport&&) = delete;
  BirdTransport& operator=(BirdTransport&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // A ruleset without the section has no transport: the routine then closes at once.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> startNewGame(GameSession& session) override;

  // tickBirdraTransport state 0: the list. False (closed) without rules, money or story.
  bool open();
  void close();
  [[nodiscard]] bool isOpen() const { return open_; }
  [[nodiscard]] bool available() const { return rules_.has_value(); }
  [[nodiscard]] const BirdTransportRules& rules() const;

  [[nodiscard]] const std::vector<BirdRow>& rows() const { return rows_; }
  [[nodiscard]] std::int32_t money() const;
  // tickBirdraTransportMenu's confirm: a row the money pays for is picked and the carrier asks;
  // false for one it does not (the refusal sound).
  bool pick(std::size_t row);
  [[nodiscard]] std::optional<std::size_t> picked() const { return picked_; }
  // State 5, the yes: the map and entry variables, the money; the MAPHEAD section the script goes
  // on at.
  [[nodiscard]] std::int32_t travel();
  // Whether cancel answers no to the yes-or-no (the lock trigger is clear).
  [[nodiscard]] bool choiceCancellable() const;

  // The text key of a line of the carrier (resolveMapHeadEntry); empty when MAPHEAD's listing
  // does not have it.
  [[nodiscard]] std::string lineKey(std::int32_t line) const;
  // The money box's line ("Bits {bits}").
  [[nodiscard]] std::string bitsKey() const;

private:
  [[nodiscard]] std::string sectionLine(std::int32_t section, std::int32_t line) const;

  BirdTransportDesc desc_;
  std::optional<BirdTransportRules> rules_;
  bool open_ = false;
  std::vector<BirdRow> rows_;
  std::optional<std::size_t> picked_;
  std::optional<MapHeadLineTables> lineTables_;
};

} // namespace dwe::session
