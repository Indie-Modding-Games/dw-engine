#include "tournament_match_screen.hpp"

#include <dwe/localization/catalog.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace dwe::app {

namespace {

constexpr std::string_view Revision = "tournament_match.revision";
constexpr std::string_view Prefix = "tournament_match.";
// STD_renderIntroStats: the labels (localization/<language>/tournament.yaml) and the bars' full
// values (STD_renderIntroStatBar: STD_INTRO_STAT_LIMITS).
constexpr std::array<std::string_view, 6> StatKeys{
    "tournament.match.hp",      "tournament.match.mp",    "tournament.match.offense",
    "tournament.match.defense", "tournament.match.speed", "tournament.match.brains"};
constexpr std::array<std::int32_t, 6> StatLimits{9999, 9999, 999, 999, 999, 999};
constexpr std::int32_t BarWidth = 100;
constexpr std::string_view DamageKey = "tournament.match.damage";
constexpr std::string_view TimeUpKey = "tournament.match.time_up";

std::string setting(std::string_view name) {
  return std::format("{}{}", Prefix, name);
}

} // namespace

void TournamentMatchScreen::show(const TournamentMatchView& view) {
  if (ui_ == nullptr) {
    return;
  }
  screens::ScreenSettings out = settings(view);
  if (shown_ && last_ == out) {
    return;
  }
  screens::publish(*ui_, out, Revision);
  last_ = std::move(out);
  if (!shown_) {
    ui_->show(TournamentMatchDocument);
    shown_ = true;
  }
}

void TournamentMatchScreen::hide() {
  if (!shown_ || ui_ == nullptr) {
    return;
  }
  shown_ = false;
  last_.reset();
  screens::publish(*ui_, {{.name = setting("shown"), .value = false}}, Revision);
  ui_->hide(TournamentMatchDocument);
}

screens::ScreenSettings TournamentMatchScreen::settings(const TournamentMatchView& view) const {
  const auto number = [](auto value) { return static_cast<double>(value); };
  screens::ScreenSettings out{
      {.name = setting("shown"), .value = true},
      {.name = setting("time_up_label"), .value = localization::tr(TimeUpKey)},
      {.name = setting("name"), .value = view.intro ? view.intro->name : std::string{}},
      {.name = setting("name_y"), .value = number(view.intro ? view.intro->nameY : 0)},
      {.name = setting("stats"), .value = view.intro && view.intro->stats.has_value()},
      {.name = setting("counted"), .value = view.intro && view.intro->counted},
  };
  for (std::size_t i = 0; i < StatKeys.size(); ++i) {
    const std::int32_t value = view.intro && view.intro->stats ? view.intro->stats->at(i) : 0;
    const std::string base = std::format("stat.{}.", i);
    out.push_back({.name = setting(base + "label"), .value = localization::tr(StatKeys.at(i))});
    out.push_back({.name = setting(base + "value"), .value = number(value)});
    // STD_renderIntroStatBar: at least one pixel.
    out.push_back({.name = setting(base + "width"),
                   .value = number(std::max(1, value * BarWidth / StatLimits.at(i)))});
  }
  out.push_back({.name = setting("time_up"), .value = view.timeUp && !view.timeUp->box});
  out.push_back({.name = setting("box"), .value = view.timeUp && view.timeUp->box});
  out.push_back(
      {.name = setting("partner"), .value = view.timeUp ? view.timeUp->partner : std::string{}});
  out.push_back(
      {.name = setting("rival"), .value = view.timeUp ? view.timeUp->rival : std::string{}});
  out.push_back({.name = setting("damage_label"), .value = localization::tr(DamageKey)});
  out.push_back({.name = setting("dealt"), .value = number(view.timeUp ? view.timeUp->dealt : 0)});
  out.push_back({.name = setting("taken"), .value = number(view.timeUp ? view.timeUp->taken : 0)});
  for (std::size_t i = 0; i < 32; ++i) {
    out.push_back({.name = setting(std::format("wipe.{}", i)),
                   .value = number(i < view.wipe.size() ? view.wipe[i] : 0)});
  }
  return out;
}

} // namespace dwe::app
