#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::game::battle {

// Which battle a fight plays: the original's, or the combat design's.
enum class BattleSystemId : std::uint8_t { Classic, Folder };

inline constexpr std::string_view ClassicBattleName = "classic";
inline constexpr std::string_view FolderBattleName = "folder";

[[nodiscard]] std::string_view battleSystemName(BattleSystemId system);
[[nodiscard]] std::optional<BattleSystemId> battleSystemOf(std::string_view name);

} // namespace dwe::game::battle
