-- luacheck's settings for the engine's own Lua (scripts/dev.sh lint): the interface scripts of the packs and the
-- sample game script. The globals below are the engine's Lua API; any other global is an error.
std = "lua54"
max_line_length = 100
-- An argument a callback's signature needs but its body does not use starts with an underscore.
ignore = { "212/_.*" }

-- An interface document runs in an environment of its own (engine/ui/src/scripting.cpp). The game gives it the ui
-- table, t() and document_name, and calls the functions it defines at the top: on_show, on_action, on_tick and the
-- rest, and the handlers its markup names, so a function it defines at the top is not unused.
files["content/packs/*/ui/**/*.lua"] = {
  allow_defined_top = true,
  ignore = { "131" },
  read_globals = { "ui", "t", "document_name", "skin_choice" },
}

-- The skin entry the main menu and the pause menu share, loaded into both documents.
files["content/packs/*/ui/common/ui_skin_choice.lua"] = {
  globals = { "skin_choice" },
}

-- The game's sample script: the game calls tick(dt), and marker keeps its state across reloads.
files["apps/game/scripts/main.lua"] = {
  read_globals = { "input" },
  globals = { "tick", "marker" },
}
