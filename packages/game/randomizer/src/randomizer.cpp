#include <dwe/game/randomizer/randomizer.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::randomizer {

namespace {

constexpr std::uint64_t FnvOffset = 0xCBF29CE484222325U;
constexpr std::uint64_t FnvPrime = 0x100000001B3U;
constexpr std::uint64_t KeyEnd = 0xFFU;

// splitmix64's finalizer.
std::uint64_t mix(std::uint64_t value) {
  value += 0x9E3779B97F4A7C15U;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9U;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBU;
  return value ^ (value >> 31U);
}

void feed(std::uint64_t& state, std::string_view text) {
  for (const char character : text) {
    state ^= static_cast<std::uint8_t>(character);
    state *= FnvPrime;
  }
  state ^= KeyEnd;
  state *= FnvPrime;
}

bool listed(const std::vector<std::string>& list, std::string_view id) {
  return std::ranges::find(list, id) != list.end();
}

} // namespace

std::uint64_t Randomizer::hash(std::string_view domain,
                               std::initializer_list<std::string_view> keys) const {
  std::uint64_t state = FnvOffset ^ mix(settings_.seed);
  feed(state, domain);
  for (const std::string_view key : keys) {
    feed(state, key);
  }
  return mix(state);
}

std::optional<std::string> Randomizer::pick(std::string_view domain,
                                            std::initializer_list<std::string_view> keys,
                                            Pool pool) const {
  std::vector<std::string_view> allowed;
  for (const std::string& candidate : pool) {
    if (!excludesSpecies(candidate) && !keepsItem(candidate) && !excludesItem(candidate)) {
      allowed.emplace_back(candidate);
    }
  }
  if (allowed.empty()) {
    return std::nullopt;
  }
  return std::string{allowed[hash(domain, keys) % allowed.size()]};
}

std::optional<std::string> Randomizer::evolution(std::string_view from, std::string_view target,
                                                 Pool stage) const {
  if (!settings_.evolutions) {
    return std::nullopt;
  }
  return pick("evolution", {from, target}, stage);
}

std::optional<std::string> Randomizer::starter(std::string_view /*original*/, Pool rookies) const {
  if (!settings_.starter) {
    return std::nullopt;
  }
  return pick("starter", {}, rookies);
}

std::optional<std::int32_t> Randomizer::learnChance() const {
  return percentOf(settings_.learnChance);
}

bool Randomizer::wildEnemy(std::string_view map, std::size_t slot) const {
  const auto found = rules_.enemies.find(map);
  return found != rules_.enemies.end() && std::ranges::contains(found->second, slot);
}

std::optional<std::string> Randomizer::enemy(std::string_view map, std::size_t slot,
                                             Pool stage) const {
  if (!settings_.enemies || !wildEnemy(map, slot)) {
    return std::nullopt;
  }
  return pick("enemy", {map, std::to_string(slot)}, stage);
}

std::optional<std::string> Randomizer::fieldItem(std::string_view map, std::string_view spot,
                                                 std::string_view original, Pool items) const {
  if (!settings_.items || keepsItem(original) || excludesItem(original)) {
    return std::nullopt;
  }
  return pick("item", {map, spot, original}, items);
}

std::optional<std::string> Randomizer::drop(std::string_view species, std::string_view original,
                                            Pool items) const {
  if (!settings_.drops || keepsItem(original)) {
    return std::nullopt;
  }
  return pick("drop", {species}, items);
}

bool Randomizer::keepsItem(std::string_view item) const {
  return listed(rules_.keptItems, item);
}

bool Randomizer::excludesItem(std::string_view item) const {
  return listed(rules_.excludedItems, item);
}

bool Randomizer::excludesSpecies(std::string_view species) const {
  return listed(rules_.excludedSpecies, species);
}

} // namespace dwe::game::randomizer
