#pragma once

#include <dwe/core/yaml.hpp>
#include <dwe/formats/effect/efe_analysis.hpp>
#include <dwe/formats/effect/efe_file.hpp>
#include <dwe/formats/effect/efe_script.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

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

// A slot's scripts read back from its script YAML: what the game runs (battle_effect.c:
// BTL_runEFEScript). The instructions keep their decoded operands; the raw halfwords
// (EfeInstruction::word and operands) are not rebuilt. The data area is the slot's bytes from its
// base up to the start script, fill (0xCD) where the YAML gives no value: the variables, tables,
// instance tables and the numbers, vectors and colours the sub-ops point at get their values; the
// data only drawing reads (bone offsets, sprites, UV keyframes, CLUTs, tile maps, ribbons) stays
// fill.
struct EfeProgram {
  std::uint32_t startScript = 0; // offsets from the slot's base
  std::uint32_t initScript = 0;
  std::vector<EfeInstruction> instructions; // the start script's, then the init script's
  std::vector<std::byte> data;

  // The index of the instruction at an offset, or nothing.
  [[nodiscard]] std::optional<std::size_t> instructionAt(std::uint32_t offset) const;
};

// Reads the document efeScriptToYaml writes. Fails on an unknown op, sub-op, operator or
// comparison, a variable or label it cannot read, and data outside the data area.
core::Result<EfeProgram> readEfeScriptYaml(const core::YamlNode& root);

} // namespace dwe::formats::effect
