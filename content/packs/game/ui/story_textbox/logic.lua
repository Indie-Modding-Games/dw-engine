-- The story's textboxes: the game publishes the dialogue's view as story_textbox.* settings
-- (apps/game: story_textbox.cpp) and bumps story_textbox.revision once per change; only then this
-- script redraws. The game has already put the page in the player's language and split it in rows
-- of coloured runs (story_textbox.row.<r>.runs, story_textbox.row.<r>.run.<k>.text and .color).
-- The keys reach the dialogue from the game, not from this document, which never takes focus.

local max_rows = 12
local max_runs = 8
local max_color = 10
local column_dp = 36
local row_dp = 39
local padding_dp = 30

local function number(name, fallback)
  local value = ui.setting(name)
  if type(value) ~= "number" then
    return fallback
  end
  return value
end

local function place_box(box, sized)
  box:set_class("sized", sized)
  if not sized then
    box:set_property("width", "786dp")
    box:set_property("height", "177dp")
    box:set_property("margin-left", "-393dp")
    box:set_property("margin-top", "0dp")
    return
  end
  -- ui.c: createAnimatedUIBox, the sized box is its columns and rows of text and a border round
  -- them.
  local width = number("story_textbox.columns", 1) * column_dp + padding_dp
  local height = number("story_textbox.rows", 1) * row_dp + padding_dp
  box:set_property("width", string.format("%ddp", math.floor(width)))
  box:set_property("height", string.format("%ddp", math.floor(height)))
  box:set_property("margin-left", string.format("%ddp", -math.floor(width / 2)))
  box:set_property("margin-top", string.format("%ddp", -math.floor(height / 2)))
end

local function fill_rows(document)
  local count = number("story_textbox.row_count", 0)
  local cursor_row = number("story_textbox.cursor_row", -1)
  for row = 0, max_rows - 1 do
    local element = document:get("row-" .. row)
    element:set_class("hidden", row >= count)
    element:set_class("selected", row == cursor_row)
    local prefix = "story_textbox.row." .. row
    local runs = row < count and number(prefix .. ".runs", 0) or 0
    for run = 0, max_runs - 1 do
      local span = document:get("row-" .. row .. "-run-" .. run)
      local entry = prefix .. ".run." .. run
      local text = run < runs and ui.setting(entry .. ".text") or ""
      span:set_text(text)
      local color = number(entry .. ".color", 1)
      for index = 0, max_color do
        span:set_class("color-" .. index, index == color)
      end
    end
  end
end

local function refresh(document)
  local box = document:get("box")
  place_box(box, ui.setting("story_textbox.kind") == "sized")
  -- The box grows over its opening frames (ui.c: tickAnimatedUIBox); its text shows once it is
  -- open.
  local opening = number("story_textbox.opening", 1)
  box:set_property("transform", string.format("scale(%.3f)", math.max(opening, 0.05)))
  fill_rows(document)
  document:get("rows"):set_class("hidden", opening < 1)
  document:get("prompt"):set_class("hidden", ui.setting("story_textbox.prompt") ~= true)
end

function on_show(document)
  refresh(document)
end

function on_setting(document, name)
  if name == "story_textbox.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end
