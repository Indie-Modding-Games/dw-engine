-- The naming box (script_menu.c: setupNameSelectorBox, setupNameDisplayBox, tickNamingBox): the
-- letters' box and, apart, the name's box, drawn from the UI API's naming namespace
-- (docs/modding/ui-api.md): naming.state() is whose name it asks (a text key; the original's box
-- only says "Name", so the original look hides it), the longest name, the letters so far, the
-- grid's size and the cursor, naming.cell(row, column) a letter of the grid (a space for an empty
-- cell), and naming.changed comes with each change. The game takes the buttons and the keyboard's
-- typing; the document never takes focus.

local max_rows = 9
local max_columns = 10
-- The longest name the game asks for: the engine's long names (twelve letters, naming_box.hpp).
local max_letters = 12

local function refresh(document)
  local state = naming.state()
  document:get("whose"):set_text(t(state.title_key))
  local length = state.length
  -- setupNameDisplayBox, renderNamingUnderscore: the underscore marks the next letter, or the last
  -- one once the name is full.
  local written = 0
  for i = 0, max_letters - 1 do
    local letter = state.letters[i + 1] or ""
    if letter ~= "" and i < length then
      written = i + 1
    end
  end
  local current = math.min(written, length - 1)
  for i = 0, max_letters - 1 do
    local slot = document:get("letter-" .. i)
    local letter = state.letters[i + 1] or ""
    slot:set_text(letter == "" and " " or letter)
    slot:set_class("hidden", i >= length)
    slot:set_class("current", i == current)
  end
  local rows = state.grid.rows
  local columns = state.grid.columns
  for row = 0, max_rows - 1 do
    document:get("row-" .. row):set_class("hidden", row >= rows)
    for column = 0, max_columns - 1 do
      local cell = document:get("cell-" .. row .. "-" .. column)
      cell:set_text(naming.cell(row, column))
      cell:set_class("hidden", column >= columns)
      cell:set_class("selected", row == state.cursor.row and column == state.cursor.column)
    end
  end
  document:get("button-back"):set_class("selected", state.cursor.button == "back")
  document:get("button-ok"):set_class("selected", state.cursor.button == "ok")
end

local function refresh_prompts(document)
  -- On a keyboard the box takes the typed text first, so Space writes a blank and Q a "q": their
  -- hints (Write and Clear all) would lie there; the typing hint says how to write instead.
  local keyboard = ui.device() ~= "gamepad"
  document:get("hint-write"):set_class("hidden", keyboard)
  document:get("hint-clear"):set_class("hidden", keyboard)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
  -- Start on a gamepad (the original's OK button), Tab on a keyboard.
  document:get("prompt-menu"):set_prompt(ui.device() == "gamepad" and "pause" or "menu")
  document:get("prompt-extra"):set_prompt("extra")
end

function on_show(document)
  refresh(document)
  refresh_prompts(document)
end

hooks.on("naming.changed", function(_, document)
  if ui.is_visible(document_name) then
    refresh(document)
  end
end)

function on_language(document)
  refresh(document)
end

function on_device(document)
  refresh_prompts(document)
end
