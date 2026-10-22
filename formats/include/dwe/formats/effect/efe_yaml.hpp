#pragma once

#include <dwe/core/yaml.hpp>
#include <dwe/formats/effect/efe_analysis.hpp>
#include <dwe/formats/effect/efe_file.hpp>
#include <dwe/formats/effect/efe_script.hpp>

#include <span>
#include <string>

// The YAML documents of an effect slot.
namespace dwe::formats::effect {

// Names for the numbers a slot's data refers to: the Digimon types of its bone offset tables.
// Without them the numbers stay.
struct EfeYamlIds {
  std::span<const std::string> digimon;
};

// The header's fields, offsets as "0x...." from the slot's base.
core::YamlNode efeHeaderToYaml(const EfeHeader& header);

// The scripts and the data area of a slot. Instructions are written by name with their decoded
// operands; branch, jump, call and code argument targets get labels ("start", "init",
// "l_<offset>"); every sub-op lists its arguments by name with what was pushed for each. The data
// area is written item by item, decoded as the scripts use it.
core::YamlNode efeScriptToYaml(std::span<const std::byte> payload, const EfeHeader& header,
                               const EfeScripts& scripts, const EfeAnalysis& analysis,
                               const EfeYamlIds& ids = {});

} // namespace dwe::formats::effect
