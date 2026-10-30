-- The lost life's lives box (murd.c: MURD_createLivesBox, MURD_tickLivesBox, MURD_renderLivesBox,
-- MURD_animateLivesBoxOut). The game publishes lives_box.* (apps/screens: lives_box.hpp) and counts
-- up lives_box.revision; the box takes no input. MURD_tickScene's fade (layer 0, over the box)
-- darkens it too: the game reports the fade it subtracts from the scene (screen_fade, 0 to 1) and
-- the box fades out with it.

local max_slots = 5
-- The box in place (structure.rcss): its left in percent of the window and dp from there, its top
-- in percent of the window, and its size in dp.
local box_left = 50
local box_margin_left = -285
local box_top = 29.1667
local box_width = 570
local box_height = 132
-- The rectangle it grows from (MURD_LIVES_BOX_START_POS): 16 x 12 pixels round the partner's point.
local start_width = 48
local start_height = 36
-- ETCHI/LIFE.TIM's cells (MURD_LIFE_FULL and MURD_LIFE_EMPTY).
local full_rect = "0 0 24 24"
local empty_rect = "24 0 24 24"

local function number(name, fallback)
  return tonumber(ui.setting(name)) or fallback
end

local function lerp(from, to, amount)
  return from + (to - from) * amount
end

-- ui.c: renderUIBoxAnim: each edge a quarter of the way more on each frame, from the partner's
-- point to the box's place.
local function place(box, grow)
  local anchor_x = number("lives_box.anchor_x", 50)
  local anchor_y = number("lives_box.anchor_y", 50)
  box:set_property("left", string.format("%.3f%%", lerp(anchor_x, box_left, grow)))
  box:set_property("top", string.format("%.3f%%", lerp(anchor_y, box_top, grow)))
  box:set_property(
    "margin-left",
    string.format("%.1fdp", lerp(-start_width / 2, box_margin_left, grow))
  )
  box:set_property("margin-top", string.format("%.1fdp", lerp(-start_height / 2, 0, grow)))
  box:set_property("width", string.format("%.1fdp", lerp(start_width, box_width, grow)))
  box:set_property("height", string.format("%.1fdp", lerp(start_height, box_height, grow)))
end

-- MURD_renderLivesBox: the lives left full, the one just lost as the box's frame shows it, and the
-- rest empty.
local function fill(document)
  local lives = number("lives_box.lives", 0)
  local slots = number("lives_box.slots", 0)
  local lost_full = ui.setting("lives_box.lost_full") == true
  for index = 0, max_slots - 1 do
    local slot = document:get("slot-" .. index)
    slot:set_class("hidden", index >= slots)
    local full = index < lives or (index == lives and lost_full)
    slot:set_attribute("rect", full and full_rect or empty_rect)
  end
end

local function refresh_fade(document)
  local level = ui.setting("screen_fade")
  if type(level) ~= "number" then
    level = 0
  end
  local opacity = string.format("%.3f", 1 - math.min(math.max(level, 0), 1))
  document:get("box"):set_property("opacity", opacity)
end

local function refresh(document)
  local box = document:get("box")
  local outline = ui.setting("lives_box.outline") == true
  box:set_class("outline", outline)
  document:get("lives"):set_class("hidden", outline)
  place(box, number("lives_box.grow", 1))
  fill(document)
end

function on_show(document)
  refresh(document)
  refresh_fade(document)
end

function on_setting(document, name)
  if name == "lives_box.revision" then
    refresh(document)
  elseif name == "screen_fade" then
    refresh_fade(document)
  end
end
