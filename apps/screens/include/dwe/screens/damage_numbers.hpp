#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::screens {

// The settings of the battle's floating numbers on the battle document (ui/battle/document), which
// redraws them on DamageRevisionSetting. DamageNumbersSetting packs the numbers drawn on the last
// battle frame, six values each: the anchor's x and y in percent of the window, the offset of the
// first digit's top-left corner from it in the original's pixels (x, y), the value and the colour
// (NumberColour).
inline constexpr std::string_view DamageRevisionSetting = "battle.damage.revision";
inline constexpr std::string_view DamageNumbersSetting = "battle.damage.numbers";

// The tints a battle's numbers take (utils.c: TEXT_COLORS): 0, the digits as they are, for the
// damage of a hit or a block (battle_main.c: BTL_tickAttackHits), and 0xc, red and blue only, for
// the poison's (BTL_tickStatusEffects).
enum class NumberColour : std::uint8_t { Plain = 0, Poison = 0xc };

// A number as one battle frame draws it: where the fighter's feet showed when the number was added
// (nothing when they were off the camera), the first digit's top-left corner from there in the
// original's pixels, the value and its tint.
struct ShownNumber {
  WindowFraction anchor;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t value = 0;
  NumberColour colour = NumberColour::Plain;

  friend bool operator==(const ShownNumber&, const ShownNumber&) = default;
};

// The numbers that pop out of the fighters in battle (entity_text.c: addEntityText,
// setCombatTextPosition, renderEntityText, removeEntityText). Each fighter has its own queue of up
// to 8: a number is fixed on the screen where its fighter's feet were, 8 pixels up, and each one
// added while the queue is busy sits 4 pixels further right and down. A number bounces up 20 pixels
// and back over 11 frames, then up 10 and down to 4 over its start, stays until its 31st frame and
// goes; the next one waits until the one before has shown 11 frames. Frame by frame at the battle's
// rate.
class DamageNumbers {
public:
  static constexpr std::size_t Fighters = 4;
  static constexpr std::size_t PerFighter = 8;

  // Queues `value` over `fighter` (0 the partner, then the enemies), anchored at its feet as the
  // screen shows them now. A fighter whose queue took 8 since it was last empty takes no more.
  void add(std::size_t fighter, std::int32_t value, NumberColour colour,
           std::optional<WindowFraction> feet);
  // One battle frame: each queue moves and draws its numbers (renderEntityText).
  void step();
  // Takes every number away (removeEntityText for each fighter).
  void clear();

  [[nodiscard]] bool empty() const;
  // The numbers the last step drew, the partner's first.
  [[nodiscard]] const std::vector<ShownNumber>& shown() const { return shown_; }

private:
  struct Entry {
    std::optional<WindowFraction> anchor;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t value = 0;
    NumberColour colour = NumberColour::Plain;
    std::int32_t frame = 0;
  };
  struct Queue {
    // The numbers taken since the queue was last empty (activeElements): it never counts down.
    std::size_t added = 0;
    // The numbers still on, in the order they show (activeList).
    std::vector<Entry> entries;
  };

  std::array<Queue, Fighters> queues_{};
  std::vector<ShownNumber> shown_;
};

// The numbers as the settings the battle document reads.
[[nodiscard]] ScreenSettings settingsOf(const std::vector<ShownNumber>& shown);

} // namespace dwe::screens
