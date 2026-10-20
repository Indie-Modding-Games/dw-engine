#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::partner {

// The life section (rules/<id>/life.yaml, docs/reference/care.md "Vida y edad", "Muerte y renacer"). Hours are game
// hours.
inline constexpr std::string_view LifeSection = "life";

// The lifetime unhappiness costs: every so many hours, below a happiness, (happiness - below) / -divisor + extra
// hours.
struct UnhappyLifetime {
    std::int32_t everyHours = 0;
    std::int32_t happinessBelow = 0;
    std::int32_t happinessDivisor = 0;
    std::int32_t extraHours = 0;
};

// A baby the partner can be reborn as, with its starting stats (initializeReincarnatedPartner).
struct RebirthBaby {
    std::string digimon;
    StatBlock stats;
};

struct RebirthRules {
    std::string babyChoice;
    std::vector<RebirthBaby> babies;
    std::int32_t inheritPercentPerTamerLevel = 0;
};

struct LifeRules {
    std::int32_t lifetimeHours = 0;
    std::int32_t lives = 0;
    UnhappyLifetime unhappyLifetime;
    RebirthRules rebirth;
};

// Reads the life section; errors name the file, line and column.
core::Result<LifeRules> readLifeRules(const ruleset::Section& section);

} // namespace dwe::game::partner
