-- The naming box (script_menu.c: setupNameSelectorBox, setupNameDisplayBox, tickNamingBox): the
-- letters' box and, apart, the name's box. The game publishes naming_box.title (a text key: whose
-- name; the original's box only says "Name", so the original look hides it), naming_box.length
-- (the longest name), naming_box.name.<i> (the letters so far), naming_box.rows and
-- naming_box.columns (the grid's size; the document has room for 9 rows of 10),
-- naming_box.cell.<row>.<column> (a letter, or a space for an empty cell) and
-- naming_box.cursor_row / naming_box.cursor_column (-1 on a button) and naming_box.cursor_button
-- (0 Back, 1 OK, -1 in the grid), and bumps naming_box.revision. The game takes the buttons and
-- the keyboard's typing; the document never takes focus.

local max_rows = 9
local max_columns = 10
-- The longest name the game asks for: the engine's long names (twelve letters, naming_box.hpp).
local max_letters = 12

local function number(name, default)
  return math.floor(ui.setting(name) or default)
end

local function refresh(document)
  document:get("whose"):set_text(t(ui.setting("naming_box.title") or ""))
  local length = number("naming_box.length", 0)
  -- setupNameDisplayBox, renderNamingUnderscore: the underscore marks the next letter, or the last
  -- one once the name is full.
  local written = 0
  for i = 0, max_letters - 1 do
    local letter = ui.setting("naming_box.name." .. i) or ""
    if letter ~= "" and i < length then
      written = i + 1
    end
  end
  local current = math.min(written, length - 1)
  for i = 0, max_letters - 1 do
    local slot = document:get("letter-" .. i)
    local letter = ui.setting("naming_box.name." .. i) or ""
    slot:set_text(letter == "" and " " or letter)
    slot:set_class("hidden", i >= length)
    slot:set_class("current", i == current)
  end
  local rows = number("naming_box.rows", 0)
  local columns = number("naming_box.columns", 0)
  local cursor_row = number("naming_box.cursor_row", 0)
  local cursor_column = number("naming_box.cursor_column", 0)
  for row = 0, max_rows - 1 do
    document:get("row-" .. row):set_class("hidden", row >= rows)
    for column = 0, max_columns - 1 do
      local cell = document:get("cell-" .. row .. "-" .. column)
      cell:set_text(ui.setting("naming_box.cell." .. row .. "." .. column) or "")
      cell:set_class("hidden", column >= columns)
      cell:set_class("selected", row == cursor_row and column == cursor_column)
    end
  end
  local cursor_button = number("naming_box.cursor_button", -1)
  document:get("button-back"):set_class("selected", cursor_button == 0)
  document:get("button-ok"):set_class("selected", cursor_button == 1)
end

local function refresh_prompts(document)
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

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
end

function on_device(document)
  refresh_prompts(document)
end
