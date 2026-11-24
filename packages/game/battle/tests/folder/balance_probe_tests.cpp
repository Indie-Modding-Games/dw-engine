#include <dwe/game/battle/folder/formulas.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "folder_kit.hpp"

using namespace folder_test;

// Not a check: the numbers the provisional Folder rules are tuned with (folder-battle.md, "Pending
// balance"). Run with -tc='*balance probe*' --no-skip.
TEST_CASE("folder balance probe" * doctest::skip()) {
  struct Matchup {
    const char* name;
    CombatStats partner;
    CombatStats enemy;
  };
  const std::vector<Matchup> matchups = {
      {"even 300/100", evenStats(300, 100), evenStats(300, 100)},
      {"slightly weaker", evenStats(300, 100), evenStats(330, 115)},
      {"weaker", evenStats(300, 100), evenStats(400, 150)},
      {"stronger", evenStats(400, 150), evenStats(300, 100)},
      {"even 600/200", evenStats(600, 200), evenStats(600, 200)},
  };
  constexpr std::int64_t Seeds = 40;
  struct Variant {
    std::string name;
    BattleSystemId system;
    BattleRules rules;
  };
  std::vector<Variant> variants{{"classic", BattleSystemId::Classic, originalRules()}};
  for (const int percent : {100, 150, 200, 300}) {
    variants.push_back({"folder atk " + std::to_string(percent), BattleSystemId::Folder,
                        rulesOf(originalWith("attack_percent: 100",
                                             "attack_percent: " + std::to_string(percent)))});
  }
  for (const Matchup& matchup : matchups) {
    for (const Variant& variant : variants) {
      std::int64_t wins = 0;
      std::int64_t frames = 0;
      std::int64_t hpLeft = 0;
      for (std::int64_t seed = 1; seed <= Seeds; ++seed) {
        BattleSetup setup =
            duel(matchup.partner, matchup.enemy, {"punch", "shot"}, {"punch", "shot"});
        setup.system = variant.system;
        Battle battle =
            startBattle(std::move(setup), static_cast<std::uint64_t>(seed), variant.rules);
        while (!battle.finished() && battle.state().frame < 20000) {
          if (const FolderFighter* own = battle.folderOf(PartnerId);
              own && !own->active && own->queue.empty()) {
            const bool shot = battle.state().fighters[PartnerId].mp >= 10;
            static_cast<void>(battle.order(shot ? 1 : 0, 1));
          }
          battle.step();
        }
        if (const std::optional<BattleResult>& result = battle.result()) {
          wins += result->outcome == Outcome::Victory ? 1 : 0;
          frames += result->frames;
          hpLeft += result->partnerHp;
        } else {
          frames += battle.state().frame;
        }
      }
      MESSAGE(std::string{matchup.name} << " " << variant.name << ": wins " << wins * 100 / Seeds
                                        << "%, frames " << frames / Seeds << ", partner hp left "
                                        << hpLeft / Seeds);
    }
  }
}
