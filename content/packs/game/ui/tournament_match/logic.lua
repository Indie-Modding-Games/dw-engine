-- What a cup match shows over the arena (std/std_main.c: STD_renderIntroStats,
-- STD_tickBattleResultScreen, STD_renderTimeoutWindow; std_hud.c: STD_renderLoseScene). The game
-- drives it (apps/game/src/tournament_match_screen.cpp); this script reads it through the UI API
-- (battle.match(), docs/modding/ui-api.md) and redraws on its battle.match event. Positions are the
-- original's pixels from the screen's centre.

local Pixel = 3
local CentreX = 160
local CentreY = 120
local Columns = 32

local function place(element, x, y)
  element:set_property("left", string.format("%ddp", math.floor((x + CentreX) * Pixel)))
  element:set_property("top", string.format("%ddp", math.floor((y + CentreY) * Pixel)))
end

-- STD_renderIntroStatNumber: the value right-aligned in `count` cells of 12, from (x, y); without
-- leading zeros. Each cell is the digit as text and a class d-<digit>, which the original look
-- draws as its sprite of stdtim 002.
local function digits(document, prefix, count, value, x, y, shown)
  local s = tostring(math.max(0, math.floor(value)))
  local first = count - #s
  for i = 0, count - 1 do
    local cell = document:get(prefix .. i)
    local visible = shown and i >= first
    cell:set_class("hidden", not visible)
    local digit = visible and (tonumber(string.sub(s, i - first + 1, i - first + 1)) or 0) or -1
    for glyph = 0, 9 do
      cell:set_class("d-" .. glyph, digit == glyph)
    end
    if visible then
      cell:set_text(tostring(digit))
      place(cell, x + i * 12, y)
    end
  end
end

local function refresh(document)
  local match = battle.match()
  local shown = match.shown
  document:get("frame"):set_class("hidden", not shown)
  document:get("wipe"):set_class("hidden", not shown)
  if not shown then
    return
  end
  local name = document:get("name")
  name:set_text(match.name)
  name:set_property("top", string.format("%ddp", math.floor((match.name_y + CentreY) * Pixel)))
  local stats = match.stats
  document:get("stats"):set_class("hidden", not stats)
  if stats then
    local counted = match.counted
    for i = 0, 5 do
      local stat = match.stat[i + 1]
      local label = document:get("label-" .. i)
      label:set_text(stat.label)
      place(label, -100, i * 16 - 28)
      local bar = document:get("bar-" .. i)
      place(bar, -50, i * 16 - 26)
      bar:set_property("width", string.format("%ddp", math.floor(stat.width * Pixel)))
      digits(document, "digit-" .. i .. "-", 4, stat.value, 52, i * 16 - 28, counted)
    end
  end
  local time_up = document:get("time-up")
  local time_shown = match.time_up
  time_up:set_class("hidden", not time_shown)
  if time_shown then
    time_up:set_text(match.time_up_label)
  end
  local box = match.box
  document:get("damage-box"):set_class("hidden", not box)
  if box then
    document:get("partner-name"):set_text(match.partner)
    document:get("rival-name"):set_text(match.rival)
    document:get("damage-label"):set_text(match.damage_label)
    -- Inside the box: positions from its corner, the box at (-132, -27).
    local dealt = match.dealt
    local taken = match.taken
    local dealt_digits = #tostring(math.max(0, math.floor(dealt)))
    local taken_digits = #tostring(math.max(0, math.floor(taken)))
    digits(
      document,
      "dealt-",
      4,
      dealt,
      42 + (48 - dealt_digits * 12) / 2 - (4 - dealt_digits) * 12 - 160,
      30 - 120,
      true
    )
    digits(
      document,
      "taken-",
      4,
      taken,
      174 + (48 - taken_digits * 12) / 2 - (4 - taken_digits) * 12 - 160,
      30 - 120,
      true
    )
  end
  for i = 0, Columns - 1 do
    local column = document:get("column-" .. i)
    local height = match.wipe[i + 1]
    column:set_property("left", string.format("%ddp", i * 10 * Pixel))
    column:set_property("height", string.format("%ddp", math.floor(height * Pixel)))
  end
end

function on_show(document)
  refresh(document)
end

hooks.on("battle.match", function(_, document)
  if ui.is_visible(document_name) then
    refresh(document)
  end
end)

function on_language(document)
  refresh(document)
end
