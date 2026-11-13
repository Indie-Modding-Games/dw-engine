-- The UI API's naming (docs/modding/ui-api.md, namespace naming): the naming box the story opens to
-- ask the player's or the partner's name (apps/game: naming_box.cpp), and what a UI may ask of it.
-- The game takes the player's buttons and typing itself; these commands let a UI that draws the box
-- its own way write and accept a name too. The game keeps every rule: which letters the grid has,
-- how long a name may be, that OK takes no empty name.
--
-- Every function returns a fresh table; changing it changes nothing in the game.

naming = {}

local function whole(name, fallback)
  local found = ui.setting(name)
  if type(found) ~= "number" then
    return fallback
  end
  return math.floor(found)
end

local function text(name)
  local found = ui.setting(name)
  return type(found) == "string" and found or ""
end

-- Whether the naming box is on screen, taking a name.
function naming.is_open()
  return ui.is_visible("naming_box")
end

-- The box: title_key (whose name it asks), length (the most letters a name may have), letters (the
-- name so far, one letter each, up to length), name (them joined, without the blanks at its end),
-- grid ({rows, columns}), cursor ({row, column} on a letter, -1 each on a button; button: "back",
-- "ok" or nil).
function naming.state()
  local length = whole("naming_box.length", 0)
  local letters = {}
  for index = 0, length - 1 do
    letters[#letters + 1] = text("naming_box.name." .. index)
  end
  local button = whole("naming_box.cursor_button", -1)
  return {
    title_key = text("naming_box.title"),
    length = length,
    letters = letters,
    name = (table.concat(letters):gsub("%s+$", "")),
    grid = { rows = whole("naming_box.rows", 0), columns = whole("naming_box.columns", 0) },
    cursor = {
      row = whole("naming_box.cursor_row", -1),
      column = whole("naming_box.cursor_column", -1),
      button = button == 0 and "back" or button == 1 and "ok" or nil,
    },
  }
end

-- The letter of the grid's cell (row and column from 0); a blank cell is " ", a cell past the grid
-- "".
function naming.cell(row, column)
  return text("naming_box.cell." .. row .. "." .. column)
end

local not_open = "the naming box is not open"

-- Writes letters of the grid at the end of the name, as the keyboard's typing does; a letter the
-- grid lacks is refused with its sound.
function naming.write(letters)
  if not naming.is_open() then
    return false, not_open
  end
  if type(letters) ~= "string" or letters == "" then
    return false, "letters must be a text"
  end
  ui.send("naming_write", letters)
  return true
end

-- Takes the last letter back, as cancel.
function naming.erase()
  if not naming.is_open() then
    return false, not_open
  end
  ui.send("naming_erase")
  return true
end

-- OK: the game takes the name unless it is empty (then it refuses it with its sound).
function naming.accept()
  if not naming.is_open() then
    return false, not_open
  end
  ui.send("naming_accept")
  return true
end

-- naming.changed: the name, the cursor or the grid changed.
event_source("naming_box.revision", "naming.changed")
