#pragma once

#include <dwe/core/message.hpp>

#include <string_view>

// The error and port log messages of the textboxes, as std::format templates for
// core::formatMessage.
namespace dwe::game::story::dialogue_messages {

using core::formatMessage;

// The mode of set_textbox_mode is not one the original has (the mode).
inline constexpr std::string_view NoSuchMode = "set_textbox_mode: mode {} is not 0, 1 or 2";
// A selection without options (the count).
inline constexpr std::string_view NoOptions = "set_selection: {} options; a selection needs one";
// A sized box without room (the columns, the rows).
inline constexpr std::string_view NoRoom =
    "set_textbox_size: {} columns by {} rows; a box needs at least one of each";

inline constexpr std::string_view PortSetOwner =
    "[port:dialogue] set_dialog_owner (script_interp.c:771, script_common.c:2960 "
    "setDialogueOwner): owner {}, was {}, box open {}";
inline constexpr std::string_view PortOwnerKept =
    "[port:dialogue] setDialogueOwner: owner {} already speaks in the open box";
inline constexpr std::string_view PortOpenBox =
    "[port:dialogue] createTextbox (script_textbox.c:562): {} box {}x{}, grows from character {} "
    "point {}, opening frames {}";
inline constexpr std::string_view PortShowText =
    "[port:dialogue] show_textbox (script_interp.c:762, script_textbox.c:1054 showTextbox): {} "
    "speaker {}";
inline constexpr std::string_view PortShowSelection =
    "[port:dialogue] set_selection (script_textbox.c:1017 scriptShowSelection): {} with {} "
    "options, speaker {}";
inline constexpr std::string_view PortSkipPage =
    "[port:dialogue] showTextbox: {} skipped, the player skipped the sized box's pages "
    "(script_textbox.c:351 tickCustomSizedTextbox)";
inline constexpr std::string_view PortNoBox =
    "[port:dialogue] showTextbox: no box is set up for {}; opens the speech box of speaker {} "
    "(engine guard: the original would wait for ever)";
inline constexpr std::string_view PortSetMode =
    "[port:dialogue] set_textbox_mode (script_interp.c:1962): mode {} frames {}";
inline constexpr std::string_view PortSizedBox =
    "[port:dialogue] set_textbox_size (script_textbox.c:1248 scriptSetTextboxSize): origin {}, "
    "{} columns, {} rows";
inline constexpr std::string_view PortSetOrigin =
    "[port:dialogue] set_textbox_origin (script_interp.c:1521): {} {} {}";
inline constexpr std::string_view PortCloseBox =
    "[port:dialogue] fadeout_hud (script_interp.c:837, script_textbox.c:855 closeBox): box {}, "
    "open {}";
inline constexpr std::string_view PortNoStoryBox =
    "[port:dialogue] closeBox: box {} is not the story's; nothing to close";
inline constexpr std::string_view PortPageShown =
    "[port:dialogue] flipIdleTextboxPage (script_textbox.c:814): {} on show";
inline constexpr std::string_view PortBoxOpen = "[port:dialogue] renderUIBoxAnimated (ui.c:234): "
                                                "the box is open after {} frames";
inline constexpr std::string_view PortPageDone =
    "[port:dialogue] tickConfirmDialogue (script_textbox.c:280): page done, mode {}";
inline constexpr std::string_view PortSizedPageDone =
    "[port:dialogue] tickCustomSizedTextbox (script_textbox.c:351): page {}";
inline constexpr std::string_view PortSelectionDone =
    "[port:dialogue] tickSelectionDialogue (script_textbox.c:226): picked {} of {}";
inline constexpr std::string_view PortSelectionRefused =
    "[port:dialogue] tickSelectionDialogue: cancel refused while trigger {} is set";
inline constexpr std::string_view PortCursor =
    "[port:dialogue] tickSelectionDialogue: cursor on {} of {}";
inline constexpr std::string_view PortSizedClosed =
    "[port:dialogue] tickCustomSizedTextbox: the sized box closes after its last page";
inline constexpr std::string_view PortScriptStarted =
    "[port:dialogue] callScriptSection (script_interp.c:2259): the owner is the player again";
inline constexpr std::string_view PortCloseAll =
    "[port:dialogue] closeAllTextboxes (script_textbox.c:543): box open {}";
inline constexpr std::string_view PortTriggerFailed =
    "[port:dialogue] tickSelectionDialogue: trigger {} unreadable: {}";

} // namespace dwe::game::story::dialogue_messages
