#include <dwe/core/log.hpp>
#include <dwe/game/story/partner_routines.hpp>

#include "routine_messages.hpp"

namespace dwe::game::story {

namespace {

constexpr std::int64_t LastScene = static_cast<std::int64_t>(PartnerScene::EvolveTo);

} // namespace

PartnerRoutines::PartnerRoutines(StoryStatePort& state, PartnerScenePort& scenes, SceneSync& sync,
                                 PartnerRoutineRules rules)
    : state_{&state}, scenes_{&scenes}, sync_{&sync}, rules_{rules} {}

core::Result<void> PartnerRoutines::call(std::int64_t routine) {
  if (routine < 0 || routine > LastScene) {
    core::logDebug("{}", messages::formatMessage(messages::PartnerRoutineUnknown, routine));
    return {};
  }
  const auto scene = static_cast<PartnerScene>(routine);
  std::optional<std::int64_t> species;
  if (scene == PartnerScene::ReturnFromVirus || scene == PartnerScene::EvolveTo) {
    // partner.c: callDigimonRoutine sets EVOLUTION_TARGET from the variable before the scene.
    const std::int64_t variable =
        scene == PartnerScene::EvolveTo ? rules_.targetVariable : rules_.virusBackupVariable;
    const auto target = state_->variable(variable);
    if (!target) {
      return std::unexpected{target.error()};
    }
    species = *target;
  }
  core::logDebug(
      "{}", messages::formatMessage(messages::PortPartnerRoutine, routine, species.value_or(-1)));
  sync_->begin();
  scenes_->startScene(scene, species);
  return {};
}

} // namespace dwe::game::story
