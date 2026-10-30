-- The gym's stats box after a course (trn_hud.c: TRN_createPostTrainingStatsBox and its render):
-- the six stats as the game counts the gains into them, with their bars and the gains left, and the
-- brains course's box under it (new orders, a move learned).
-- The game publishes, and bumps gym.revision on a change:
--   gym.box.state: 0 nothing, 1 the outline growing or shrinking, 2 the box;
--   gym.box.x, .y, .w, .h: the outline (ui.c: renderUIBoxAnim), in the original's pixels from the
--     middle of its 320 x 240 screen;
--   gym.stat.<n>.label (a text key), .value, .gain (left to count), .changed (1 when the session
--     changed it), .max (the bar's full value);
--   gym.notice.count, gym.notice.<n>.text and .name (text keys), gym.notice.<n>.order (1: an
--     order's line) and gym.notice.move (1: the shorter box of a move learned).
-- Confirm and cancel reach the game as field buttons.

local stats = 6
local notices = 4
-- One pixel of the original is 3dp; its screen's middle is 120 pixels from the top.
local dp = 3
local middle_y = 120
-- trn_hud.c: the bars are 50 pixels at full.
local bar_pixels = 50

local function number(name, fallback)
  local value = ui.setting(name)
  if type(value) ~= "number" then
    return fallback
  end
  return value
end

local function text(key, arguments)
  if key == nil or key == "" then
    return ""
  end
  return t(key, arguments)
end

local function place_outline(document, shown)
  local outline = document:get("outline")
  outline:set_class("hidden", not shown)
  if not shown then
    return
  end
  local x = math.floor(number("gym.box.x", 0))
  local y = math.floor(number("gym.box.y", 0))
  local w = math.max(math.floor(number("gym.box.w", 0)), 1)
  local h = math.max(math.floor(number("gym.box.h", 0)), 1)
  outline:set_property("left", "50%")
  outline:set_property("margin-left", string.format("%ddp", x * dp))
  outline:set_property("top", string.format("%ddp", (middle_y + y) * dp))
  outline:set_property("width", string.format("%ddp", w * dp))
  outline:set_property("height", string.format("%ddp", h * dp))
end

local function fill_stats(document)
  for index = 0, stats - 1 do
    local base = "gym.stat." .. index .. "."
    local value = math.floor(number(base .. "value", 0))
    local gain = math.floor(number(base .. "gain", 0))
    local max = math.max(math.floor(number(base .. "max", 1)), 1)
    document:get("stat-" .. index .. "-label"):set_text(text(ui.setting(base .. "label")))
    document:get("stat-" .. index):set_text(tostring(value))
    document:get("stat-" .. index .. "-bar"):set_class("raised", number(base .. "changed", 0) == 1)
    local pixels = math.max(math.min(math.floor(value * bar_pixels / max), bar_pixels), 0)
    document
      :get("stat-" .. index .. "-fill")
      :set_property("width", string.format("%ddp", pixels * dp))
    -- The render skips a stat with no gain left: no arrow, no number.
    document:get("gain-" .. index .. "-arrow"):set_class("hidden", gain == 0)
    document:get("gain-" .. index):set_text(gain ~= 0 and tostring(gain) or "")
  end
end

local function fill_notes(document, shown)
  local count = shown and math.floor(number("gym.notice.count", 0)) or 0
  local notes = document:get("notes")
  notes:set_class("hidden", count == 0)
  notes:set_class("move", number("gym.notice.move", 0) == 1)
  for index = 0, notices - 1 do
    local line = document:get("notice-" .. index)
    if index < count then
      local base = "gym.notice." .. index .. "."
      line:set_text(text(ui.setting(base .. "text"), { name = text(ui.setting(base .. "name")) }))
      line:set_class("order", number(base .. "order", 0) == 1)
    else
      line:set_text("")
    end
    line:set_class("hidden", index >= count)
  end
end

local function refresh(document)
  local state = math.floor(number("gym.box.state", 0))
  place_outline(document, state == 1)
  document:get("box"):set_class("hidden", state ~= 2)
  fill_stats(document)
  fill_notes(document, state == 2)
end

function on_show(document)
  refresh(document)
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
end
