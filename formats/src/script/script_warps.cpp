#include <dwe/core/collision_triggers.hpp>
#include <dwe/formats/script/script_warps.hpp>
#include <dwe/formats/script/script_yaml.hpp>

#include <yaml/yaml_values.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace dwe::formats::script {

namespace {

constexpr std::string_view ScriptOperand = "script";
constexpr std::string_view OffsetKey = "offset";
constexpr std::string_view RecursionKey = "recursion";
constexpr std::string_view ForgetKey = "forget";
constexpr std::string_view TriggersKey = "triggers";
constexpr std::array<std::string_view, 4> ViaNames = {"step", "confirm", "enter", "other"};
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
    if (!code || code->listing == nullptr || place.instruction >= code->listing->instructions.size()) {
        return nullptr;
    }
    return &code->listing->instructions[place.instruction];
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
    return Load{.script = static_cast<std::uint16_t>(*script), .map = static_cast<std::uint16_t>(*map)};
}

// The map changes of every section of a script the game can run, by section, walked once per script.
class SectionChanges {
public:
    explicit SectionChanges(const ScriptLookup& lookup) : lookup_(&lookup) {}

    const std::vector<std::pair<std::uint16_t, std::vector<MapChange>>>& of(std::uint16_t script) {
        const auto cached = cache_.find(script);
        if (cached != cache_.end()) {
            return cached->second;
        }
        auto& sections = cache_[script];
        const auto code = (*lookup_)(script);
        if (code && code->listing != nullptr) {
            for (const ListingSection& section : code->listing->runnableSections()) {
                sections.emplace_back(section.id, findMapChanges(script, section.id, *lookup_));
            }
        }
        return sections;
    }

private:
    const ScriptLookup* lookup_;
    std::map<std::uint16_t, std::vector<std::pair<std::uint16_t, std::vector<MapChange>>>> cache_;
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
            if (const ListingInstruction* instruction = instructionAt(lookup, forget->place)) {
                site.set(std::string{OffsetKey}, yaml::number(instruction->offset));
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
            if (const ListingInstruction* instruction = instructionAt(lookup, recursion.routine)) {
                routine.set(std::string{OffsetKey}, yaml::number(instruction->offset));
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

YamlNode warpToYaml(const ScriptWarp& warp, const ScriptLookup& lookup, const MapName& mapName) {
    YamlNode node = YamlNode::mapping();
    node.set("map", mapToYaml(mapName, warp.map));
    node.set("via", YamlNode::scalar(std::string{ViaNames.at(static_cast<std::size_t>(warp.via))}));
    node.set("section", yaml::number(warp.section));
    const MapChange& change = warp.change;
    if (change.screen) {
        YamlNode to = YamlNode::mapping();
        const auto screen = static_cast<std::uint16_t>(*change.screen);
        if (const auto name = mapName(screen)) {
            to.set("map", YamlNode::scalar(*name));
        } else {
            to.set("screen", yaml::number(screen));
        }
        to.set("exit", yaml::number(change.exit.value_or(0)));
        node.set("to", std::move(to));
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
    if (const ListingInstruction* instruction = instructionAt(lookup, change.path.place)) {
        source.set(std::string{OffsetKey}, yaml::number(instruction->offset));
    }
    node.set("source", std::move(source));
    return node;
}

} // namespace

WarpVia warpViaOf(std::uint16_t section) {
    if (section == EnterSection) {
        return WarpVia::Enter;
    }
    if (section >= core::collision::FirstStepTrigger && section < core::collision::FirstConfirmTrigger) {
        return WarpVia::Step;
    }
    if (section >= core::collision::FirstConfirmTrigger && section < core::collision::FirstWarpTrigger) {
        return WarpVia::Confirm;
    }
    return WarpVia::Other;
}

ScriptWarps findScriptWarps(const ScriptLookup& lookup) {
    ScriptWarps result;
    const auto mapHead = lookup(MapHeadScript);
    if (!mapHead || mapHead->listing == nullptr) {
        return result;
    }
    SectionChanges changes{lookup};
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
                                    .change = std::move(change)});
        }
        for (const GuardedInstruction& found : loads) {
            const auto load = loadOf(lookup, found);
            if (!load) {
                continue;
            }
            for (const auto& [scriptSection, sectionChanges] : changes.of(load->script)) {
                for (const MapChange& change : sectionChanges) {
                    result.warps.push_back({.map = load->map,
                                            .via = warpViaOf(scriptSection),
                                            .section = scriptSection,
                                            .script = load->script,
                                            .scriptGuard = found.guard,
                                            .change = change});
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
            result =
                NewGameTriggers{.map = load->map, .script = load->script, .set = {}, .agreed = true, .unknown = {}};
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
    result->unknown.assign(StartingPartnerRaisedTriggers.begin(), StartingPartnerRaisedTriggers.end());
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
    std::erase_if(result->set,
                  [&](std::uint16_t trigger) { return std::ranges::binary_search(result->unknown, trigger); });
    return result;
}

YamlNode scriptWarpsToYaml(const ScriptWarps& warps, const ScriptLookup& lookup, const MapName& mapName) {
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
    return node;
}

} // namespace dwe::formats::script
