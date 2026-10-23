-- The gym's roulette (trn_slots.c: TRN_renderSlotMachine): three reels of symbols 1 to 7. The game
-- publishes gym.slots.<reel>.row<n> (the symbols from two cells above the line to one below),
-- gym.slots.<reel>.offset (how far down the reel is drawn, in pixels) and gym.slots.payout (the
-- rate in tenths, -1 until the reels stop), and bumps gym.slots.revision; confirm reaches the game
-- as a field button.

local reels = 3
local rows = 4
-- The strip's first cell sits this far above the window when the reel rests.
local rest_top = -48

local function refresh(document)
  for reel = 0, reels - 1 do
    local base = "gym.slots." .. reel .. "."
    for row = 0, rows - 1 do
      local symbol = math.floor(ui.setting(base .. "row" .. row) or 0)
      document:get("cell-" .. reel .. "-" .. row):set_text(symbol > 0 and tostring(symbol) or "")
    end
    local offset = math.floor(ui.setting(base .. "offset") or 0)
    document:get("strip-" .. reel):set_property("top", (rest_top + offset) .. "dp")
  end
  local payout = math.floor(ui.setting("gym.slots.payout") or -1)
  local line = document:get("payout")
  line:set_text(payout >= 0 and ("x" .. string.format("%.1f", payout / 10)) or "")
  line:set_class("hidden", payout < 0)
end

function on_show(document)
  refresh(document)
end

function on_setting(document, _name)
  refresh(document)
end
