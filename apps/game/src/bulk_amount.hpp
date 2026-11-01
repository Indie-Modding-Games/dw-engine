#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace dwe::app {

// The user setting and the pause menu option of the bulk trades (an engine improvement, on by
// default): the item trader and the card shop's packs ask how many at once.
inline constexpr std::string_view BulkTradesSetting = "bulk_trades";

// The "How many?" count of the bulk trades: from 1 to the most the trade allows; left and right
// move it by one, up and down by ten (stopping at the ends), and the extra button takes the most.
struct BulkAmount {
  static constexpr std::int32_t Step = 10;

  std::int32_t value = 1;
  std::int32_t most = 1;

  void reset(std::int32_t limit) {
    most = std::max(limit, 1);
    value = 1;
  }

  // Whether the action is the count's (and so taken).
  bool act(std::string_view action) {
    std::int32_t change = 0;
    if (action == "right") {
      change = 1;
    } else if (action == "left") {
      change = -1;
    } else if (action == "up") {
      change = Step;
    } else if (action == "down") {
      change = -Step;
    } else if (action == "extra") {
      value = most;
      return true;
    } else {
      return false;
    }
    value = std::clamp(value + change, 1, most);
    return true;
  }
};

} // namespace dwe::app
