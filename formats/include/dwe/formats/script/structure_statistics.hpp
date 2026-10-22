#pragma once

#include <dwe/formats/script/structuring.hpp>

#include <cstddef>

// Counts over a structured script, for the reports on how readable the converted code is.
namespace dwe::formats::script {

struct StructureStatistics {
  std::size_t sections = 0;
  std::size_t subroutines = 0;
  std::size_t sharedTails = 0;
  std::size_t stateMachines = 0; // routines that fell back, by reason below
  std::size_t irreducible = 0;
  std::size_t nestedExits = 0;
  std::size_t duplications = 0;
  std::size_t copiedRoutines = 0; // routines that hold some code more than once (tail duplication)
  std::size_t ifs = 0;
  std::size_t loops = 0;
  std::size_t breaks = 0;
  std::size_t choices = 0;
  std::size_t tailCalls = 0;
  std::size_t maxDepth = 0; // the deepest nesting of if, loop and choice statements, elseif flat
  std::size_t emittedInstructions = 0;  // in runs, copies counted
  std::size_t distinctInstructions = 0; // in runs, each once per script

  StructureStatistics& operator+=(const StructureStatistics& other);
};

// Counts the routines and statements of a structured script.
StructureStatistics measureStructure(const StructuredScript& script);

} // namespace dwe::formats::script
