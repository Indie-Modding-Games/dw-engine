#include <dwe/formats/script/map_changes.hpp>

#include <string_view>
#include <utility>

namespace dwe::formats::script {

namespace {

constexpr std::string_view WarpToOp = "warp_to";
constexpr std::string_view ScreenOperand = "screen";
constexpr std::string_view ExitOperand = "exit";
constexpr std::string_view SectionOperand = "section";
constexpr std::string_view VariableOperand = "pstat";

} // namespace

std::vector<MapChange> findMapChanges(std::uint16_t script, std::uint16_t section, const ScriptLookup& lookup,
                                      const WalkOptions& options) {
    const InstructionFilter isMapChange = [](const ListingInstruction& instruction) {
        return instruction.role == FlowRole::MapChange;
    };
    std::vector<MapChange> changes;
    for (GuardedInstruction& path : walkSection(script, section, lookup, isMapChange, options)) {
        // The walk reports only instructions of scripts the lookup found.
        const auto code = lookup(path.place.script);
        if (!code) {
            continue;
        }
        const ListingInstruction& instruction = code->listing->instructions[path.place.instruction];
        MapChange change{.path = std::move(path),
                         .screen = std::nullopt,
                         .exit = std::nullopt,
                         .arrivalSection = std::nullopt,
                         .screenVariable = std::nullopt};
        if (instruction.op == WarpToOp) {
            change.screen = instruction.argument(ScreenOperand);
            change.exit = instruction.argument(ExitOperand);
            change.arrivalSection = instruction.argument(SectionOperand);
        } else {
            change.screenVariable = instruction.argument(VariableOperand);
        }
        changes.push_back(std::move(change));
    }
    return changes;
}

} // namespace dwe::formats::script
