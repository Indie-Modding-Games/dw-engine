#pragma once

#include <dwe/assets/map_table.hpp>
#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/inputs.hpp>
#include <dwe/game/evolution/result.hpp>
#include <dwe/game/evolution/state.hpp>
#include <dwe/game/evolution/tables.hpp>
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

class BattleAdapter;
class StoryAdapter;

// The save section of the evolution state.
inline constexpr std::string_view EvolutionSaveName = "evolution";

struct EvolutionAdapterDesc {
  // The mounted packs, for the tables and the map table; they must outlive the adapter.
  const assets::VirtualFileSystem* files = nullptr;
  // Tables already built (tests); otherwise they are read once from the ruleset's tables.
  std::optional<game::evolution::EvolutionTables> tables;
  // The story, which owns the raised forms as triggers 0x200 + number (setDigimonRaised); it must
  // outlive the adapter. Without it (tests of the adapter alone) the evolution state keeps them.
  StoryAdapter* story = nullptr;
};

// Glues the evolution system to the partner (docs/reference/evolution.md): fills its inputs from
// the partner, checks it on the clock's hours and on the partner's events, applies what it decides
// and saves its state.
class EvolutionAdapter final : public SessionSystem {
public:
  explicit EvolutionAdapter(EvolutionAdapterDesc desc);
  ~EvolutionAdapter() override;
  EvolutionAdapter(const EvolutionAdapter&) = delete;
  EvolutionAdapter& operator=(const EvolutionAdapter&) = delete;
  EvolutionAdapter(EvolutionAdapter&&) = delete;
  EvolutionAdapter& operator=(EvolutionAdapter&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  core::Result<void> startNewGame(GameSession& session) override;
  void tick(GameSession& session, const ruleset::ClockAdvance& advance) override;
  void routePartnerEvent(GameSession& session, const game::partner::PartnerEvent& event) override;
  void afterPraiseOrScold(GameSession& session, const PraiseOrScold& event) override;
  void afterWakeUp(GameSession& session) override;

  // Where the partner's moves are: the session's battle adapter, which owns them and must outlive
  // this adapter. The battle adapter is built after this one (it needs the inventory, which needs
  // this), so it comes in here. The requirements read the learned moves from it and an evolution
  // gives it the new form's moves. Without it the requirements see no learned moves.
  void setMoveSource(BattleAdapter& battle) { battle_ = &battle; }

  // An evolution item used on the partner (item.c: handleEvoItems); the inventory's command calls
  // it. Whether the partner evolved.
  bool useItem(GameSession& session, std::string_view item);
  // Back from the virus form (script routine 2). Whether the partner changed.
  bool returnFromVirus(GameSession& session);
  // Into the form an event script names (script routine 10); an unknown form is an error. Whether
  // it evolved.
  core::Result<bool> evolveByScript(GameSession& session, std::string_view species);

  // The level of a form of the tables; Level::None for an unknown one.
  [[nodiscard]] game::evolution::Level levelOf(std::string_view species) const;
  // The level of the form an evolution item turns into, if it is one.
  [[nodiscard]] std::optional<game::evolution::Level> itemTargetLevel(std::string_view item) const;

  [[nodiscard]] const game::evolution::EvolutionState& state() const { return state_; }
  // The ruleset's evolution scene, or nullptr when it has none (or no evolution rules are read).
  [[nodiscard]] const game::evolution::EvolutionSceneRules* sceneRules() const {
    return evolution_ && evolution_->rules().scene ? &*evolution_->rules().scene : nullptr;
  }
  // The evolution rules, or nullptr before they are read.
  [[nodiscard]] const game::evolution::Evolution* evolution() const {
    return evolution_ ? &*evolution_ : nullptr;
  }
  // How the partner stands toward the targets of its chart now, with the inputs and raised forms
  // the next check would read (the evolution chart's requirements); nothing without rules or when
  // its form is not in the tables.
  [[nodiscard]] std::optional<game::evolution::EvolutionOutlook>
  outlook(const GameSession& session) const;
  // The evolutions applied since the last call, oldest first, for the UI.
  [[nodiscard]] std::vector<game::evolution::EvolutionResult> takeEvolutions();

private:
  class Serializer;

  // What the rules read of the partner now, or nothing when its form is not in the tables.
  [[nodiscard]] std::optional<game::evolution::EvolutionInputs> inputsOf(const GameSession& session,
                                                                         bool scriptRunning) const;
  [[nodiscard]] static std::string mapIdOf(const GameSession& session);
  // Warns of each map a special rule asks for that is no map of data/maps, when the packs have a
  // map table: that rule never holds (dwx validate reports it too).
  void warnUnknownMaps(const game::evolution::SpecialRules& specials) const;
  // Applies the result to the partner (evl.c: EVL_applyEvolution's effects) and records it.
  void apply(GameSession& session, const game::evolution::EvolutionResult& result);

  [[nodiscard]] std::int32_t masteredMoves() const;
  // The raised forms the rules read, from the story before each check (a script may have raised
  // one). Static so the checks keep knowing evolution_ is set.
  static void syncRaised(const StoryAdapter* story, game::evolution::EvolutionState& state);
  // Marks a form raised in the story and in the rules' view of it.
  void raise(game::evolution::Species species);

  EvolutionAdapterDesc desc_;
  BattleAdapter* battle_ = nullptr;
  std::optional<game::evolution::Evolution> evolution_;
  std::optional<assets::MapTable> maps_;
  game::evolution::EvolutionState state_;
  std::vector<game::evolution::EvolutionResult> evolutions_;
  std::unique_ptr<Serializer> serializer_;
};

} // namespace dwe::session
