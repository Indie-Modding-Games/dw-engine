#pragma once

#include <dwe/game/curling/curling.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace dwe::session {

class StoryAdapter;

struct CurlingAdapterDesc {
  // The story (the opponent it picked, the result it reads); it must outlive the adapter.
  StoryAdapter* story = nullptr;
};

// The stones game the story starts (call_partner_routine 1: kar.c: KAR_start): the match against
// the opponent in the story's opponent variable, and its result in the result variable when it is
// over (KAR_finishMatch). The app draws it and gives it the buttons.
class CurlingAdapter final : public SessionSystem {
public:
  explicit CurlingAdapter(CurlingAdapterDesc desc) : desc_{desc} {}

  core::Result<void> readRules(const ruleset::Ruleset& rules) override;

  // Whether the ruleset has the stones game.
  [[nodiscard]] bool enabled() const { return rules_.has_value(); }
  // KAR_start: a match from the start. Fails when there are no rules or no story.
  core::Result<void> begin(GameSession& session);
  // The power gauge's top (KAR_tickMatchState), 0 without rules.
  [[nodiscard]] std::int32_t powerLimit() const { return rules_ ? rules_->powerLimit : 0; }
  [[nodiscard]] const game::curling::Match* match() const { return match_ ? &*match_ : nullptr; }
  // One frame of the original (20 a second); nothing when no match runs.
  std::span<const game::curling::CurlingEvent> play(GameSession& session,
                                                    const game::curling::CurlingButtons& buttons);
  // KAR_finishMatch: writes the result where the story reads it and ends the match. The result.
  core::Result<std::int32_t> finish();

private:
  CurlingAdapterDesc desc_;
  std::optional<game::curling::CurlingRules> rules_;
  std::optional<game::curling::Match> match_;
};

} // namespace dwe::session
