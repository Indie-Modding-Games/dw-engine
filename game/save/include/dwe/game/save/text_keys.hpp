#pragma once

#include <dwe/game/save/autosave.hpp>

#include <string>
#include <string_view>

// The localisation keys of what the player sees about saving (docs/architecture/localizacion.md). The texts live in
// the localisation files of the `save` area, never in code.
namespace dwe::game::save::text_key {

// The toast after an autosave.
inline constexpr std::string_view Autosaved = "save.autosaved";
// The toast when an autosave could not be written.
inline constexpr std::string_view AutosaveFailed = "save.autosave_failed";
// The start of each reason's key: `save.autosave_reason.map_change`.
inline constexpr std::string_view AutosaveReasonPrefix = "save.autosave_reason.";

// The key of a reason's text, for a slot list or a toast that says why the game saved.
[[nodiscard]] std::string autosaveReason(AutosaveReason reason);

} // namespace dwe::game::save::text_key
