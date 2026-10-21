#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/rules.hpp>
#include <dwe/game/inventory/stacking.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/session/partner_moves.hpp>
#include <dwe/session/session_system.hpp>

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
    // Where the bits and the drops go, and what a defeat takes; without it they are lost. It must outlive the adapter.
    InventoryAdapter* inventory = nullptr;
};

// A battle the game or a script starts: everything but the partner, which the adapter builds from the session.
struct BattleRequest {
    std::vector<game::battle::Combatant> enemies;
    bool scripted = false;
    bool tamerless = false;
    std::string map;
    game::battle::Point tamer;
    game::battle::BattleKind kind = game::battle::BattleKind::Normal;
    // The resident that joins the city if the partner wins; the session decides it (the story's recruit triggers).
    std::optional<std::string> recruitOnVictory;
};

// What a finished battle left, besides the partner's state: for the result screen and the recycle shop.
struct BattleAftermath {
    // The stat raises as the partner took them (each stops at the care limits).
    game::partner::StatBlock statGains;
    // The moves learned that it did not know.
    std::vector<std::string> learnedMoves;
    // Drops the bag did not take: they stay on the ground.
    std::vector<std::string> dropsLeft;
    // The stacks a defeat took (script_common.c: handleItemLoss).
    std::vector<game::inventory::Stack> itemsLost;
    // The resident that joined the city.
    std::optional<std::string> recruitJoined;
};

// Glues the battle system to the partner (docs/reference/battle.md): builds the partner's fighter and condition from
// the session, starts the battle with the session's random numbers, and gives the result back: stats, care, time,
// lives, bits, drops and learned moves. It owns the partner's moves and saves them.
class BattleAdapter final : public SessionSystem {
public:
    explicit BattleAdapter(BattleAdapterDesc desc);
    ~BattleAdapter() override;
    BattleAdapter(const BattleAdapter&) = delete;
    BattleAdapter& operator=(const BattleAdapter&) = delete;
    BattleAdapter(BattleAdapter&&) = delete;
    BattleAdapter& operator=(BattleAdapter&&) = delete;

    [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
    // Reads the battle and partner_moves sections and builds the move catalog. A battle in progress keeps pointing
    // at the rules it started with, so the rules must not change during one.
    core::Result<void> readRules(const ruleset::Ruleset& rules) override;
    core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
    // The first partner's moves (partner_impl.c: initializePartner).
    core::Result<void> startNewGame(GameSession& session) override;
    // A reborn partner equips one move again (evolution.c: reincarnatePartner).
    void routePartnerEvent(GameSession& session, const game::partner::PartnerEvent& event) override;

    // The partner as a fighter: its species' row, stats, HP, MP and equipped moves.
    [[nodiscard]] core::Result<game::battle::Combatant> partnerCombatant(const GameSession& session) const;
    // Starts a battle with the partner, at the clock's minute, seeded from the session's random numbers.
    core::Result<game::battle::Battle> start(GameSession& session, BattleRequest request);
    // Gives a finished battle's result to the partner, the bag and the moves, and asks for the autosaves of the
    // battle's end, a recruit and a tournament.
    core::Result<BattleAftermath> finish(GameSession& session, const game::battle::BattleResult& result);

    // Equips the species' move at `slot` in one of the four places, or empties it.
    core::Result<void> equip(const GameSession& session, std::size_t place, std::optional<MoveSlot> slot);

    [[nodiscard]] const PartnerMoves& moves() const { return moves_; }
    // The move table indices of the learned moves (utils2.c: learnMove sets bit `index` of learnedMoves); a move
    // the table does not have is left out.
    [[nodiscard]] std::vector<std::int32_t> learnedMoveIndices() const;
    [[nodiscard]] const game::battle::BattleRules& rules() const;
    [[nodiscard]] const game::battle::MoveCatalog& catalog() const;

private:
    class Serializer;

    core::Result<void> loadTables(const ruleset::Ruleset& rules);
    [[nodiscard]] const assets::DigimonRow* rowOf(std::string_view species) const;
    [[nodiscard]] std::optional<MoveSlot> finisherSlot(const assets::DigimonRow& row) const;
    void giveRewards(GameSession& session, const game::battle::BattleResult& result, BattleAftermath& aftermath) const;

    BattleAdapterDesc desc_;
    std::optional<ruleset::Section> section_;
    std::unique_ptr<game::battle::BattleRules> rules_;
    std::unique_ptr<game::battle::MoveCatalog> catalog_;
    PartnerMoveRules moveRules_;
    PartnerMoves moves_;
    std::unique_ptr<Serializer> serializer_;
};

} // namespace dwe::session
