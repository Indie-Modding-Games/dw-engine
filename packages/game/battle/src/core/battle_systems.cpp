#include <memory>
#include <optional>
#include <string_view>

#include "classic/classic.hpp"
#include "core/battle_system.hpp"
#include "folder/folder.hpp"

namespace dwe::game::battle {

std::string_view battleSystemName(BattleSystemId system) {
  return system == BattleSystemId::Folder ? FolderBattleName : ClassicBattleName;
}

std::optional<BattleSystemId> battleSystemOf(std::string_view name) {
  if (name == ClassicBattleName) {
    return BattleSystemId::Classic;
  }
  if (name == FolderBattleName) {
    return BattleSystemId::Folder;
  }
  return std::nullopt;
}

std::unique_ptr<BattleSystem> makeBattleSystem(BattleSystemId id) {
  switch (id) {
  case BattleSystemId::Classic:
    return makeClassicBattle();
  case BattleSystemId::Folder:
    return makeFolderBattle();
  }
  return makeClassicBattle();
}

} // namespace dwe::game::battle
