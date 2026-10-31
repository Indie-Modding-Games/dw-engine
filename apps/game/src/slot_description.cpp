#include "slot_description.hpp"

#include <chrono>
#include <format>
#include <string_view>

#include "messages.hpp"

namespace dwe::app {

namespace {

// What the slot list will show of a saved slot (its summary); messages.hpp holds the rest of the
// line.
constexpr std::string_view SummaryText =
    R"(; playthrough slot "{}", tamer "{}", partner "{}" "{}", map "{}")";

} // namespace

std::string describeSlot(const game::save::SlotInfo& info) {
  using game::save::SlotInfo;
  // A newer game's save has no header this game reads either: the line says why, as for a damaged
  // one, instead of calling the slot empty.
  if (info.state == SlotInfo::State::Damaged || info.state == SlotInfo::State::Newer) {
    return info.problem;
  }
  const std::string name = game::save::slotFileName(info.slot);
  if (!info.header) {
    return std::format(messages::EmptySlot, name);
  }
  const game::save::SaveHeader& header = *info.header;
  const std::chrono::hh_mm_ss played{std::chrono::seconds{header.playSeconds}};
  const game::save::SaveSummary& summary = header.summary;
  return std::format(messages::SavedSlot, name, header.label, played.hours().count(),
                     played.minutes().count(), header.rulesetId, header.updated,
                     header.gameVersion) +
         std::format(SummaryText, summary.slot, summary.tamer, summary.species, summary.nickname,
                     summary.map);
}

} // namespace dwe::app
