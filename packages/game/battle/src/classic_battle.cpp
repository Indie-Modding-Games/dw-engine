#include <memory>

#include "battle_context.hpp"
#include "battle_system.hpp"

namespace dwe::game::battle {

namespace {

// The original's battle: the frame's decisions and the attacks run as they always did.
class ClassicBattle final : public BattleSystem {
public:
  [[nodiscard]] BattleSystemId id() const override { return BattleSystemId::Classic; }
  void start(BattleContext& /*context*/) override {}
  void tickDecisions(BattleContext& context) override {
    tickPartnerAi(context);
    tickEnemyAi(context);
  }
  void handleAttack(BattleContext& context, const AttackInFlight& attack) override {
    handleOriginalAttack(context, attack);
  }
  void endFrame(BattleContext& /*context*/) override {}
};

} // namespace

std::unique_ptr<BattleSystem> makeClassicBattle() {
  return std::make_unique<ClassicBattle>();
}

} // namespace dwe::game::battle
