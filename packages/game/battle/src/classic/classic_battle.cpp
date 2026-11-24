#include <memory>

#include "classic/classic.hpp"
#include "core/battle_system.hpp"

namespace dwe::game::battle {

namespace {

class ClassicBattle final : public BattleSystem {
public:
  [[nodiscard]] BattleSystemId id() const override { return BattleSystemId::Classic; }
  void start(BattleContext& /*context*/) override {}
  core::Result<void> command(BattleContext& context, Command command, FighterId side) override {
    return classicCommand(context, command, side);
  }
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
