#include <dwe/assets/new_game_story.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/collision_triggers.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/field/script_warps.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::game::field {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view ScriptWarpsId = "field/script_warps";
constexpr std::string_view Context = "script warps";
constexpr std::string_view TriggerTest = "trigger";
constexpr std::array<std::string_view, 4> ViaNames = {"step", "confirm", "enter", "other"};

Truth both(Truth a, Truth b) {
    if (a == Truth::False || b == Truth::False) {
        return Truth::False;
    }
    return a == Truth::True && b == Truth::True ? Truth::True : Truth::Unknown;
}

Truth either(Truth a, Truth b) {
    if (a == Truth::True || b == Truth::True) {
        return Truth::True;
    }
    return a == Truth::False && b == Truth::False ? Truth::False : Truth::Unknown;
}

Truth testValue(const GuardTest& test, const TriggerState& triggers) {
    if (test.test != TriggerTest || !triggers) {
        return Truth::Unknown;
    }
    const std::optional<bool> set = triggers(test.trigger);
    if (!set) {
        return Truth::Unknown;
    }
    return *set != test.clear ? Truth::True : Truth::False;
}

// What the path made it forget before a term: every trigger, after a recursion or game code that may write any, or
// the triggers listed, each with what may have written it ("the recursion at script 63").
struct Forgotten {
    bool all = false;
    std::string allSite;
    std::vector<std::pair<std::int32_t, std::string>> triggers;

    // The site of the code that may have written the trigger; nothing if none did.
    [[nodiscard]] std::optional<std::string> siteOf(std::int32_t trigger) const {
        if (all) {
            return allSite;
        }
        const auto found = std::ranges::find(triggers, trigger, &std::pair<std::int32_t, std::string>::first);
        return found == triggers.end() ? std::nullopt : std::optional{found->second};
    }

    void forget(const GuardForget& term) {
        if (all) {
            return;
        }
        const std::string site = core::formatMessage(messages::GameCodeSite, term.site);
        if (term.triggers.empty()) {
            all = true;
            allSite = site;
            return;
        }
        for (const std::int32_t trigger : term.triggers) {
            if (!siteOf(trigger)) {
                triggers.emplace_back(trigger, site);
            }
        }
    }
};

// An IF's decision. Under Allow, one whose tests are all trigger tests and read a trigger the state does not know
// holds whichever way it goes, and says so in `allowed`.
Truth branchValue(const GuardBranch& branch, const TriggerState& known, const Forgotten& forgotten,
                  UnknownPolicy policy, std::vector<std::string>& allowed) {
    const TriggerState triggers = known ? TriggerState{[&](std::int32_t trigger) -> std::optional<bool> {
        return forgotten.siteOf(trigger) ? std::nullopt : known(trigger);
    }}
                                        : known;
    Truth result = Truth::False;
    for (const GuardTest& test : branch.tests) {
        const Truth value = testValue(test, triggers);
        switch (test.combine) {
        case TestCombine::Replace:
            result = value;
            break;
        case TestCombine::And:
            result = both(result, value);
            break;
        case TestCombine::Or:
            result = either(result, value);
            break;
        }
    }
    if (result == Truth::Unknown) {
        const bool onlyTriggers = triggers && std::ranges::all_of(branch.tests, [](const GuardTest& test) {
                                      return test.test == TriggerTest;
                                  });
        if (policy != UnknownPolicy::Allow || !onlyTriggers) {
            return Truth::Unknown;
        }
        for (const GuardTest& test : branch.tests) {
            if (triggers(test.trigger)) {
                continue;
            }
            if (const auto site = forgotten.siteOf(test.trigger)) {
                allowed.push_back(core::formatMessage(messages::ForgottenTriggerAllowed, test.trigger, *site));
            } else {
                allowed.push_back(core::formatMessage(messages::UnknownTriggerAllowed, test.trigger));
            }
        }
        return Truth::True;
    }
    return (result == Truth::True) == branch.holds ? Truth::True : Truth::False;
}

// A guard's decision. `forgotten` is what the game code before it on the path made it forget (in the script guard,
// for the section's guard): the trigger tests do not read those triggers from the state.
Truth guardValue(const WarpGuard& guard, const TriggerState& triggers, UnknownPolicy policy,
                 std::vector<std::string>& allowed, Forgotten& forgotten) {
    Truth result = Truth::True;
    for (const auto& term : guard.terms) {
        Truth value = Truth::Unknown;
        if (const auto* branch = std::get_if<GuardBranch>(&term)) {
            value = branchValue(*branch, triggers, forgotten, policy, allowed);
        } else if (const auto* recursion = std::get_if<GuardRecursion>(&term)) {
            if (!forgotten.all) {
                forgotten = Forgotten{.all = true,
                                      .allSite = core::formatMessage(messages::RecursionSite, recursion->site),
                                      .triggers = {}};
            }
            if (policy == UnknownPolicy::Allow) {
                allowed.push_back(core::formatMessage(messages::RecursionAllowed, recursion->site));
                value = Truth::True;
            }
        } else if (const auto* forget = std::get_if<GuardForget>(&term)) {
            forgotten.forget(*forget);
            value = Truth::True;
        }
        result = both(result, value);
    }
    if (guard.unstructured && result == Truth::True) {
        return Truth::Unknown;
    }
    return result;
}

// "script 206, offset 0x069e", from a term's script and offset.
std::string siteOf(YamlReader& reader, const YamlNode& node) {
    std::string site = std::format("script {}", reader.integer<std::int32_t>(reader.field(node, "script")));
    if (const auto offset = reader.optionalInteger<std::int32_t>(node, "offset")) {
        site += std::format(", offset 0x{:04x}", *offset);
    }
    return site;
}

GuardTest readTest(YamlReader& reader, const YamlNode& node) {
    GuardTest test{
        .test = reader.text(reader.field(node, "test")), .combine = TestCombine::Replace, .trigger = 0, .clear = false};
    if (const YamlNode* combine = node.isMapping() ? node.find("combine") : nullptr; combine != nullptr) {
        const std::string word = reader.text(*combine);
        if (word == "and") {
            test.combine = TestCombine::And;
        } else if (word == "or") {
            test.combine = TestCombine::Or;
        } else {
            reader.fail(*combine, "combine is and or or");
        }
    }
    if (test.test == TriggerTest &&
        reader.expectKeys(node, "a trigger test", {"test", "trigger"}, {"clear", "combine", "code"})) {
        test.trigger = reader.integer<std::int32_t>(reader.field(node, "trigger"));
        if (const YamlNode* clear = node.find("clear"); clear != nullptr) {
            test.clear = reader.boolean(*clear);
        }
    }
    return test;
}

WarpGuard readGuard(YamlReader& reader, const YamlNode* node) {
    WarpGuard guard;
    if (node == nullptr || !reader.expectKeys(*node, "a guard", {"terms"}, {"unstructured"})) {
        return guard;
    }
    if (const YamlNode* unstructured = node->find("unstructured"); unstructured != nullptr) {
        guard.unstructured = reader.boolean(*unstructured);
    }
    for (const YamlNode& term : reader.sequence(reader.field(*node, "terms"), "terms")) {
        if (!term.isMapping()) {
            reader.fail(term, "a term is a mapping");
            break;
        }
        if (const YamlNode* recursion = term.find("recursion"); recursion != nullptr) {
            if (!reader.expectKeys(term, "a recursion term", {"recursion"}) ||
                !reader.expectKeys(*recursion, "a recursion", {"script"}, {"offset"})) {
                break;
            }
            guard.terms.emplace_back(GuardRecursion{.site = siteOf(reader, *recursion)});
            continue;
        }
        if (const YamlNode* forget = term.find("forget"); forget != nullptr) {
            if (!reader.expectKeys(term, "a forget term", {"forget"}) ||
                !reader.expectKeys(*forget, "a forget", {"script"}, {"offset", "triggers"})) {
                break;
            }
            GuardForget written{.site = siteOf(reader, *forget), .triggers = {}};
            if (const YamlNode* triggers = forget->find("triggers"); triggers != nullptr) {
                for (const YamlNode& trigger : reader.sequence(*triggers, "triggers")) {
                    written.triggers.push_back(reader.integer<std::int32_t>(trigger));
                }
            }
            guard.terms.emplace_back(std::move(written));
            continue;
        }
        if (term.find("if") != nullptr) {
            if (!reader.expectKeys(term, "an IF term", {"if", "holds"})) {
                break;
            }
            GuardBranch branch{.tests = {}, .holds = reader.boolean(reader.field(term, "holds"))};
            for (const YamlNode& test : reader.sequence(reader.field(term, "if"), "if")) {
                branch.tests.push_back(readTest(reader, test));
            }
            guard.terms.emplace_back(std::move(branch));
            continue;
        }
        const bool selection = term.find("selection") != nullptr;
        if (!reader.expectKeys(term, "a choice term", {selection ? "selection" : "switch"})) {
            break;
        }
        guard.terms.emplace_back(
            GuardChoice{.arm = reader.integer<std::int32_t>(reader.field(term, selection ? "selection" : "switch")),
                        .selection = selection});
    }
    return guard;
}

ScriptWarp readWarp(YamlReader& reader, const YamlNode& item) {
    ScriptWarp warp;
    if (!reader.expectKeys(item, "a script warp", {"map", "via", "section", "source"},
                           {"to", "teleport", "arrival_section", "script_guard", "guard", "sets_flags"})) {
        return warp;
    }
    warp.map = reader.text(reader.field(item, "map"));
    const std::string via = reader.text(reader.field(item, "via"));
    const auto named = std::ranges::find(ViaNames, via);
    if (named == ViaNames.end()) {
        reader.fail(reader.field(item, "via"), "via is step, confirm, enter or other");
    } else {
        warp.via = static_cast<WarpVia>(named - ViaNames.begin());
    }
    warp.section = reader.integer<std::int32_t>(reader.field(item, "section"));
    if (const YamlNode* to = item.find("to");
        to != nullptr && reader.expectKeys(*to, "to", {"exit"}, {"map", "screen"})) {
        if (const YamlNode* map = to->find("map"); map != nullptr) {
            warp.targetMap = reader.text(*map);
        }
        warp.targetExit = reader.integer<std::int32_t>(reader.field(*to, "exit"));
    }
    warp.scriptGuard = readGuard(reader, item.find("script_guard"));
    warp.guard = readGuard(reader, item.find("guard"));
    if (const YamlNode* effects = item.find("sets_flags"); effects != nullptr) {
        for (const YamlNode& effect : reader.sequence(*effects, "sets_flags")) {
            if (reader.expectKeys(effect, "a flag effect", {"trigger", "set"})) {
                warp.setsFlags.push_back({.trigger = reader.integer<std::int32_t>(reader.field(effect, "trigger")),
                                          .set = reader.boolean(reader.field(effect, "set"))});
            }
        }
    }
    const YamlNode& source = reader.field(item, "source");
    if (reader.expectKeys(source, "the source", {"script", "section"}, {"offset"})) {
        warp.source = std::format("script {}, section {}", reader.integer<std::int32_t>(reader.field(source, "script")),
                                  reader.integer<std::int32_t>(reader.field(source, "section")));
        if (const auto offset = reader.optionalInteger<std::int32_t>(source, "offset")) {
            warp.source += std::format(", offset 0x{:04x}", *offset);
        }
    }
    return warp;
}

} // namespace

WarpVia warpViaOf(std::int32_t value) {
    using core::collision::FirstConfirmTrigger;
    using core::collision::FirstStepTrigger;
    using core::collision::FirstWarpTrigger;
    if (value >= FirstStepTrigger && value < FirstConfirmTrigger) {
        return WarpVia::Step;
    }
    return value >= FirstConfirmTrigger && value < FirstWarpTrigger ? WarpVia::Confirm : WarpVia::Other;
}

Truth evaluate(const WarpGuard& guard, const TriggerState& triggers) {
    std::vector<std::string> allowed;
    Forgotten forgotten;
    return guardValue(guard, triggers, UnknownPolicy::Deny, allowed, forgotten);
}

Truth evaluate(const ScriptWarp& warp, const TriggerState& triggers) {
    return decide(warp, triggers, UnknownPolicy::Deny).truth;
}

WarpDecision decide(const ScriptWarp& warp, const TriggerState& triggers, UnknownPolicy policy) {
    WarpDecision decision;
    // The script guard's path runs before the section's (MAPHEAD loads the script), so what the game code there may
    // write the section's trigger tests do not read either.
    Forgotten forgotten;
    const Truth scriptGuard = guardValue(warp.scriptGuard, triggers, policy, decision.allowed, forgotten);
    const Truth taken = both(scriptGuard, guardValue(warp.guard, triggers, policy, decision.allowed, forgotten));
    decision.truth = taken == Truth::True && !warp.targetMap ? Truth::Unknown : taken;
    if (decision.truth != Truth::True) {
        decision.allowed.clear();
    }
    std::vector<std::string> once;
    for (std::string& reason : decision.allowed) {
        if (!std::ranges::contains(once, reason)) {
            once.push_back(std::move(reason));
        }
    }
    decision.allowed = std::move(once);
    return decision;
}

TriggerState newGameTriggers(const assets::NewGameStory& story, std::string_view startMap, std::int32_t startExit) {
    std::vector<std::int32_t> set = story.triggersSet;
    if (story.opening && (startMap != story.map || startExit != story.opening->exit)) {
        set.insert(set.end(), story.opening->sets.begin(), story.opening->sets.end());
        std::ranges::sort(set);
        const auto repeated = std::ranges::unique(set);
        set.erase(repeated.begin(), repeated.end());
    }
    return [set = std::move(set), unknown = story.triggersUnknown,
            agreed = story.agreed](std::int32_t trigger) -> std::optional<bool> {
        if (std::ranges::binary_search(unknown, trigger)) {
            return std::nullopt;
        }
        if (std::ranges::binary_search(set, trigger)) {
            return true;
        }
        return agreed ? std::optional{false} : std::nullopt;
    };
}

CellWarp warpOnCell(std::span<const ScriptWarp> warps, std::string_view map, WarpVia via, std::int32_t section,
                    const TriggerState& triggers, UnknownPolicy policy) {
    CellWarp cell;
    std::vector<std::pair<const ScriptWarp*, WarpDecision>> taken;
    for (const ScriptWarp& warp : warps) {
        if (warp.map != map || warp.via != via || warp.section != section) {
            continue;
        }
        WarpDecision decision = decide(warp, triggers, policy);
        if (decision.truth == Truth::True) {
            taken.emplace_back(&warp, std::move(decision));
        } else if (taken.empty()) {
            cell.undecided = cell.undecided || decision.truth == Truth::Unknown;
        }
    }
    if (taken.empty()) {
        return cell;
    }
    // A True decision that allowed nothing needs no Unknown: the original takes that path whatever it does not know.
    const auto decided = [](const auto& entry) { return entry.second.allowed.empty(); };
    const bool oneDecided = std::ranges::count_if(taken, decided) == 1;
    const auto chosen = oneDecided ? std::ranges::find_if(taken, decided) : taken.begin();
    const auto placeOf = [](const auto& entry) { return std::pair{entry.first->targetMap, entry.first->targetExit}; };
    const bool places =
        std::ranges::any_of(taken, [&](const auto& entry) { return placeOf(entry) != placeOf(*chosen); });
    if (places && !oneDecided) {
        std::ranges::transform(taken, std::back_inserter(cell.ambiguous),
                               [](const auto& entry) { return entry.first; });
    }
    cell.warp = chosen->first;
    cell.decision = std::move(chosen->second);
    return cell;
}

core::Result<std::vector<ScriptWarp>> parseScriptWarps(std::string_view yaml) {
    auto tree = core::parseYaml(yaml);
    if (!tree) {
        return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
    }
    YamlReader reader;
    if (!reader.expectKeys(*tree, "the script warps", {"warps"})) {
        return std::unexpected{reader.error(Context)};
    }
    std::vector<ScriptWarp> warps;
    for (const YamlNode& item : reader.sequence(reader.field(*tree, "warps"), "warps")) {
        warps.push_back(readWarp(reader, item));
        if (!reader.ok()) {
            break;
        }
    }
    return reader.finish(std::move(warps), Context);
}

core::Result<std::vector<ScriptWarp>> loadScriptWarps(const assets::VirtualFileSystem& vfs) {
    auto id = assets::AssetId::parse(ScriptWarpsId);
    if (!id) {
        return std::unexpected{id.error()};
    }
    if (vfs.resolve(*id) == nullptr) {
        return std::vector<ScriptWarp>{};
    }
    auto bytes = vfs.read(*id);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    return parseScriptWarps(std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

} // namespace dwe::game::field
