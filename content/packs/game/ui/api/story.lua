-- The UI API's story (docs/modding/ui-api.md, namespace story): the story's textbox as the game
-- publishes it (apps/game: story_textbox.cpp, new_game_interview.cpp, story_text_rows.cpp) and the
-- presses a UI may give it while it waits for the player. The game already put the page in the
-- player's language, filled its names and numbers and split it in rows of coloured runs; it keeps
-- every rule: when a page waits, how long the box takes to open, which options a selection has. The
-- game also reads the player's own buttons, so a UI that only draws the box needs no command.
--
-- Every function returns a fresh table; changing it changes nothing in the game.

story = {}

local function value(name, fallback)
  local found = ui.setting(name)
  if found == nil then
    return fallback
  end
  return found
end

local function number(name, fallback)
  local found = ui.setting(name)
  if type(found) ~= "number" then
    return fallback
  end
  return found
end

local function whole(name, fallback)
  return math.floor(number(name, fallback))
end

-- One row of the box: its runs of text, each with the font's colour (0 to 10).
local function row_at(index)
  local prefix = "story_textbox.row." .. index
  local runs = {}
  for run = 0, whole(prefix .. ".runs", 0) - 1 do
    local entry = prefix .. ".run." .. run
    runs[#runs + 1] = {
      text = tostring(value(entry .. ".text", "")),
      color = whole(entry .. ".color", 1),
    }
  end
  return { runs = runs }
end

-- What the box waits for from the player: "page" (a page's confirm), "choice" (a selection's
-- option) or "" (nothing: no box, a box still opening, a page that goes on by itself).
function story.input()
  local input = ui.setting("story_textbox.input")
  if input == "page" or input == "choice" then
    return input
  end
  return ""
end

-- The box: open, kind ("speech", or "sized" for a box of its own size: size = {columns, rows} of
-- text),
-- top (the speech box at the top of the window), opening (from 0 to 1 while it grows; its text
-- shows once it is 1), origin ({x, y}, the fraction of the window it grows from, or nil), prompt
-- (it shows the confirm prompt), input (story.input()), page (counts up with each page shown),
-- speaker ({name, kind}: kind is none, player, partner, character or special; the name is also the
-- first row), rows (the rows as the box draws them, each {runs = {{text, color}...}}), cursor_row
-- (the row the selection's cursor is on, from 0, or -1) and cursor_columns (the characters the
-- selection's bar spans, or -1).
function story.textbox()
  local rows = {}
  for row = 0, whole("story_textbox.row_count", 0) - 1 do
    rows[#rows + 1] = row_at(row)
  end
  local origin_x = number("story_textbox.origin_x", -1)
  local origin_y = number("story_textbox.origin_y", -1)
  return {
    open = value("story_textbox.open", false) == true,
    kind = value("story_textbox.kind", "speech") == "sized" and "sized" or "speech",
    size = { columns = whole("story_textbox.columns", 0), rows = whole("story_textbox.rows", 0) },
    top = value("story_textbox.top", false) == true,
    opening = number("story_textbox.opening", 1),
    origin = (origin_x >= 0 and origin_y >= 0) and { x = origin_x, y = origin_y } or nil,
    prompt = value("story_textbox.prompt", false) == true,
    input = story.input(),
    page = whole("story_textbox.page", 0),
    speaker = {
      name = tostring(value("story_textbox.speaker", "")),
      kind = tostring(value("story_textbox.speaker_kind", "none")),
    },
    rows = rows,
    cursor_row = whole("story_textbox.cursor_row", -1),
    cursor_columns = whole("story_textbox.cursor_columns", -1),
  }
end

-- The selection on show: options (each {text, row}: its text and the row it is drawn on) and cursor
-- (the option the cursor is on, from 0); nil without one.
function story.choice()
  local count = whole("story_textbox.options", 0)
  if count <= 0 then
    return nil
  end
  local first = whole("story_textbox.first_option", 0)
  local options = {}
  for option = 0, count - 1 do
    local text = ""
    for _, run in ipairs(row_at(first + option).runs) do
      text = text .. run.text
    end
    options[#options + 1] = { text = text, row = first + option }
  end
  return { options = options, cursor = whole("story_textbox.cursor_row", first) - first }
end

-- Confirm, as the player's confirm button: a page goes on, a selection takes the option under its
-- cursor. Only while the box waits for the player.
function story.advance()
  if story.input() == "" then
    return false, "the story waits for no press now"
  end
  ui.send("story_advance")
  return true
end

-- Picks an option of the selection on show (from 0), as a click on it.
function story.choose(option)
  if story.input() ~= "choice" then
    return false, "no selection waits for an option now"
  end
  local count = whole("story_textbox.options", 0)
  if type(option) ~= "number" or option ~= math.floor(option) or option < 0 or option >= count then
    return false, string.format("option must be a whole number from 0 to %d", count - 1)
  end
  ui.send("story_choose", option)
  return true
end

-- Cancel, as the player's cancel button: a selection takes its way out (or refuses it, as the
-- story says), a sized box shows its last pages at once.
function story.cancel()
  if story.input() == "" then
    return false, "the story waits for no press now"
  end
  ui.send("story_cancel")
  return true
end

-- story.changed: the box changed (opened, grew, a page, the cursor); story.text: a new page shows;
-- story.input: the box started or stopped waiting for the player.
event_source("story_textbox.revision", "story.changed")
event_source("story_textbox.page", "story.text")
event_source("story_textbox.input", "story.input")
