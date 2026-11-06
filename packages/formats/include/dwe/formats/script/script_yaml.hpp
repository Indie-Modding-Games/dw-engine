#pragma once

#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/disassembly.hpp>

#include <string>

namespace dwe::formats::script {

// The name of the label at an offset: "l_01a4".
std::string labelName(std::uint32_t offset);

// Converts a disassembly to a listing, one mapping per instruction in file order: `at` (the
// offset), `label` when something jumps there, `op` and the named operands. Jump operands and
// targets are label names; IF entries go in `conditions`, SWITCH cases in `cases`, SET_SELECTION
// options in `options`; text is the decoded string and DATA its bytes in hex. Pad bytes that are
// not 0 and IF entry codes that the fields do not imply are kept, so nothing of the code span is
// lost.
core::YamlNode scriptToYaml(const Disassembly& disassembly);

// One IF entry as the listing writes it in `conditions`: `test`, `clear` for a clear trigger,
// `compare`, `combine` when it is and/or, the operands, and `code` when the fields do not imply it.
core::YamlNode conditionToYaml(const Condition& condition);

} // namespace dwe::formats::script
