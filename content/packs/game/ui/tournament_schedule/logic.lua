-- The registration at the arena's counter (dget/dget.c: initTournamentSchedule,
-- tickTournamentSchedule, initTournamentInfo). The game drives it
-- (apps/game/src/tournament_schedule_screen.cpp) and publishes it as settings, bumping
-- tournament_schedule.revision once per change: .phase (schedule, peek, question, choice), .title,
-- .column and .row of the cursor, .day.<c> (the day of the cycle over each column),
-- .cell.<c>.<r>.grade and .state (closed, open, overlevel or empty), and two boxes of coloured runs,
-- info (the cup's text) and talk (the question or the choice): .<box>.rows, .<box>.<r>.runs and
-- .<box>.<r>.run.<k>.text/.color, with .talk_cursor the choice's row. Every button goes to the game
-- as tournament_schedule_action.

local Days = 5
local Rows = 6
local MaxColor = 10
-- One pixel of the original is 3dp; a column is 51 pixels and a row 16 (renderTournamentSchedule).
local ColumnDp = 153
local RowDp = 48
local Boxes = { info = { rows = 8, runs = 4 }, talk = { rows = 4, runs = 6 } }

local function number(name, fallback)
  local value = ui.setting(name)
  if type(value) ~= "number" then
    return fallback
  end
  return value
end

local function text(name)
  local value = ui.setting(name)
  if type(value) ~= "string" then
    return ""
  end
  return value
end

local function draw_grid(document)
  for column = 0, Days - 1 do
    document:get("day-" .. column):set_text(text("tournament_schedule.day." .. column))
    for row = 0, Rows - 1 do
      local prefix = "tournament_schedule.cell." .. column .. "." .. row .. "."
      local cell = document:get("cell-" .. column .. "-" .. row)
      cell:set_text(text(prefix .. "grade"))
      local state = text(prefix .. "state")
      for _, name in ipairs({ "closed", "open", "overlevel" }) do
        cell:set_class(name, state == name)
      end
    end
  end
  local cursor = document:get("cursor")
  cursor:set_property("left", string.format("%ddp", 21 + ColumnDp * number("tournament_schedule.column", 0)))
  cursor:set_property("top", string.format("%ddp", 75 + RowDp * number("tournament_schedule.row", 0)))
end

local function draw_box(document, name)
  local shape = Boxes[name]
  local prefix = "tournament_schedule." .. name .. "."
  local count = number("tournament_schedule." .. name .. ".rows", 0)
  local cursor = name == "talk" and number("tournament_schedule.talk_cursor", -1) or -1
  for row = 0, shape.rows - 1 do
    local element = document:get(name .. "-" .. row)
    element:set_class("hidden", row >= count)
    element:set_class("selected", row == cursor)
    local runs = row < count and number(prefix .. row .. ".runs", 0) or 0
    for run = 0, shape.runs - 1 do
      local span = document:get(name .. "-" .. row .. "-run-" .. run)
      local entry = prefix .. row .. ".run." .. run
      span:set_text(run < runs and text(entry .. ".text") or "")
      local color = number(entry .. ".color", 1)
      for index = 0, MaxColor do
        span:set_class("color-" .. index, index == color)
      end
    end
  end
end

local function refresh(document)
  local phase = text("tournament_schedule.phase")
  local frame = document:get("frame")
  for _, name in ipairs({ "schedule", "peek", "question", "choice" }) do
    frame:set_class("phase-" .. name, phase == name)
  end
  document:get("title-text"):set_text(text("tournament_schedule.title"))
  draw_grid(document)
  draw_box(document, "info")
  draw_box(document, "talk")
end

function on_show(document)
  refresh(document)
end

function on_setting(document, name)
  if name == "tournament_schedule.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end

-- Every button is the game's: the document neither moves a focus nor closes by itself.
function on_action(_document, action)
  ui.send("tournament_schedule_action", action)
  return true
end
