#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

// The error and note messages of the newer formats readers, as std::format templates for
// core::formatMessage. Older readers still write theirs inline; they move here when they are next
// changed.
namespace dwe::formats::messages {

using core::formatMessage;

// Constant switches (exe/constant_switch.cpp).
inline constexpr std::string_view SwitchNotLocated =
    "the constant switch is not located for this version";
inline constexpr std::string_view UnsupportedInstruction =
    "instruction {:08x} at {:08x} is not one that only sets values";
inline constexpr std::string_view UnknownBranch =
    "the branch at {:08x} depends on a value that is not known";
inline constexpr std::string_view UnknownAddress =
    "the memory access at {:08x} uses an address that is not known";
inline constexpr std::string_view BranchInDelaySlot = "the delay slot at {:08x} holds a branch";
inline constexpr std::string_view NoJoin = "the cases did not join after {} instructions";
inline constexpr std::string_view UnknownLocal = "local {} is not known where the cases join";
inline constexpr std::string_view SwitchArgument = "argument {}: {}";

// Item effects (exe/item_effects.cpp).
inline constexpr std::string_view TableNotLocated = "{} is not located for this version";
inline constexpr std::string_view InTable = "{}: {}";
inline constexpr std::string_view UnknownItemHandler =
    "ITEM_FUNCTIONS[{}] points to {:08x}, which is no known handler";
inline constexpr std::string_view ItemSwitchSkipped = "item {} ({}): {}";

// Script listings read back from YAML (script/script_listing.cpp).
inline constexpr std::string_view ListingNotMapping =
    "the listing is not a mapping with `sections` and `instructions`";
inline constexpr std::string_view ListingMissingKey = "instruction {}: `{}` is missing";
inline constexpr std::string_view ListingNotString = "instruction {}: `{}` must be a string";
inline constexpr std::string_view ListingBadNumber = "instruction {}: `{}` is not a number: {}";
inline constexpr std::string_view ListingBadLabel = "`{}` is not a label (l_<hex>)";
inline constexpr std::string_view ListingUnknownTest = "instruction {}: `{}` is not an IF test";
inline constexpr std::string_view ListingBadWord =
    "instruction {}: `{}` is not one of the words it takes: {}";
inline constexpr std::string_view ListingOutOfOrder =
    "the instruction at {:#06x} does not come after the one before";
inline constexpr std::string_view ListingNoInstruction =
    "{} {:#06x} is not the start of an instruction";

// Control-flow graphs (script/control_flow.cpp).
inline constexpr std::string_view FlowRunsOffEnd =
    "the instruction at {:#06x} runs past the end of the code";

} // namespace dwe::formats::messages
