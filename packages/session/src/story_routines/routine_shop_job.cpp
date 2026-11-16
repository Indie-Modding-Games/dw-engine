#include <dwe/core/log.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/shop_job_adapter.hpp>

#include <cstdint>
#include <optional>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

namespace shop_job = game::shop_job;

// The engine's routines of the improved shop job: numbers past the original's routines, which
// the story patches story/patches/shop_job_*.lua call. Each writes its answer to the rules'
// result variable.
constexpr game::story::RoutineNumber ModeRoutine = 256;
constexpr game::story::RoutineNumber NextCustomerRoutine = 257;
constexpr game::story::RoutineNumber ArrivalRoutine = 258;
constexpr game::story::RoutineNumber CounterRoutine = 259;
constexpr game::story::RoutineNumber BonusRoutine = 260;

// The bonus routine's answers.
constexpr std::int64_t NoBonus = 0;
constexpr std::int64_t BonusGiven = 1;
constexpr std::int64_t BonusKept = 2;
constexpr std::int64_t BonusNowhere = 3;

using Handler = core::Result<game::story::RoutineStart> (*)(StoryScenesContext&);

// The improved job's adapter while the improved mode is on; nullptr plays the original's.
ShopJobAdapter* improvedJob(StoryScenesContext& scenes) {
  ShopJobAdapter* job = scenes.shopJob;
  return job != nullptr && job->improved() ? job : nullptr;
}

core::Result<game::story::RoutineStart> answer(StoryScenesContext& scenes, std::int64_t value) {
  const ShopJobAdapter* job = scenes.shopJob;
  if (job == nullptr || job->rules() == nullptr) {
    return game::story::RoutineGoesOn{};
  }
  if (auto written = scenes.state().setVariable(job->rules()->resultVariable, value); !written) {
    return std::unexpected{written.error()};
  }
  return game::story::RoutineGoesOn{};
}

// The customers served so far (gcan11's variable 123), 0 without the story's job.
std::int64_t customersServed(const StoryScenesContext& scenes) {
  const auto& job = scenes.rules.shopJob;
  return job ? scenes.variable(job->customersVariable) : 0;
}

// 256: 1 when the story plays the improved job, 0 for the original's scripts.
core::Result<game::story::RoutineStart> mode(StoryScenesContext& scenes) {
  return answer(scenes, improvedJob(scenes) != nullptr ? 1 : 0);
}

// 257: the script of the next customer of the line (script 49's patch jumps to its section 0x300).
// Before the job's first customer it makes the line, once: asking again before that customer comes
// in gives the same line.
core::Result<game::story::RoutineStart> nextCustomer(StoryScenesContext& scenes) {
  ShopJobAdapter* job = improvedJob(scenes);
  if (job == nullptr || scenes.session == nullptr) {
    return answer(scenes, 0);
  }
  const std::int64_t served = customersServed(scenes);
  if (job->lineup() == nullptr || (served == 0 && job->lineBegun())) {
    job->startJob(scenes.session->seed());
  }
  const shop_job::Customer* customer = job->customerAt(static_cast<std::size_t>(served));
  if (customer == nullptr) {
    return answer(scenes, 0);
  }
  return answer(scenes, job->rules()->species.at(customer->species).script);
}

// 258: the customer who just came in (the story counted it) goes to the counter; its item.
core::Result<game::story::RoutineStart> arrival(StoryScenesContext& scenes) {
  ShopJobAdapter* job = improvedJob(scenes);
  const std::int64_t served = customersServed(scenes);
  if (job == nullptr || served <= 0 || !job->beginCustomer(static_cast<std::size_t>(served - 1))) {
    return answer(scenes, 0);
  }
  return answer(scenes, static_cast<std::int64_t>(job->haggle()->customer().item));
}

// 259: one turn at the counter: the app's haggle screen, waited for; the adapter writes the turn.
core::Result<game::story::RoutineStart> counter(StoryScenesContext& scenes) {
  ShopJobAdapter* job = improvedJob(scenes);
  if (job == nullptr || job->haggle() == nullptr) {
    return answer(scenes, static_cast<std::int64_t>(ShopJobTurn::Dismissed));
  }
  return scenes.openScreen({.screen = StoryScreen::ShopHaggle,
                            .routine = CounterRoutine,
                            .first = static_cast<std::int64_t>(job->customerIndex()),
                            .second = 0,
                            .waits = true});
}

// 260: the job's end: the bonus item over the original's pay.
core::Result<game::story::RoutineStart> bonus(StoryScenesContext& scenes) {
  ShopJobAdapter* job = improvedJob(scenes);
  if (job == nullptr) {
    return answer(scenes, NoBonus);
  }
  auto paid = job->payBonus();
  if (!paid) {
    core::logWarning("{}", paid.error().message);
    return answer(scenes, NoBonus);
  }
  if (!paid->earned) {
    return answer(scenes, NoBonus);
  }
  switch (paid->place) {
  case ShopJobAdapter::BonusPlace::Bag:
    return answer(scenes, BonusGiven);
  case ShopJobAdapter::BonusPlace::Keeper:
    return answer(scenes, BonusKept);
  case ShopJobAdapter::BonusPlace::Nowhere:
    break;
  }
  return answer(scenes, BonusNowhere);
}

} // namespace

core::Result<void> registerShopJobRoutines(StoryScenesContext& scenes) {
  for (const auto& [number, handler] :
       {std::pair{ModeRoutine, Handler{&mode}},
        std::pair{NextCustomerRoutine, Handler{&nextCustomer}},
        std::pair{ArrivalRoutine, Handler{&arrival}}, std::pair{CounterRoutine, Handler{&counter}},
        std::pair{BonusRoutine, Handler{&bonus}}}) {
    auto added = scenes.routines.registerRoutine(
        number,
        [&scenes, handler](game::story::RoutineNumber /*routine*/)
            -> core::Result<game::story::RoutineStart> { return handler(scenes); });
    if (!added) {
      return added;
    }
  }
  return {};
}

} // namespace dwe::session
