#include <dwe/session/game_session.hpp>
#include <dwe/session/session_system.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"

using dwe::session::GameSession;
using dwe::session::PraiseOrScold;
using dwe::session::SessionSystem;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// Keeps the praises and scolds it is told about.
class PraiseWatcher final : public SessionSystem {
public:
  explicit PraiseWatcher(std::vector<PraiseOrScold>& seen) : seen_(&seen) {}
  void afterPraiseOrScold(GameSession& /*session*/, const PraiseOrScold& event) override {
    seen_->push_back(event);
  }

private:
  std::vector<PraiseOrScold>* seen_;
};

} // namespace

TEST_CASE(
    "the systems hear of a praise or a scold with the discipline and happiness from before it") {
  const TestPacks packs;
  std::vector<PraiseOrScold> seen;
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::make_unique<PraiseWatcher>(seen));
  auto session = newGameWith(packs, std::move(systems));
  const auto& mood = session->partner().state().mood;
  const PraiseOrScold beforeScold{
      .scolded = true, .disciplineBefore = mood.discipline, .happinessBefore = mood.happiness};
  session->scold();
  const PraiseOrScold beforePraise{
      .scolded = false, .disciplineBefore = mood.discipline, .happinessBefore = mood.happiness};
  session->praise();
  REQUIRE(seen.size() == 2);
  CHECK(seen[0].scolded == beforeScold.scolded);
  CHECK(seen[0].disciplineBefore == beforeScold.disciplineBefore);
  CHECK(seen[0].happinessBefore == beforeScold.happinessBefore);
  CHECK(seen[1].scolded == beforePraise.scolded);
  CHECK(seen[1].disciplineBefore == beforePraise.disciplineBefore);
  CHECK(seen[1].happinessBefore == beforePraise.happinessBefore);
}

TEST_CASE("a tamer level chance of 100 always gives the level, within the tamer section's limits") {
  const TestPacks packs;
  auto session = newGameWith(packs, {});
  const auto start = session->tamer().level;
  session->addTamerLevel(100, 1);
  CHECK(session->tamer().level == start + 1);
  session->addTamerLevel(0, 1);
  CHECK(session->tamer().level == start + 1);
}
