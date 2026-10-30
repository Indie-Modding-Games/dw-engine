#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/medals.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::session {

class BattleAdapter;
class CardsAdapter;
class InventoryAdapter;
class StoryAdapter;

// The ruleset section of the medals.
inline constexpr std::string_view MedalsSection = "medals";

// The sound the award notice plays when its box opens (tamer.c: tamerTickAwardSomething,
// playSound(0, 7)).
struct AwardSound {
  std::int64_t slot = 0;
  std::int64_t sound = 0;
};

struct MedalsRules {
  game::story::MedalRules medals;
  AwardSound awardSound;
};

// Reads the medals section; errors name the file and line.
core::Result<MedalsRules> readMedalsRules(const ruleset::Section& section);

struct MedalsAdapterDesc {
  // They must outlive the adapter.
  StoryAdapter* story = nullptr;
  const BattleAdapter* battle = nullptr;
  const InventoryAdapter* inventory = nullptr;
  const CardsAdapter* cards = nullptr;
};

// The medals the field gives (tamer.c: checkMedalConditions) and the notice they leave due
// (MEDAL_AWARD_PENDING), which the game shows with the tamer level's (tamerTickAwardSomething).
// The medals are the story's triggers; the notice is not saved, as in the original.
class MedalsAdapter final : public SessionSystem {
public:
  explicit MedalsAdapter(MedalsAdapterDesc desc);

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // Reads the medals section; without one no medal is checked.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> startNewGame(GameSession& session) override;
  void afterLoad(GameSession& session) override;

  // One frame of the player walking freely: gives the medals whose conditions hold, adds the tamer
  // levels they bring, and leaves the notice due.
  void check(GameSession& session);

  // Whether a medal notice is due.
  [[nodiscard]] bool awardPending() const { return awardPending_; }
  void clearAward() { awardPending_ = false; }
  [[nodiscard]] const std::optional<MedalsRules>& rules() const { return rules_; }

private:
  MedalsAdapterDesc desc_;
  std::optional<MedalsRules> rules_;
  bool awardPending_ = false;
};

} // namespace dwe::session
