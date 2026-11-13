-- The story's textboxes, drawn from the UI API's story namespace (docs/modding/ui-api.md):
-- story.textbox() is the box as the game publishes it (apps/game: story_textbox.cpp), the page
-- already in the player's language and split in rows of coloured runs, and story.changed comes once
-- per change; only then this script redraws. The keys reach the dialogue from the game, not from
-- this document, which never takes focus.

local max_rows = 12
local max_runs = 8
local max_color = 10
local column_dp = 36
local row_dp = 39
local padding_dp = 30
-- The box's growth as it opens, kept for the ticks that fit it in the window again.
local opening_scale = 1

local function place_box(element, box)
  local sized = box.kind == "sized"
  element:set_class("sized", sized)
  if not sized then
    element:set_property("width", "786dp")
    element:set_property("height", "177dp")
    -- setupDialogueBox: x -130 from the centre, 262 wide (structure.rcss).
    element:set_property("margin-left", "-390dp")
    element:set_property("margin-top", "0dp")
    return
  end
  -- ui.c: createAnimatedUIBox, the sized box is its columns and rows of text and a border round
  -- them.
  local width = box.size.columns * column_dp + padding_dp
  local height = box.size.rows * row_dp + padding_dp
  element:set_property("width", string.format("%ddp", math.floor(width)))
  element:set_property("height", string.format("%ddp", math.floor(height)))
  element:set_property("margin-left", string.format("%ddp", -math.floor(width / 2)))
  element:set_property("margin-top", string.format("%ddp", -math.floor(height / 2)))
end

-- script_textbox.c: scriptShowSelection, the selection's bar is as wide as the page's longest line,
-- 12 pixels a character and 2 more, at most 240, whichever option it marks. A skin that draws the
-- bar shows .row.selected .bar.
local function bar_width(columns)
  if columns < 0 then
    return nil
  end
  return string.format("%ddp", math.min(columns * 12 + 2, 240) * 3)
end

local function fill_rows(document, box)
  local count = #box.rows
  local cursor_row = box.cursor_row
  local width = bar_width(box.cursor_columns)
  for row = 0, max_rows - 1 do
    local element = document:get("row-" .. row)
    element:set_class("hidden", row >= count)
    element:set_class("selected", row == cursor_row)
    element:set_class("barred", row == cursor_row and width ~= nil)
    if row == cursor_row and width ~= nil then
      document:get("row-" .. row .. "-bar"):set_property("width", width)
    end
    local runs = row < count and box.rows[row + 1].runs or {}
    local spans = {}
    for run = 0, max_runs - 1 do
      local span = document:get("row-" .. row .. "-run-" .. run)
      spans[#spans + 1] = span
      local entry = runs[run + 1]
      -- The original's button characters and the {prompt:...} markers are the device's glyphs.
      span:set_prompt_text(entry and entry.text or "", true)
      local color = entry and entry.color or 1
      for index = 0, max_color do
        span:set_class("color-" .. index, index == color)
      end
    end
    -- A row a long name made wider than the box is squeezed into it (common/fit_text.lua).
    fit_text.fit(element, spans)
  end
end

local function refresh(document)
  local box = story.textbox()
  local element = document:get("box")
  place_box(element, box)
  element:set_class("top", box.top)
  -- The box grows over its opening frames (ui.c: tickAnimatedUIBox); its text shows once it is
  -- open.
  opening_scale = math.max(box.opening, 0.05)
  -- A UI scale larger than the window allows draws the box smaller, inside the window
  -- (common/screen_fit.lua).
  screen_fit.apply(document, element, opening_scale)
  fill_rows(document, box)
  document:get("rows"):set_class("hidden", box.opening < 1)
  document:get("prompt"):set_class("hidden", not box.prompt)
end

-- The confirm button the box shows while it waits (script_textbox.c: renderUIBox), as the device
-- in use's glyph.
local function refresh_prompt(document)
  document:get("prompt"):set_prompt("confirm", true)
end

function on_show(document)
  refresh_prompt(document)
  refresh(document)
end

function on_device(document)
  refresh_prompt(document)
  refresh(document)
end

hooks.on("story.changed", function(_, document)
  if ui.is_visible(document_name) then
    refresh(document)
  end
end)

function on_language(document)
  refresh_prompt(document)
  refresh(document)
end

function on_tick(document, _seconds)
  fit_text.tick()
  screen_fit.apply(document, document:get("box"), opening_scale)
end
