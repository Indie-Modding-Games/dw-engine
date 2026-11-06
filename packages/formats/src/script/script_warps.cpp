#include <dwe/core/collision_triggers.hpp>
#include <dwe/formats/script/routine_writes.hpp>
#include <dwe/formats/script/script_warps.hpp>
#include <dwe/formats/script/script_yaml.hpp>

#include <yaml/yaml_values.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "listing_vocabulary.hpp"

namespace dwe::formats::script {

namespace {

constexpr std::string_view ScriptOperand = "script";
constexpr std::string_view OffsetKey = "offset";
constexpr std::string_view RecursionKey = "recursion";
constexpr std::string_view ForgetKey = "forget";
constexpr std::string_view TriggersKey = "triggers";
constexpr std::array<std::string_view, 5> ViaNames = {"step", "confirm", "enter", "other", "talk"};
constexpr std::string_view RoutineOperand = "routine";
// The comparison of the money test of a transport pick: MONEY must reach the cost (CompareNames:
// ge).
constexpr std::int32_t AtLeastComparison = 2;
constexpr std::string_view MapOperand = "map";

bool loadsScript(const ListingInstruction& instruction) {
  return instruction.role == FlowRole::LoadScript;
}

bool endsSection(const ListingInstruction& instruction) {
  return instruction.role == FlowRole::EndSection || instruction.role == FlowRole::EndScript ||
         instruction.role == FlowRole::MapChange;
}

const ListingInstruction* instructionAt(const ScriptLookup& lookup, const ScriptPlace& place) {
  const auto code = lookup(place.script);
  if (!code || code->listing == nullptr ||
      place.instruction >= code->listing->instructions.size()) {
    return nullptr;
  }
  return &code->listing->instructions[place.instruction];
}

// The offset of an instruction, for the documents: nothing when the script's listing has none of
// its own (WalkableScript::offsets).
std::optional<std::uint32_t> offsetAt(const ScriptLookup& lookup, const ScriptPlace& place) {
  const auto code = lookup(place.script);
  const ListingInstruction* instruction = instructionAt(lookup, place);
  if (!code || !code->offsets || instruction == nullptr) {
    return std::nullopt;
  }
  return instruction->offset;
}

// The script and map a SET_SCRIPT loads.
struct Load {
  std::uint16_t script = 0;
  std::uint16_t map = 0;
};

std::optional<Load> loadOf(const ScriptLookup& lookup, const GuardedInstruction& found) {
  const ListingInstruction* instruction = instructionAt(lookup, found.place);
  if (instruction == nullptr) {
    return std::nullopt;
  }
  const auto script = instruction->argument(ScriptOperand);
  const auto map = instruction->argument(MapOperand);
  if (!script || !map) {
    return std::nullopt;
  }
  return Load{.script = static_cast<std::uint16_t>(*script),
              .map = static_cast<std::uint16_t>(*map)};
}

bool callsTransport(const ListingInstruction& instruction) {
  return instruction.op == CallRoutineOp &&
         instruction.argument(RoutineOperand) == TransportRoutine;
}

// The listing's IF entry for a test the game code makes: a trigger set, or MONEY at least an
// amount.
Condition triggerSetTest(std::uint16_t trigger) {
  return {
      .code = vocabulary::entryCode(ConditionTest::Trigger, 0, Combine::Replace),
      .test = ConditionTest::Trigger,
      .combine = Combine::Replace,
      .arguments = {Argument{.name = "trigger", .kind = ArgumentKind::Trigger, .value = trigger}}};
}

Condition moneyAtLeastTest(std::uint32_t amount) {
  return {.code = vocabulary::entryCode(ConditionTest::Money, 0, Combine::Replace),
          .test = ConditionTest::Money,
          .combine = Combine::Replace,
          .arguments = {Argument{.name = vocabulary::CompareKey,
                                 .kind = ArgumentKind::U8,
                                 .value = AtLeastComparison},
                        Argument{.name = "value",
                                 .kind = ArgumentKind::S32,
                                 .value = static_cast<std::int32_t>(amount)}}};
}

// The transport menu's picks a call that opens it makes: one per entry of the table, each behind
// the entry's trigger (birdraFillTargetList lists it), its cost (it must be affordable) and the
// player's pick of it.
std::vector<std::pair<MapChange, TransportPick>>
transportPicks(const GuardedInstruction& call, std::span<const TransportDestination> table) {
  std::vector<std::pair<MapChange, TransportPick>> picks;
  for (std::size_t entry = 0; entry < table.size(); ++entry) {
    const TransportDestination& destination = table[entry];
    GuardedInstruction path = call;
    path.guard.terms.push_back(
        {.decision = BranchTerm{
             .place = call.place, .tests = {triggerSetTest(destination.trigger)}, .holds = true}});
    path.guard.terms.push_back(
        {.decision = BranchTerm{
             .place = call.place, .tests = {moneyAtLeastTest(destination.cost)}, .holds = true}});
    path.guard.terms.push_back(
        {.decision = ChoiceTerm{.place = call.place, .arm = entry, .selection = true}});
    picks.emplace_back(
        MapChange{.path = std::move(path),
                  .screen = destination.map,
                  .exit = destination.exit,
                  .arrivalSection = std::nullopt,
                  .screenVariable = std::nullopt},
        TransportPick{.entry = static_cast<std::uint8_t>(entry), .cost = destination.cost});
  }
  return picks;
}

// The map changes of every section of a script the game can run, by section, walked once per
// script.
class SectionChanges {
public:
  SectionChanges(const ScriptLookup& lookup, std::span<const TransportDestination> transport)
      : lookup_(&lookup), transport_(transport) {}

  // The transport pick of a section's change, by its index among the section's changes.
  [[nodiscard]] std::optional<TransportPick> pickOf(std::uint16_t script, std::uint16_t section,
                                                    std::size_t change) const {
    const auto found = picks_.find({script, section});
    if (found == picks_.end()) {
      return std::nullopt;
    }
    const auto pick =
        std::ranges::find(found->second, change, &std::pair<std::size_t, TransportPick>::first);
    return pick != found->second.end() ? std::optional{pick->second} : std::nullopt;
  }

  const std::vector<std::pair<std::uint16_t, std::vector<MapChange>>>& of(std::uint16_t script) {
    const auto cached = cache_.find(script);
    if (cached != cache_.end()) {
      return cached->second;
    }
    auto& sections = cache_[script];
    const auto code = (*lookup_)(script);
    if (code && code->listing != nullptr) {
      for (const ListingSection& section : code->listing->runnableSections()) {
        auto found = findMapChanges(script, section.id, *lookup_);
        for (const GuardedInstruction& call :
             walkSection(script, section.id, *lookup_, &callsTransport)) {
          for (auto& [change, pick] : transportPicks(call, transport_)) {
            found.emplace_back(std::move(change));
            picks_[{script, section.id}].emplace_back(found.size() - 1, pick);
          }
        }
        sections.emplace_back(section.id, std::move(found));
      }
    }
    return sections;
  }

private:
  const ScriptLookup* lookup_;
  std::span<const TransportDestination> transport_;
  std::map<std::uint16_t, std::vector<std::pair<std::uint16_t, std::vector<MapChange>>>> cache_;
  std::map<std::pair<std::uint16_t, std::uint16_t>,
           std::vector<std::pair<std::size_t, TransportPick>>>
      picks_;
};

using core::YamlNode;

YamlNode guardToYaml(const PathGuard& guard, const ScriptLookup& lookup) {
  YamlNode node = YamlNode::mapping();
  if (guard.unstructured) {
    node.set("unstructured", yaml::boolean(true));
  }
  YamlNode terms = YamlNode::sequence();
  for (const GuardTerm& term : guard.terms) {
    YamlNode entry = YamlNode::mapping();
    if (const auto* branch = std::get_if<BranchTerm>(&term.decision)) {
      YamlNode tests = YamlNode::sequence();
      for (const Condition& test : branch->tests) {
        tests.append(conditionToYaml(test));
      }
      entry.set("if", std::move(tests));
      entry.set("holds", yaml::boolean(branch->holds));
    } else if (const auto* choice = std::get_if<ChoiceTerm>(&term.decision)) {
      entry.set(choice->selection ? "selection" : "switch", yaml::number(choice->arm));
    } else if (const auto* forget = std::get_if<ForgetTerm>(&term.decision)) {
      YamlNode site = YamlNode::mapping();
      site.set(std::string{ScriptOperand}, yaml::number(forget->place.script));
      if (const auto offset = offsetAt(lookup, forget->place)) {
        site.set(std::string{OffsetKey}, yaml::number(*offset));
      }
      if (!forget->triggers.empty()) {
        YamlNode triggers = YamlNode::sequence();
        for (const std::uint16_t trigger : forget->triggers) {
          triggers.append(yaml::number(trigger));
        }
        site.set(std::string{TriggersKey}, std::move(triggers));
      }
      entry.set(std::string{ForgetKey}, std::move(site));
    } else {
      const auto& recursion = std::get<RecursionTerm>(term.decision);
      YamlNode routine = YamlNode::mapping();
      routine.set(std::string{ScriptOperand}, yaml::number(recursion.routine.script));
      if (const auto offset = offsetAt(lookup, recursion.routine)) {
        routine.set(std::string{OffsetKey}, yaml::number(*offset));
      }
      entry.set(std::string{RecursionKey}, std::move(routine));
    }
    terms.append(std::move(entry));
  }
  node.set("terms", std::move(terms));
  return node;
}

bool hasGuard(const PathGuard& guard) {
  return guard.unstructured || !guard.terms.empty();
}

YamlNode mapToYaml(const MapName& mapName, std::uint16_t map) {
  const auto name = mapName(map);
  return name ? YamlNode::scalar(*name) : yaml::number(map);
}

// Where a map change goes: the map, or the screen for a number no map has, and the exit.
YamlNode destinationToYaml(const MapName& mapName, std::uint16_t screen, std::int32_t exit) {
  YamlNode to = YamlNode::mapping();
  if (const auto name = mapName(screen)) {
    to.set("map", YamlNode::scalar(*name));
  } else {
    to.set("screen", yaml::number(screen));
  }
  to.set("exit", yaml::number(exit));
  return to;
}

YamlNode warpToYaml(const ScriptWarp& warp, const ScriptLookup& lookup, const MapName& mapName) {
  YamlNode node = YamlNode::mapping();
  node.set("map", mapToYaml(mapName, warp.map));
  node.set("via", YamlNode::scalar(std::string{ViaNames.at(static_cast<std::size_t>(warp.via))}));
  node.set("section", yaml::number(warp.section));
  const MapChange& change = warp.change;
  if (change.screen) {
    node.set("to", destinationToYaml(mapName, static_cast<std::uint16_t>(*change.screen),
                                     change.exit.value_or(0)));
  }
  if (change.screenVariable) {
    YamlNode teleport = YamlNode::mapping();
    teleport.set("pstat", yaml::number(*change.screenVariable));
    node.set("teleport", std::move(teleport));
  }
  if (change.arrivalSection && *change.arrivalSection != NoArrivalSection) {
    node.set("arrival_section", yaml::number(*change.arrivalSection));
  }
  if (hasGuard(warp.scriptGuard)) {
    node.set("script_guard", guardToYaml(warp.scriptGuard, lookup));
  }
  if (hasGuard(change.path.guard)) {
    node.set("guard", guardToYaml(change.path.guard, lookup));
  }
  if (warp.transport) {
    YamlNode transport = YamlNode::mapping();
    transport.set("entry", yaml::number(warp.transport->entry));
    transport.set("cost", yaml::number(warp.transport->cost));
    transport.set("hand_off", yaml::number(TransportSection));
    node.set("transport", std::move(transport));
  }
  if (!change.path.effects.empty()) {
    YamlNode effects = YamlNode::sequence();
    for (const TriggerEffect& effect : change.path.effects) {
      YamlNode entry = YamlNode::mapping();
      entry.set("trigger", yaml::number(effect.trigger));
      entry.set("set", yaml::boolean(effect.set));
      effects.append(std::move(entry));
    }
    node.set("sets_flags", std::move(effects));
  }
  YamlNode source = YamlNode::mapping();
  source.set("script", yaml::number(change.path.place.script));
  source.set("section", yaml::number(warp.section));
  if (const auto offset = offsetAt(lookup, change.path.place)) {
    source.set(std::string{OffsetKey}, yaml::number(*offset));
  }
  node.set("source", std::move(source));
  return node;
}

} // namespace

WarpVia warpViaOf(std::uint16_t section) {
  if (section == EnterSection) {
    return WarpVia::Enter;
  }
  if (section >= core::collision::FirstStepTrigger &&
      section < core::collision::FirstConfirmTrigger) {
    return WarpVia::Step;
  }
  if (section >= core::collision::FirstConfirmTrigger &&
      section < core::collision::FirstWarpTrigger) {
    return WarpVia::Confirm;
  }
  return WarpVia::Other;
}

ScriptWarps findScriptWarps(const ScriptLookup& lookup, const ScriptWarpSources& sources) {
  ScriptWarps result;
  const auto mapHead = lookup(MapHeadScript);
  if (!mapHead || mapHead->listing == nullptr) {
    return result;
  }
  SectionChanges changes{lookup, sources.transport};
  for (const ListingSection& section : mapHead->listing->runnableSections()) {
    const auto loads = walkSection(MapHeadScript, section.id, lookup, &loadsScript);
    if (loads.empty()) {
      result.otherMapHeadSections.push_back(section.id);
      continue;
    }
    for (MapChange& change : findMapChanges(MapHeadScript, section.id, lookup)) {
      result.warps.push_back({.map = section.id,
                              .via = WarpVia::Enter,
                              .section = section.id,
                              .script = MapHeadScript,
                              .scriptGuard = {},
                              .change = std::move(change),
                              .transport = std::nullopt});
    }
    for (const GuardedInstruction& found : loads) {
      const auto load = loadOf(lookup, found);
      if (!load) {
        continue;
      }
      for (const auto& [scriptSection, sectionChanges] : changes.of(load->script)) {
        WarpVia via = warpViaOf(scriptSection);
        if (via == WarpVia::Other && sources.talks && sources.talks(load->map, scriptSection)) {
          via = WarpVia::Talk;
        }
        for (std::size_t index = 0; index < sectionChanges.size(); ++index) {
          result.warps.push_back({.map = load->map,
                                  .via = via,
                                  .section = scriptSection,
                                  .script = load->script,
                                  .scriptGuard = found.guard,
                                  .change = sectionChanges[index],
                                  .transport = changes.pickOf(load->script, scriptSection, index)});
        }
      }
    }
  }
  std::ranges::stable_sort(result.warps, [](const ScriptWarp& a, const ScriptWarp& b) {
    return std::pair{a.map, a.section} < std::pair{b.map, b.section};
  });
  return result;
}

std::optional<NewGameTriggers> findNewGameTriggers(const ScriptLookup& lookup) {
  const WalkOptions newGame{.knownTriggers = {}, .othersClear = true};
  const auto loads = walkSection(MapHeadScript, NewGameSection, lookup, &loadsScript, newGame);
  std::optional<NewGameTriggers> result;
  std::vector<std::vector<std::uint16_t>> ends;
  const auto setIn = [](const std::vector<TriggerEffect>& state) {
    std::vector<std::uint16_t> set;
    for (const TriggerEffect& trigger : state) {
      if (trigger.set) {
        set.push_back(trigger.trigger);
      }
    }
    std::ranges::sort(set);
    return set;
  };
  for (const GuardedInstruction& found : loads) {
    const auto load = loadOf(lookup, found);
    if (!load) {
      continue;
    }
    if (!result) {
      result = NewGameTriggers{.map = load->map,
                               .script = load->script,
                               .set = {},
                               .agreed = true,
                               .unknown = {},
                               .opening = {}};
    }
    const WalkOptions entered{.knownTriggers = found.state, .othersClear = true};
    const auto finished = walkSection(load->script, EnterSection, lookup, &endsSection, entered);
    if (finished.empty()) {
      ends.push_back(setIn(found.state));
    }
    for (const GuardedInstruction& end : finished) {
      ends.push_back(setIn(end.state));
    }
  }
  if (!result) {
    return result;
  }
  result->unknown.assign(StartingPartnerRaisedTriggers.begin(),
                         StartingPartnerRaisedTriggers.end());
  std::ranges::sort(result->unknown);
  if (ends.empty()) {
    return result;
  }
  result->set = ends.front();
  for (const auto& set : ends) {
    result->agreed = result->agreed && set == ends.front();
    std::vector<std::uint16_t> shared;
    std::ranges::set_intersection(result->set, set, std::back_inserter(shared));
    result->set = std::move(shared);
  }
  std::erase_if(result->set, [&](std::uint16_t trigger) {
    return std::ranges::binary_search(result->unknown, trigger);
  });
  return result;
}

core::Result<NewGameOpening> findNewGameOpening(const NewGameTriggers& newGame,
                                                std::uint8_t spawnCell,
                                                const ScriptLookup& lookup) {
  if (warpViaOf(spawnCell) != WarpVia::Step) {
    return core::fail(std::format("cell {} is not a step cell", spawnCell));
  }
  if (!newGame.agreed) {
    return core::fail("the new game's paths do not agree on the triggers they set");
  }
  if (newGame.unknown.size() > MaximumOpeningUnknowns) {
    return core::fail(std::format("{} triggers are unknown, more than the {} tried each way",
                                  newGame.unknown.size(), MaximumOpeningUnknowns));
  }
  // The walk takes every trigger it is not told about as clear, so the unknown ones are tried each
  // way: the opening is certain only if it is the same in all.
  std::optional<NewGameOpening> found;
  for (std::size_t mask = 0; mask < (std::size_t{1} << newGame.unknown.size()); ++mask) {
    WalkOptions state{.knownTriggers = {}, .othersClear = true};
    for (const std::uint16_t trigger : newGame.set) {
      state.knownTriggers.push_back({.trigger = trigger, .set = true});
    }
    for (std::size_t i = 0; i < newGame.unknown.size(); ++i) {
      state.knownTriggers.push_back(
          {.trigger = newGame.unknown[i], .set = ((mask >> i) & 1U) != 0});
    }
    const auto changes = findMapChanges(newGame.script, spawnCell, lookup, state);
    if (changes.size() != 1 || !changes.front().screen || hasGuard(changes.front().path.guard)) {
      return core::fail(
          std::format("section {} makes no single map change to a screen that the new game's "
                      "triggers decide",
                      spawnCell));
    }
    const MapChange& change = changes.front();
    NewGameOpening opening{.exit = NewGameExit,
                           .section = spawnCell,
                           .screen = static_cast<std::uint16_t>(*change.screen),
                           .screenExit = change.exit.value_or(0),
                           .sets = {}};
    for (const TriggerEffect& effect : change.path.effects) {
      // The field starts after the opening with the new game's triggers and `sets` set, so it would
      // keep one the path clears.
      if (!effect.set && std::ranges::binary_search(newGame.set, effect.trigger)) {
        return core::fail(std::format("section {} clears trigger {}, which the new game sets",
                                      spawnCell, effect.trigger));
      }
      std::erase(opening.sets, effect.trigger);
      if (effect.set) {
        opening.sets.push_back(effect.trigger);
      }
    }
    std::ranges::sort(opening.sets);
    if (found && *found != opening) {
      return core::fail(std::format("section {} changes with the unknown triggers", spawnCell));
    }
    found = std::move(opening);
  }
  if (!found) {
    return core::fail(std::format("section {} was not walked", spawnCell));
  }
  return std::move(*found);
}

YamlNode scriptWarpsToYaml(const ScriptWarps& warps, const ScriptLookup& lookup,
                           const MapName& mapName) {
  YamlNode list = YamlNode::sequence();
  for (const ScriptWarp& warp : warps.warps) {
    list.append(warpToYaml(warp, lookup, mapName));
  }
  YamlNode node = YamlNode::mapping();
  node.set("warps", std::move(list));
  return node;
}

YamlNode newGameTriggersToYaml(const NewGameTriggers& triggers, const MapName& mapName) {
  YamlNode node = YamlNode::mapping();
  node.set("map", mapToYaml(mapName, triggers.map));
  node.set("script", yaml::number(triggers.script));
  node.set("triggers_set", yaml::numbers(triggers.set));
  if (!triggers.agreed) {
    node.set("agreed", yaml::boolean(false));
  }
  if (!triggers.unknown.empty()) {
    node.set("triggers_unknown", yaml::numbers(triggers.unknown));
  }
  if (triggers.opening) {
    const NewGameOpening& opening = *triggers.opening;
    YamlNode entry = YamlNode::mapping();
    entry.set("exit", yaml::number(opening.exit));
    entry.set("section", yaml::number(opening.section));
    entry.set("to", destinationToYaml(mapName, opening.screen, opening.screenExit));
    entry.set("sets", yaml::numbers(opening.sets));
    node.set("opening", std::move(entry));
  }
  return node;
}

} // namespace dwe::formats::script
