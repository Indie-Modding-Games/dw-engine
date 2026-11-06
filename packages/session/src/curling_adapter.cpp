#include <dwe/core/log.hpp>
#include <dwe/session/curling_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>

#include <utility>

namespace dwe::session {

core::Result<void> CurlingAdapter::readRules(const ruleset::Ruleset& rules) {
  rules_.reset();
  match_.reset();
  const ruleset::Section* section = rules.section(game::curling::CurlingSectionName);
  if (section == nullptr) {
    return {};
  }
  auto read = game::curling::parseCurlingRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> CurlingAdapter::begin(GameSession& session) {
  match_.reset();
  if (!rules_ || desc_.story == nullptr) {
    return core::fail("there are no stones game rules");
  }
  auto picked = desc_.story->state().variable(rules_->opponentVariable);
  if (!picked) {
    return std::unexpected{picked.error()};
  }
  // KAR_start: the opponent's entity is the variable plus 2: 0 the first opponent, 1 the second.
  const game::curling::Opponent opponent =
      *picked == 0 ? game::curling::Opponent::Penguinmon : game::curling::Opponent::MetalMamemon;
  match_.emplace(*rules_, opponent, session.random());
  core::logInfo("[port:curling] KAR_start (kar.c:778): a match against opponent {}", *picked);
  return {};
}

std::span<const game::curling::CurlingEvent>
CurlingAdapter::play(GameSession& session, const game::curling::CurlingButtons& buttons) {
  if (!match_) {
    return {};
  }
  return match_->tick(buttons, session.random());
}

core::Result<std::int32_t> CurlingAdapter::finish() {
  if (!rules_ || !match_ || desc_.story == nullptr) {
    return core::fail("no stones match runs");
  }
  const std::int32_t result = match_->result();
  match_.reset();
  if (auto written = desc_.story->state().setVariable(rules_->resultVariable,
                                                      static_cast<std::uint8_t>(result));
      !written) {
    return std::unexpected{written.error()};
  }
  core::logInfo("[port:curling] KAR_finishMatch (kar.c:945): result {}", result);
  return result;
}

} // namespace dwe::session
