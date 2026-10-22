#pragma once

#include <optional>
#include <string>

namespace dwe::assets {

// A reference from one game table to an entry of another, as dwx writes it: the entry's id, nothing
// for none, or the raw number as text when it is outside the referenced table.
using TableReference = std::optional<std::string>;

} // namespace dwe::assets
