#include <dwe/screens/damage_numbers.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <string>
#include <utility>

namespace dwe::screens {
namespace {

// How far a number moves down on each of its first 21 frames (entity_text.c:
// ENTITY_TEXT_Y_OFFSETS): up 20 and back, then up 10 and down to 4 over where it started.
constexpr std::array<std::int32_t, 21> Bounce{
    0, -7, -5, -5, -2, -1, 1, 2, 5, 5, 7, -4, -3, -2, 0, -1, 1, 0, 2, 3, 0,
};
// A number shows for 31 frames; the next one in its queue waits until it has shown 11.
constexpr std::int32_t LastFrame = 31;
constexpr std::int32_t NextAfter = 11;
// setCombatTextPosition: 8 pixels over the feet; addEntityText: 4 pixels right and down for each
// number the queue took before.
constexpr std::int32_t Lift = 8;
constexpr std::int32_t Stagger = 4;

constexpr double Percent = 100.0;
constexpr double Tenths = 10.0;

double rounded(double value) {
  return std::round(value * Tenths) / Tenths;
}

} // namespace

void DamageNumbers::add(std::size_t fighter, std::int32_t value, NumberColour colour,
                        std::optional<WindowFraction> feet, NumberIcon icon) {
  if (fighter >= Fighters) {
    return;
  }
  Queue& queue = queues_.at(fighter);
  if (queue.added == PerFighter) {
    return;
  }
  const auto stagger = static_cast<std::int32_t>(queue.added) * Stagger;
  queue.entries.push_back({.anchor = feet,
                           .x = stagger,
                           .y = stagger - Lift,
                           .value = value,
                           .colour = colour,
                           .icon = icon,
                           .frame = 0});
  ++queue.added;
}

void DamageNumbers::step() {
  shown_.clear();
  for (Queue& queue : queues_) {
    for (std::size_t index = 0; index < queue.entries.size(); ++index) {
      Entry& entry = queue.entries[index];
      // The one before it is still rising (one that just went counts as gone).
      if (index > 0 && queue.entries[index - 1].frame < NextAfter) {
        break;
      }
      if (entry.frame < static_cast<std::int32_t>(Bounce.size())) {
        entry.y += Bounce.at(static_cast<std::size_t>(entry.frame));
      }
      if (entry.anchor) {
        shown_.push_back({.anchor = *entry.anchor,
                          .x = entry.x,
                          .y = entry.y,
                          .value = entry.value,
                          .colour = entry.colour,
                          .icon = entry.icon});
      }
      ++entry.frame;
    }
    std::erase_if(queue.entries, [](const Entry& entry) { return entry.frame >= LastFrame; });
    if (queue.entries.empty()) {
      queue.added = 0;
    }
  }
}

void DamageNumbers::clear() {
  for (Queue& queue : queues_) {
    queue = Queue{};
  }
  shown_.clear();
}

bool DamageNumbers::empty() const {
  return std::ranges::all_of(queues_, [](const Queue& queue) { return queue.entries.empty(); });
}

ScreenSettings settingsOf(const std::vector<ShownNumber>& shown) {
  std::string text;
  for (const ShownNumber& number : shown) {
    std::format_to(std::back_inserter(text), "{:.1f} {:.1f} {} {} {} {} {} ",
                   rounded(number.anchor.x * Percent), rounded(number.anchor.y * Percent), number.x,
                   number.y, number.value, static_cast<int>(number.colour),
                   static_cast<int>(number.icon));
  }
  return {{.name = std::string{DamageNumbersSetting}, .value = std::move(text)}};
}

} // namespace dwe::screens
