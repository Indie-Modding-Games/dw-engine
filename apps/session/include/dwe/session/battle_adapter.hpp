#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/rules.hpp>
#include <dwe/game/inventory/stacking.hpp>
#include <dwe/game/inventory/use.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/session/partner_moves.hpp>
#include <dwe/session/session_system.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::session {

class InventoryAdapter;

// The save section of the partner's moves.
inline constexpr std::string_view MovesSaveName = "moves";

struct BattleAdapterDesc {
  // The mounted packs, for data/digimon and data/moves; they must outlive the adapter.
  const assets::VirtualFileSystem* files = nullptr;
  // Tables already built (tests); otherwise they are read once from the ruleset's tables.
  std::optional<assets::DigimonTable> digimon;
  std::optional<assets::MoveTable> moves;
  // Where the bits go, and what a defeat takes; without it they are lost. It must
  // outlive the adapter.
  InventoryAdapter* inventory = nullptr;
};

// A battle the game or a script starts: everything but the partner, which the adapter builds from
// the session.
struct BattleRequest {
  std::vector<game::battle::Combatant> enemies;
  bool scripted = false;
  bool tamerless = false;
  std::string map;
  // Where the tamer and the partner stand on the field when it starts, in world axes, as the
  // enemies' positions.
  game::battle::Point tamer;
  game::battle::Point partner;
  game::battle::BattleKind kind = game::battle::BattleKind::Normal;
  // A tournament match's clock in seconds (game::battle::BattleSetup::timeLimit).
  std::optional<std::int32_t> timeLimit;
  // The resident that joins the city if the partner wins; the session decides it (the story's
  // recruit triggers).
  std::optional<std::string> recruitOnVictory;
};

// What a finished battle left, besides the partner's state: for the result screen and the recycle
// shop.
struct BattleAftermath {
  // The stat raises as the partner took them (each stops at the care limits).
  game::partner::StatBlock statGains;
  // The stats before those raises: the end box counts up from them (battle_ui.c:
  // renderPostBattleStatsBox).
  game::partner::StatBlock statsBefore;
  // The moves learned that it did not know.
  std::vector<std::string> learnedMoves;
  // The items the beaten enemies left: item.c: spawnDroppedItems lays them on the ground where each
  // enemy stood once the end box closes, for the player to pick up.
  std::vector<game::battle::Drop> drops;
  // The stacks a defeat took (script_common.c: handleItemLoss).
  std::vector<game::inventory::Stack> itemsLost;
  // The resident that joined the city.
  std::optional<std::string> recruitJoined;
};

// Glues the battle system to the partner (docs/reference/battle.md): builds the partner's fighter
// and condition from the session, starts the battle with the session's random numbers, and gives
// the result back: stats, care, time, lives, bits, drops and learned moves. It owns the partner's
// moves and saves them.
class BattleAdapter final : public SessionSystem {
public:
  explicit BattleAdapter(BattleAdapterDesc desc);
  ~BattleAdapter() override;
  BattleAdapter(const BattleAdapter&) = delete;
  BattleAdapter& operator=(const BattleAdapter&) = delete;
  BattleAdapter(BattleAdapter&&) = delete;
  BattleAdapter& operator=(BattleAdapter&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // Reads the battle and partner_moves sections and builds the move catalog. A battle in progress
  // keeps pointing at the rules it started with, so the rules must not change during one.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  // The first partner's moves (partner_impl.c: initializePartner).
  core::Result<void> startNewGame(GameSession& session) override;
  // A reborn partner equips one move again (evolution.c: reincarnatePartner).
  void routePartnerEvent(GameSession& session, const game::partner::PartnerEvent& event) override;

  // The partner as a fighter: its species' row, stats, HP, MP and equipped moves.
  [[nodiscard]] core::Result<game::battle::Combatant>
  partnerCombatant(const GameSession& session) const;
  // Starts a battle with the partner, at the clock's minute, seeded from the session's random
  // numbers.
  core::Result<game::battle::Battle> start(GameSession& session, BattleRequest request);
  // An oracle run (--original-rolls): the battles from here on roll with the original's generator
  // from this state instead (game::battle::Battle::useOriginalRolls).
  void useOriginalRolls(std::optional<std::uint32_t> state) { originalRolls_ = state; }
  // Gives a finished battle's result to the partner, the bag and the moves, and asks for the
  // autosaves of the battle's end, a recruit and a tournament.
  core::Result<BattleAftermath> finish(GameSession& session,
                                       const game::battle::BattleResult& result);

  // Throws one of an item of the bag at the partner (inventory.c: selectOptionMenu, main.c:
  // startThrowingItem): only while the battle's bag is available; the item is consumed at once and
  // its effect lands with the throw. The outcome says when the item was not thrown (not in the bag,
  // not usable in battle).
  core::Result<game::inventory::UseResult> throwItem(game::battle::Battle& battle,
                                                     assets::ItemHandle item) const;

  // Equips the species' move at `slot` in one of the four places, or empties it.
  core::Result<void> equip(const GameSession& session, std::size_t place,
                           std::optional<MoveSlot> slot);

  TechniqueToggle toggleTechnique(const GameSession& session, MoveSlot slot);
  [[nodiscard]] bool canFinishTechniqueEdit(const GameSession& session) const;

  // Teaches the partner a move outside battle (the gym's brains course: trn_reward.c:
  // TRN_tryLearnMove), with the moves learned along with it (utils2.c: learnMove). The ids it did
  // not know.
  std::vector<std::string> learnMove(std::string_view move);

  [[nodiscard]] const PartnerMoves& moves() const { return moves_; }
  // The new form's moves after an evolution (evl.c: EVL_applyEvolution): it learns `learned` and
  // the moves learned with it (utils2.c: learnMove), and the four places become `equipped`.
  void
  evolveMoves(const std::optional<std::string>& learned,
              const std::array<std::optional<MoveSlot>, game::battle::EquippedSlots>& equipped);
  // A story order: the partner learns the move with this original number and the ones learned
  // with it (utils2.c: learnMove), and whether it knows one (story_orders_partner.cpp).
  core::Result<void> learnMoveNumber(std::int64_t number);
  [[nodiscard]] core::Result<bool> knowsMoveNumber(std::int64_t number) const;
  [[nodiscard]] const game::battle::BattleRules& rules() const;
  // The tables the battles read, once the rules are read; null before.
  [[nodiscard]] const assets::DigimonTable* digimonTable() const {
    return desc_.digimon ? &*desc_.digimon : nullptr;
  }
  [[nodiscard]] const assets::MoveTable* moveTable() const {
    return desc_.moves ? &*desc_.moves : nullptr;
  }
  [[nodiscard]] const game::battle::MoveCatalog& catalog() const;

private:
  class Serializer;

  core::Result<void> loadTables(const ruleset::Ruleset& rules);
  [[nodiscard]] const assets::DigimonRow* rowOf(std::string_view species) const;
  [[nodiscard]] std::optional<MoveSlot> finisherSlot(const assets::DigimonRow& row) const;
  void giveRewards(GameSession& session, const game::battle::BattleResult& result,
                   BattleAftermath& aftermath) const;

  BattleAdapterDesc desc_;
  std::optional<ruleset::Section> section_;
  std::unique_ptr<game::battle::BattleRules> rules_;
  std::unique_ptr<game::battle::MoveCatalog> catalog_;
  PartnerMoveRules moveRules_;
  PartnerMoves moves_;
  std::unique_ptr<Serializer> serializer_;
  std::optional<std::uint32_t> originalRolls_;
};

} // namespace dwe::session
