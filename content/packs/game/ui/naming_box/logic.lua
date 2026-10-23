-- The naming box (script_menu.c: setupNameSelectorBox, tickNamingBox): the game publishes
-- naming_box.title (a text key), naming_box.length (the longest name), naming_box.name.<i> (the
-- letters so far), naming_box.rows and naming_box.columns (the grid's size; the document has room
-- for 9 rows of 10), naming_box.cell.<row>.<column> (a letter, or a space for an empty cell) and
-- naming_box.cursor_row / naming_box.cursor_column (the row after the grid is Back and OK), and
-- bumps naming_box.revision. The game takes the buttons; the document never takes focus.

local max_rows = 9
local max_columns = 10
local max_letters = 8

local function number(name, default)
  return math.floor(ui.setting(name) or default)
end

local function refresh(document)
  document:get("title"):set_text(t(ui.setting("naming_box.title") or ""))
  local length = number("naming_box.length", 0)
  for i = 0, max_letters - 1 do
    local slot = document:get("letter-" .. i)
    local letter = ui.setting("naming_box.name." .. i) or ""
    slot:set_text(letter == "" and "_" or letter)
    slot:set_class("hidden", i >= length)
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
  local on_buttons = cursor_row == rows
  local half = columns / 2
  document:get("button-back"):set_class("selected", on_buttons and cursor_column < half)
  document:get("button-ok"):set_class("selected", on_buttons and cursor_column >= half)
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
  document:get("prompt-cancel"):set_text(ui.prompt("cancel"))
  document:get("prompt-menu"):set_text(ui.prompt("menu"))
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
