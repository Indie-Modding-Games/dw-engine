#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <string_view>

namespace dwe::game::partner {
class Partner;
}

namespace dwe::app {

// The console's and the scripts' partner_set("<value> <amount>"): sets one of the partner's values
// the evolution reads (hp, mp, offense, defense, speed, brains, weight, care, battles, hours), so a
// route can reach an evolution's requirement without raising the partner for days. A debug aid: it
// runs no rule; the next check of the evolution reads the values as they are.
inline constexpr std::string_view PartnerSetCommand = "partner_set";

struct PartnerWrite {
  enum class Kind : std::uint8_t {
    Hp,
    Mp,
    Offense,
    Defense,
    Speed,
    Brains,
    Weight,
    CareMistakes,
    Battles,
    Hours
  };
  Kind kind = Kind::Hp;
  std::int32_t value = 0;
};

// Reads a partner_set argument, or says what is wrong with it.
core::Result<PartnerWrite> partnerWriteFromText(std::string_view text);

// Writes the value into the partner's state.
void applyPartnerWrite(game::partner::Partner& partner, const PartnerWrite& write);

} // namespace dwe::app
