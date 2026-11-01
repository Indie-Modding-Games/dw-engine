#include "partner_set_command.hpp"

#include <dwe/game/partner/partner.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace dwe::app {

namespace {

using Kind = PartnerWrite::Kind;

constexpr std::int32_t ValueLimit = 99999;

constexpr std::array<std::pair<std::string_view, Kind>, 10> Names{{{"hp", Kind::Hp},
                                                                   {"mp", Kind::Mp},
                                                                   {"offense", Kind::Offense},
                                                                   {"defense", Kind::Defense},
                                                                   {"speed", Kind::Speed},
                                                                   {"brains", Kind::Brains},
                                                                   {"weight", Kind::Weight},
                                                                   {"care", Kind::CareMistakes},
                                                                   {"battles", Kind::Battles},
                                                                   {"hours", Kind::Hours}}};

std::optional<std::int32_t> integerOf(std::string_view text) {
  std::int32_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

game::partner::Stat statOf(Kind kind) {
  switch (kind) {
  case Kind::Mp:
    return game::partner::Stat::Mp;
  case Kind::Offense:
    return game::partner::Stat::Offense;
  case Kind::Defense:
    return game::partner::Stat::Defense;
  case Kind::Speed:
    return game::partner::Stat::Speed;
  case Kind::Brains:
    return game::partner::Stat::Brain;
  default:
    return game::partner::Stat::Hp;
  }
}

} // namespace

core::Result<PartnerWrite> partnerWriteFromText(std::string_view text) {
  std::vector<std::string_view> words;
  for (const auto word : std::views::split(text, ' ')) {
    if (!std::ranges::empty(word)) {
      words.emplace_back(std::ranges::begin(word), std::ranges::end(word));
    }
  }
  const auto usage = [&] {
    return core::fail(std::format("'{}': expected '<hp|mp|offense|defense|speed|brains|weight|"
                                  "care|battles|hours> <0-{}>'",
                                  text, ValueLimit));
  };
  if (words.size() != 2) {
    return usage();
  }
  const auto value = integerOf(words[1]);
  const auto name = std::ranges::find(Names, words[0], &std::pair<std::string_view, Kind>::first);
  if (!value || *value < 0 || *value > ValueLimit || name == Names.end()) {
    return usage();
  }
  return PartnerWrite{.kind = name->second, .value = *value};
}

void applyPartnerWrite(game::partner::Partner& partner, const PartnerWrite& write) {
  const game::partner::PartnerState& state = partner.state();
  game::partner::EvolutionCounters counters = state.evolution;
  switch (write.kind) {
  case Kind::Weight:
    partner.setWeight(write.value);
    return;
  case Kind::CareMistakes:
    counters.careMistakes = write.value;
    partner.setEvolutionCounters(counters);
    return;
  case Kind::Battles:
    counters.battles = write.value;
    partner.setEvolutionCounters(counters);
    return;
  case Kind::Hours:
    counters.hours = write.value;
    partner.setEvolutionCounters(counters);
    return;
  default:
    break;
  }
  game::partner::PartnerStats stats = state.stats;
  stats.base[statOf(write.kind)] = write.value;
  partner.setStats(stats);
}

} // namespace dwe::app
