#include <array>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

core::Result<void> registerOpeningRoutines(StoryScenesContext& scenes) {
  using Register = core::Result<void> (*)(StoryScenesContext&);
  // One line per routine file.
  constexpr std::array<Register, 0> Routines{};
  for (const Register add : Routines) {
    if (auto added = add(scenes); !added) {
      return added;
    }
  }
  return {};
}

} // namespace dwe::session
