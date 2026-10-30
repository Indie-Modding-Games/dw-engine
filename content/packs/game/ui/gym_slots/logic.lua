-- The gym's roulette (trn_slots.c: TRN_renderSlotMachine): three reels of symbols 1 to 7. The game
-- publishes, and bumps gym.slots.revision:
--   gym.slots.box.state: 0 nothing, 1 the outline growing or shrinking, 2 the machine;
--   gym.slots.box.x, .y, .w, .h: the outline, in the original's pixels from the middle of its
--     320 x 240 screen;
--   gym.slots.<reel>.row<n>: the symbols from two cells above the line to one below;
--   gym.slots.<reel>.offset: how far down the reel is drawn, in the original's pixels;
--   gym.slots.payout: the rate in tenths, -1 until the reels stop.
-- Confirm reaches the game as a field button.

local reels = 3
local rows = 4
local symbols = 7
-- One pixel of the original is 3dp; its screen's middle is 120 pixels from the top.
local dp = 3
local middle_y = 120
-- TRN_renderSlotMachine: the first cell starts 122 pixels above the middle, the window 74: 48
-- pixels above the window when the reel rests.
local rest_top = -48

local function number(name, fallback)
  local value = ui.setting(name)
  if type(value) ~= "number" then
    return fallback
  end
  return value
end

local function place_outline(document, shown)
  local outline = document:get("outline")
  outline:set_class("hidden", not shown)
  if not shown then
    return
  end
  local x = math.floor(number("gym.slots.box.x", 0))
  local y = math.floor(number("gym.slots.box.y", 0))
  local w = math.max(math.floor(number("gym.slots.box.w", 0)), 1)
  local h = math.max(math.floor(number("gym.slots.box.h", 0)), 1)
  outline:set_property("left", "50%")
  outline:set_property("margin-left", string.format("%ddp", x * dp))
  outline:set_property("top", string.format("%ddp", (middle_y + y) * dp))
  outline:set_property("width", string.format("%ddp", w * dp))
  outline:set_property("height", string.format("%ddp", h * dp))
end

local function fill_reels(document)
  for reel = 0, reels - 1 do
    local base = "gym.slots." .. reel .. "."
    for row = 0, rows - 1 do
      local symbol = math.floor(number(base .. "row" .. row, 0))
      local cell = document:get("cell-" .. reel .. "-" .. row)
      cell:set_text(symbol > 0 and tostring(symbol) or "")
      for index = 1, symbols do
        cell:set_class("symbol-" .. index, index == symbol)
      end
    end
    local offset = math.floor(number(base .. "offset", 0))
    document
      :get("strip-" .. reel)
      :set_property("top", string.format("%ddp", (rest_top + offset) * dp))
  end
  local payout = math.floor(number("gym.slots.payout", -1))
  local line = document:get("payout")
  line:set_text(payout >= 0 and ("x" .. string.format("%.1f", payout / 10)) or "")
  line:set_class("hidden", payout < 0)
end

local function refresh(document)
  local state = math.floor(number("gym.slots.box.state", 0))
  place_outline(document, state == 1)
  document:get("box"):set_class("hidden", state ~= 2)
  if state == 2 then
    fill_reels(document)
  end
end

function on_show(document)
  refresh(document)
end

function on_setting(document, _name)
  refresh(document)
end
