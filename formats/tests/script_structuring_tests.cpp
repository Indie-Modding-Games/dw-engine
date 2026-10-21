#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/control_flow.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/script_yaml.hpp>
#include <dwe/formats/script/structure_statistics.hpp>
#include <dwe/formats/script/structuring.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <ostream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "structure_parity.hpp"

using dwe::core::YamlNode;
using dwe::formats::script::buildScriptFlow;
using dwe::formats::script::ChoiceStatement;
using dwe::formats::script::FallbackReason;
using dwe::formats::script::FlowRole;
using dwe::formats::script::IfStatement;
using dwe::formats::script::labelName;
using dwe::formats::script::LoopStatement;
using dwe::formats::script::measureStructure;
using dwe::formats::script::readScriptListing;
using dwe::formats::script::RoutineKind;
using dwe::formats::script::ScriptListing;
using dwe::formats::script::ScriptReference;
using dwe::formats::script::Statement;
using dwe::formats::script::StatementList;
using dwe::formats::script::StructuredScript;
using dwe::formats::script::structureScript;
using dwe::formats::script::StructuringOptions;
using dwe::formats::script::TailCallStatement;
using dwe::formats::script::TerminatorKind;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

YamlNode text(std::string value) {
    return YamlNode::scalar(std::move(value));
}

YamlNode hexNumber(std::uint32_t value) {
    return text(std::format("0x{:04x}", value));
}

// Builds a listing the way scriptToYaml writes it, with only the operands that steer the flow.
class ListingBuilder {
public:
    ListingBuilder& section(std::uint16_t id, std::uint32_t at) {
        YamlNode node = YamlNode::mapping();
        node.set("id", hexNumber(id));
        node.set("at", text(labelName(at)));
        sections_.append(std::move(node));
        return *this;
    }

    ListingBuilder& op(std::uint32_t at, std::string name) {
        instructions_.append(instruction(at, std::move(name)));
        return *this;
    }

    ListingBuilder& jump(std::uint32_t at, std::string name, std::uint32_t target) {
        YamlNode node = instruction(at, std::move(name));
        node.set("target", text(labelName(target)));
        instructions_.append(std::move(node));
        return *this;
    }

    // An IF that tests a variable, then jumps on each jump entry: {jumps when true, target}.
    ListingBuilder& branch(std::uint32_t at, const std::vector<std::pair<bool, std::uint32_t>>& gotos) {
        YamlNode conditions = YamlNode::sequence();
        YamlNode test = YamlNode::mapping();
        test.set("test", text("pstat"));
        test.set("compare", text("eq"));
        test.set("pstat", text("5"));
        test.set("value", text("1"));
        conditions.append(std::move(test));
        for (const auto& [whenTrue, target] : gotos) {
            YamlNode entry = YamlNode::mapping();
            entry.set("test", text(whenTrue ? "goto_if_true" : "goto_if_false"));
            entry.set("target", text(labelName(target)));
            conditions.append(std::move(entry));
        }
        YamlNode node = instruction(at, "if");
        node.set("conditions", std::move(conditions));
        instructions_.append(std::move(node));
        return *this;
    }

    ListingBuilder& choice(std::uint32_t at, std::string name, const std::vector<std::uint32_t>& targets) {
        YamlNode labels = YamlNode::sequence();
        for (const std::uint32_t target : targets) {
            labels.append(text(labelName(target)));
        }
        const bool selection = name == "set_selection";
        YamlNode node = instruction(at, std::move(name));
        node.set(selection ? "options" : "cases", std::move(labels));
        instructions_.append(std::move(node));
        return *this;
    }

    ListingBuilder& fileJump(std::uint32_t at, std::string name, std::uint16_t script, std::uint16_t section) {
        YamlNode node = instruction(at, std::move(name));
        node.set("script", text(std::to_string(script)));
        node.set("section", text(std::to_string(section)));
        instructions_.append(std::move(node));
        return *this;
    }

    [[nodiscard]] YamlNode yaml() const {
        YamlNode listing = YamlNode::mapping();
        listing.set("sections", sections_);
        listing.set("instructions", instructions_);
        return listing;
    }

    [[nodiscard]] ScriptListing listing() const {
        auto read = readScriptListing(yaml());
        REQUIRE_MESSAGE(read.has_value(), errorOf(read));
        return std::move(*read);
    }

private:
    static YamlNode instruction(std::uint32_t at, std::string name) {
        YamlNode node = YamlNode::mapping();
        node.set("at", hexNumber(at));
        node.set("op", text(std::move(name)));
        return node;
    }

    YamlNode sections_ = YamlNode::sequence();
    YamlNode instructions_ = YamlNode::sequence();
};

StructuredScript structure(const ScriptListing& listing, const StructuringOptions& options = {}) {
    auto script = structureScript(listing, options);
    REQUIRE_MESSAGE(script.has_value(), errorOf(script));
    return std::move(*script);
}

// Every routine runs the instructions its graph runs, and leaves a statement only by a structured transfer (no jump).
void checkParity(const ScriptListing& listing, const StructuredScript& script) {
    const auto flow = buildScriptFlow(listing);
    REQUIRE(flow.has_value());
    CHECK(structure_parity::TransferCheck::goesNowhereElse(script));
    for (const auto& routine : script.routines) {
        CAPTURE(routine.instruction);
        CAPTURE(structure_parity::Outline::of(routine.body));
        CHECK(structure_parity::runsLikeTheGraph(listing, flow->graph, script, routine, structure_parity::ParitySeeds));
    }
}

// The block an instruction starts, which the test requires to exist.
dwe::formats::script::BlockId blockAt(const dwe::formats::script::ControlFlowGraph& graph, std::size_t instruction) {
    const auto block = graph.blockAt(instruction);
    REQUIRE(block.has_value());
    return block.value_or(0);
}

template <typename T>
const T& statementAt(const StatementList& list, std::size_t index) {
    REQUIRE(index < list.size());
    REQUIRE(std::holds_alternative<T>(list[index].node));
    return std::get<T>(list[index].node);
}

} // namespace

TEST_CASE("a listing reads back as its control flow") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .branch(0x00, {{false, 0x0c}, {true, 0x10}})
                                      .jump(0x08, "jump_and_link", 0x12)
                                      .op(0x0c, "set_trigger")
                                      .fileJump(0x0e, "jump_to_file_and_link", 203, 51)
                                      .fileJump(0x10, "jump_to_file", 7, 0xFE)
                                      .op(0x12, "jump_return")
                                      .listing();
    REQUIRE(listing.sections.size() == 1);
    CHECK(listing.sections[0].entry == 0x00);
    REQUIRE(listing.instructions.size() == 6);
    CHECK(listing.instructions[0].role == FlowRole::If);
    REQUIRE(listing.instructions[0].conditionalJumps.size() == 2);
    CHECK(listing.instructions[0].conditionalJumps[0].entry == 1);
    CHECK_FALSE(listing.instructions[0].conditionalJumps[0].whenTrue);
    CHECK(listing.instructions[0].conditionalJumps[1].target == 0x10);
    CHECK(listing.instructions[1].role == FlowRole::Call);
    CHECK(listing.instructions[1].targets == std::vector<std::uint32_t>{0x12});
    CHECK(listing.instructions[2].role == FlowRole::Plain);
    CHECK(listing.instructions[3].reference == ScriptReference{.script = 203, .section = 51});
    CHECK(listing.instructions[4].role == FlowRole::FileJump);
    CHECK(listing.instructions[5].role == FlowRole::Return);
    CHECK(listing.indexAt(0x0e) == 3);
    CHECK_FALSE(listing.indexAt(0x0d).has_value());
}

TEST_CASE("a listing whose jumps miss its instructions does not read") {
    CHECK_FALSE(readScriptListing(ListingBuilder{}.section(0xFE, 0x00).jump(0x00, "jump_to", 0x04).yaml()));
    CHECK_FALSE(readScriptListing(ListingBuilder{}.section(0xFE, 0x02).op(0x00, "end_section").yaml()));
    CHECK_FALSE(readScriptListing(YamlNode::scalar("not a listing")));
}

TEST_CASE("an IF with two gotos is a chain of two branches") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .op(0x00, "set_trigger")
                                      .branch(0x02, {{false, 0x0c}, {true, 0x0e}})
                                      .op(0x0a, "end_section")
                                      .op(0x0c, "end_section")
                                      .op(0x0e, "end_script")
                                      .listing();
    const auto flow = buildScriptFlow(listing);
    REQUIRE_MESSAGE(flow.has_value(), errorOf(flow));
    const auto& blocks = flow->graph.blocks;
    const auto& first = blocks[blockAt(flow->graph, 0)];
    CHECK(first.count == 1);
    CHECK(first.terminator.kind == TerminatorKind::Branch);
    CHECK(first.terminator.entry == 1);
    CHECK_FALSE(first.terminator.jumpsWhenTrue);
    CHECK(first.successors[0] == flow->graph.blockAt(3));
    const auto& chained = blocks[first.successors[1]];
    CHECK(chained.count == 0);
    CHECK(chained.terminator.entry == 2);
    CHECK(chained.successors == std::vector{blockAt(flow->graph, 4), blockAt(flow->graph, 2)});
    CHECK(blocks[blockAt(flow->graph, 2)].terminator.kind == TerminatorKind::Exit);
}

TEST_CASE("a switch goes to its cases and a selection also to the cancel after it") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .choice(0x00, "switch", {0x08, 0x0a})
                                      .choice(0x04, "set_selection", {0x08, 0x0a})
                                      .op(0x06, "end_section")
                                      .op(0x08, "end_section")
                                      .op(0x0a, "end_script")
                                      .section(0x51, 0x04)
                                      .listing();
    const auto flow = buildScriptFlow(listing);
    REQUIRE(flow.has_value());
    CHECK(flow->graph.blocks[blockAt(flow->graph, 0)].successors.size() == 2);
    CHECK(flow->graph.blocks[blockAt(flow->graph, 1)].successors.size() == 3);
}

TEST_CASE("flow that runs past the last instruction does not build") {
    const ScriptListing listing = ListingBuilder{}.section(0xFE, 0x00).op(0x00, "set_trigger").listing();
    CHECK_FALSE(buildScriptFlow(listing).has_value());
}

TEST_CASE("an if whose arms meet becomes if/else and the code after it") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .branch(0x00, {{false, 0x0c}})
                                      .op(0x08, "set_trigger")
                                      .jump(0x0a, "jump_to", 0x0e)
                                      .op(0x0c, "unset_trigger")
                                      .op(0x0e, "play_sound")
                                      .op(0x10, "end_section")
                                      .listing();
    const StructuredScript script = structure(listing);
    REQUIRE(script.routines.size() == 1);
    const StatementList& body = script.routines[0].body;
    REQUIRE(body.size() == 2);
    const auto& branch = statementAt<IfStatement>(body, 0);
    CHECK(branch.whenTrue.size() == 1);
    CHECK(branch.whenFalse.size() == 1);
    CHECK(std::get<dwe::formats::script::RunStatement>(body[1].node).count == 2);
    CHECK_FALSE(script.routines[0].fallback.has_value());
    checkParity(listing, script);
}

TEST_CASE("a wait loop becomes a loop that breaks to the code after it") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .op(0x00, "delay")
                                      .branch(0x02, {{false, 0x00}})
                                      .op(0x0a, "end_section")
                                      .listing();
    const StructuredScript script = structure(listing);
    const StatementList& body = script.routines[0].body;
    const auto& loop = statementAt<LoopStatement>(body, 0);
    REQUIRE(loop.body.size() == 2);
    const auto& exit = statementAt<IfStatement>(loop.body, 1);
    CHECK(std::holds_alternative<dwe::formats::script::BreakStatement>(exit.whenTrue.at(0).node));
    CHECK(exit.whenFalse.empty());
    checkParity(listing, script);
}

TEST_CASE("a loop with two exits breaks to where they meet") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .op(0x00, "delay")
                                      .branch(0x02, {{true, 0x0e}, {false, 0x00}})
                                      .op(0x0c, "play_sound")
                                      .op(0x0e, "end_section")
                                      .listing();
    const StructuredScript script = structure(listing);
    CHECK_FALSE(script.routines[0].fallback.has_value());
    CHECK(measureStructure(script).loops == 1);
    checkParity(listing, script);
}

TEST_CASE("code that two paths reach outside the join becomes one shared tail") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .branch(0x00, {{true, 0x10}})
                                      .op(0x08, "play_sound")
                                      .branch(0x0a, {{true, 0x18}})
                                      .op(0x10, "set_trigger")
                                      .op(0x12, "set_trigger")
                                      .op(0x14, "set_trigger")
                                      .op(0x16, "end_section")
                                      .op(0x18, "end_script")
                                      .listing();
    const StructuredScript script = structure(listing);
    const auto statistics = measureStructure(script);
    CHECK(statistics.sharedTails == 1);
    CHECK(statistics.tailCalls == 2);
    CHECK(statistics.stateMachines == 0);
    REQUIRE(script.routines.size() == 2);
    CHECK(script.routines[1].kind == RoutineKind::SharedTail);
    CHECK(script.routines[1].instruction == 3);
    checkParity(listing, script);
}

// The common code is a loop both sections enter: as a shared tail it is one loop with one way in.
TEST_CASE("code that several sections jump to becomes one shared tail they call") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .section(0x05, 0x08)
                                      .op(0x00, "play_sound")
                                      .jump(0x02, "jump_to", 0x14)
                                      .op(0x08, "set_trigger")
                                      .jump(0x0c, "jump_to", 0x14)
                                      .op(0x10, "end_section")
                                      .op(0x14, "delay")
                                      .op(0x18, "set_trigger")
                                      .branch(0x1c, {{true, 0x14}})
                                      .op(0x24, "end_section")
                                      .listing();
    const StructuredScript script = structure(listing);
    CAPTURE(structure_parity::Outline::of(script.routines.back().body));
    REQUIRE(script.routines.size() == 3);
    CHECK(script.routines[2].kind == RoutineKind::SharedTail);
    CHECK(script.routines[2].instruction == 5);
    for (std::size_t section = 0; section < 2; ++section) {
        CHECK(statementAt<TailCallStatement>(script.routines[section].body, 1).instruction == 5);
    }
    CHECK(std::holds_alternative<LoopStatement>(script.routines[2].body.at(0).node));
    const auto statistics = measureStructure(script);
    CHECK(statistics.emittedInstructions == statistics.distinctInstructions);
    checkParity(listing, script);
}

TEST_CASE("a short shared tail is copied instead") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .branch(0x00, {{true, 0x10}})
                                      .op(0x08, "play_sound")
                                      .branch(0x0a, {{true, 0x12}})
                                      .op(0x10, "end_section")
                                      .op(0x12, "end_script")
                                      .listing();
    const StructuredScript script = structure(listing);
    CHECK(measureStructure(script).sharedTails == 0);
    CHECK(measureStructure(script).emittedInstructions == 4);
    checkParity(listing, script);
}

TEST_CASE("a jump into another section's code calls that section") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .section(0x51, 0x04)
                                      .op(0x00, "play_sound")
                                      .jump(0x02, "jump_to", 0x04)
                                      .op(0x04, "set_trigger")
                                      .op(0x06, "end_section")
                                      .listing();
    const StructuredScript script = structure(listing);
    REQUIRE(script.routines.size() == 2);
    const auto& call = statementAt<TailCallStatement>(script.routines[0].body, 1);
    CHECK(call.instruction == 2);
    checkParity(listing, script);
}

TEST_CASE("a jump_and_link target is a subroutine of its own") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .jump(0x00, "jump_and_link", 0x06)
                                      .op(0x04, "end_section")
                                      .op(0x06, "play_sound")
                                      .op(0x08, "jump_return")
                                      .listing();
    const StructuredScript script = structure(listing);
    REQUIRE(script.routines.size() == 2);
    CHECK(script.routines[1].kind == RoutineKind::Subroutine);
    CHECK(script.routines[1].instruction == 2);
    checkParity(listing, script);
}

TEST_CASE("switch and selection arms follow their cases, the selection's last arm is the cancel") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .choice(0x00, "set_selection", {0x06, 0x0a})
                                      .jump(0x02, "jump_to", 0x0e)
                                      .op(0x06, "play_sound")
                                      .jump(0x08, "jump_to", 0x0e)
                                      .op(0x0a, "set_trigger")
                                      .jump(0x0c, "jump_to", 0x0e)
                                      .choice(0x0e, "switch", {0x12, 0x14})
                                      .op(0x12, "end_section")
                                      .op(0x14, "end_script")
                                      .listing();
    const StructuredScript script = structure(listing);
    const StatementList& body = script.routines[0].body;
    CHECK(statementAt<ChoiceStatement>(body, 0).arms.size() == 3);
    CHECK(statementAt<ChoiceStatement>(body, 1).arms.size() == 2);
    checkParity(listing, script);
}

TEST_CASE("a loop with two ways in becomes a state machine that runs the same") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .branch(0x00, {{true, 0x0a}})
                                      .op(0x08, "play_sound")
                                      .op(0x0a, "set_trigger")
                                      .branch(0x0c, {{true, 0x08}})
                                      .op(0x14, "end_section")
                                      .listing();
    const StructuredScript script = structure(listing);
    CHECK(script.routines[0].fallback == FallbackReason::Irreducible);
    CHECK(std::holds_alternative<dwe::formats::script::StateMachineStatement>(script.routines[0].body.at(0).node));
    checkParity(listing, script);
}

TEST_CASE("two loops in a row structure without a state machine") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .op(0x00, "play_sound")
                                      .branch(0x02, {{true, 0x00}})
                                      .op(0x0a, "delay")
                                      .branch(0x0e, {{true, 0x0a}})
                                      .op(0x16, "end_section")
                                      .listing();
    const StructuredScript script = structure(listing);
    CAPTURE(structure_parity::Outline::of(script.routines[0].body));
    CHECK_FALSE(script.routines[0].fallback.has_value());
    const StatementList& body = script.routines[0].body;
    statementAt<LoopStatement>(body, 0);
    statementAt<LoopStatement>(body, 1);
    checkParity(listing, script);
}

TEST_CASE("a loop with two latches is one loop") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .op(0x00, "play_sound")
                                      .branch(0x02, {{true, 0x00}})
                                      .op(0x0a, "delay")
                                      .branch(0x0e, {{true, 0x00}})
                                      .op(0x16, "end_section")
                                      .listing();
    const StructuredScript script = structure(listing);
    CAPTURE(structure_parity::Outline::of(script.routines[0].body));
    CHECK_FALSE(script.routines[0].fallback.has_value());
    CHECK(measureStructure(script).loops == 1);
    checkParity(listing, script);
}

TEST_CASE("an exit from the middle of a loop breaks to the code after it") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .op(0x00, "play_sound")
                                      .branch(0x02, {{true, 0x14}})
                                      .op(0x0a, "delay")
                                      .jump(0x0e, "jump_to", 0x00)
                                      .op(0x14, "set_trigger")
                                      .op(0x18, "end_section")
                                      .listing();
    const StructuredScript script = structure(listing);
    CAPTURE(structure_parity::Outline::of(script.routines[0].body));
    CHECK_FALSE(script.routines[0].fallback.has_value());
    const auto statistics = measureStructure(script);
    CHECK(statistics.loops == 1);
    CHECK(statistics.breaks == 1);
    statementAt<LoopStatement>(script.routines[0].body, 0);
    CHECK(statementAt<dwe::formats::script::RunStatement>(script.routines[0].body, 1).first == 4);
    checkParity(listing, script);
}

TEST_CASE("a switch inside a switch arm nests, and both meet after the outer one") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .choice(0x00, "switch", {0x08, 0x18})
                                      .choice(0x08, "switch", {0x10, 0x14})
                                      .op(0x10, "play_sound")
                                      .jump(0x12, "jump_to", 0x18)
                                      .op(0x14, "delay")
                                      .op(0x18, "set_trigger")
                                      .op(0x1c, "end_section")
                                      .listing();
    const StructuredScript script = structure(listing);
    CAPTURE(structure_parity::Outline::of(script.routines[0].body));
    CHECK_FALSE(script.routines[0].fallback.has_value());
    const auto& outer = statementAt<ChoiceStatement>(script.routines[0].body, 0);
    REQUIRE(outer.arms.size() == 2);
    CHECK(statementAt<ChoiceStatement>(outer.arms[0], 0).arms.size() == 2);
    CHECK(outer.arms[1].empty());
    CHECK(statementAt<dwe::formats::script::RunStatement>(script.routines[0].body, 1).first == 5);
    checkParity(listing, script);
}

TEST_CASE("an inner loop that leaves to the head of the outer one breaks to it") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .op(0x00, "play_sound")
                                      .op(0x02, "delay")
                                      .branch(0x04, {{true, 0x00}})
                                      .branch(0x0c, {{true, 0x02}})
                                      .branch(0x14, {{true, 0x00}})
                                      .op(0x1c, "end_section")
                                      .listing();
    const StructuredScript script = structure(listing);
    CAPTURE(structure_parity::Outline::of(script.routines[0].body));
    CHECK_FALSE(script.routines[0].fallback.has_value());
    const auto& outer = statementAt<LoopStatement>(script.routines[0].body, 0);
    CHECK(std::holds_alternative<LoopStatement>(outer.body.at(1).node));
    checkParity(listing, script);
}

// The inner loop leaves to two places of the outer one that only meet again at the outer head: one of them is its
// follow, and the other would have to start the outer loop over from inside the inner one.
TEST_CASE("an inner loop that leaves to two places of the outer one becomes a state machine") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .op(0x00, "play_sound")
                                      .op(0x02, "delay")
                                      .branch(0x04, {{true, 0x20}})
                                      .branch(0x0c, {{true, 0x28}})
                                      .jump(0x14, "jump_to", 0x02)
                                      .op(0x1c, "end_section")
                                      .op(0x20, "play_sound")
                                      .jump(0x22, "jump_to", 0x00)
                                      .op(0x28, "set_trigger")
                                      .jump(0x2a, "jump_to", 0x00)
                                      .listing();
    const StructuredScript script = structure(listing);
    CAPTURE(structure_parity::Outline::of(script.routines[0].body));
    CHECK(script.routines[0].fallback == FallbackReason::NestedExit);
    checkParity(listing, script);
}

TEST_CASE("too many copies of shared code make a state machine") {
    const ScriptListing listing = ListingBuilder{}
                                      .section(0xFE, 0x00)
                                      .branch(0x00, {{true, 0x10}})
                                      .op(0x08, "play_sound")
                                      .branch(0x0a, {{true, 0x18}})
                                      .op(0x10, "set_trigger")
                                      .op(0x12, "set_trigger")
                                      .op(0x14, "set_trigger")
                                      .op(0x16, "end_section")
                                      .op(0x18, "end_script")
                                      .listing();
    const StructuredScript script = structure(listing, {.sharedTailMinimum = 7, .duplicationLimit = 1});
    CHECK(script.routines[0].fallback == FallbackReason::Duplication);
    checkParity(listing, script);
}

// Random listings: plain ops, jumps, IFs with one or two gotos, switches, selections and ends, jumping anywhere.
// Whatever the shape, every routine structures and runs like its graph.
TEST_CASE("every routine of a random listing structures and runs like its graph") {
    std::uint32_t state = 12345;
    const auto next = [&](std::uint32_t bound) {
        state = (state * 1664525U) + 1013904223U;
        return (state >> 8U) % bound;
    };
    for (int round = 0; round < 400; ++round) {
        CAPTURE(round);
        const std::uint32_t count = 4 + next(14);
        const auto offsetOf = [](std::uint32_t index) { return index * 8; };
        ListingBuilder builder;
        builder.section(0xFE, 0);
        if (count > 6) {
            builder.section(0x51, offsetOf(next(count)));
        }
        for (std::uint32_t index = 0; index + 1 < count; ++index) {
            switch (next(7)) {
            case 0:
                builder.jump(offsetOf(index), "jump_to", offsetOf(next(count)));
                break;
            case 1:
                builder.branch(offsetOf(index), {{next(2) == 0, offsetOf(next(count))}});
                break;
            case 2:
                builder.branch(offsetOf(index),
                               {{next(2) == 0, offsetOf(next(count))}, {next(2) == 0, offsetOf(next(count))}});
                break;
            case 3:
                builder.op(offsetOf(index), next(3) == 0 ? "end_section" : "delay");
                break;
            case 4:
                builder.choice(offsetOf(index), next(2) == 0 ? "switch" : "set_selection",
                               {offsetOf(next(count)), offsetOf(next(count))});
                break;
            default:
                builder.op(offsetOf(index), "play_sound");
                break;
            }
        }
        builder.op(offsetOf(count - 1), "end_script");
        const ScriptListing listing = builder.listing();
        const StructuredScript script = structure(listing);
        for (const auto& routine : script.routines) {
            CHECK_FALSE(routine.body.empty());
        }
        checkParity(listing, script);
    }
}
