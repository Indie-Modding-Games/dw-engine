-- luacheck's settings for the engine's own Lua (scripts/dev.sh lint): the interface scripts of the
-- packs and the sample game script. The globals below are the engine's Lua API; any other global is
-- an error.
std = "lua54"
max_line_length = 100
-- An argument a callback's signature needs but its body does not use starts with an underscore.
ignore = { "212/_.*" }

-- An interface document runs in an environment of its own (packages/engine/ui/src/scripting.cpp). The game
-- gives it the ui table, t() and document_name, and calls the functions it defines at the top:
-- on_show, on_action, on_tick and the rest, and the handlers its markup names, so a function it
-- defines at the top is not unused.
files["content/packs/*/ui/**/*.lua"] = {
  allow_defined_top = true,
  ignore = { "131" },
  read_globals = {
    "ui", "t", "document_name", "skin_choice", "original_box", "bag_slot", "game_options",
    "panel_enter", "fit_text",
  },
}

-- The skin entry the main menu and the pause menu share, loaded into both documents.
files["content/packs/*/ui/common/ui_skin_choice.lua"] = {
  globals = { "skin_choice" },
}

-- The original look's animated boxes, loaded into the menus that open and close them.
files["content/packs/*/ui/common/original_box.lua"] = {
  globals = { "original_box" },
}

-- The menu panel's short fade when a document opens (only the look), loaded into the menus.
files["content/packs/*/ui/common/panel_enter.lua"] = {
  globals = { "panel_enter" },
}

-- The game options the menus share, loaded into the documents that show them.
files["content/packs/*/ui/common/game_options.lua"] = {
  globals = { "game_options" },
}

-- The text that shrinks to fit its room, loaded into the documents that need it.
files["content/packs/*/ui/common/fit_text.lua"] = {
  globals = { "fit_text" },
}

-- One bag slot (icon, name, count), loaded into the documents that list the bag.
files["content/packs/*/ui/common/bag_slot.lua"] = {
  globals = { "bag_slot" },
}

-- The game's sample script: the game calls tick(dt), and marker keeps its state across reloads.
files["apps/game/scripts/main.lua"] = {
  read_globals = { "input" },
  globals = { "tick", "marker" },
}
