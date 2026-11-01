-- What a cup match shows over the arena (std/std_main.c: STD_renderIntroStats,
-- STD_tickBattleResultScreen, STD_renderTimeoutWindow; std_hud.c: STD_renderLoseScene). The game
-- drives it (apps/game/src/tournament_match_screen.cpp) and publishes it as settings, bumping
-- tournament_match.revision once per change: .shown, .name and .name_y (the intro's name as far as
-- it came in and its row), .stats (whether the box shows), .counted, .stat.<i>.label/.value/.width,
-- .time_up, .box, .partner, .rival, .damage_label, .dealt, .taken, .time_up_label, .wipe.<i> (0 to
-- 240). Positions are the original's pixels from the screen's centre.

local Pixel = 3
local CentreX = 160
local CentreY = 120
local Columns = 32

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

local function flag(name)
  local value = ui.setting(name)
  return value == true or (type(value) == "number" and value ~= 0)
end

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
  local shown = flag("tournament_match.shown")
  document:get("frame"):set_class("hidden", not shown)
  document:get("wipe"):set_class("hidden", not shown)
  if not shown then
    return
  end
  local name = document:get("name")
  name:set_text(text("tournament_match.name"))
  name:set_property(
    "top",
    string.format("%ddp", math.floor((number("tournament_match.name_y", 0) + CentreY) * Pixel))
  )
  local stats = flag("tournament_match.stats")
  document:get("stats"):set_class("hidden", not stats)
  if stats then
    local counted = flag("tournament_match.counted")
    for i = 0, 5 do
      local prefix = "tournament_match.stat." .. i .. "."
      local label = document:get("label-" .. i)
      label:set_text(text(prefix .. "label"))
      place(label, -100, i * 16 - 28)
      local bar = document:get("bar-" .. i)
      place(bar, -50, i * 16 - 26)
      bar:set_property(
        "width",
        string.format("%ddp", math.floor(number(prefix .. "width", 1) * Pixel))
      )
      digits(
        document,
        "digit-" .. i .. "-",
        4,
        number(prefix .. "value", 0),
        52,
        i * 16 - 28,
        counted
      )
    end
  end
  local time_up = document:get("time-up")
  local time_shown = flag("tournament_match.time_up")
  time_up:set_class("hidden", not time_shown)
  if time_shown then
    time_up:set_text(text("tournament_match.time_up_label"))
  end
  local box = flag("tournament_match.box")
  document:get("damage-box"):set_class("hidden", not box)
  if box then
    document:get("partner-name"):set_text(text("tournament_match.partner"))
    document:get("rival-name"):set_text(text("tournament_match.rival"))
    document:get("damage-label"):set_text(text("tournament_match.damage_label"))
    -- Inside the box: positions from its corner, the box at (-132, -27).
    local dealt = number("tournament_match.dealt", 0)
    local taken = number("tournament_match.taken", 0)
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
    local height = number("tournament_match.wipe." .. i, 0)
    column:set_property("left", string.format("%ddp", i * 10 * Pixel))
    column:set_property("height", string.format("%ddp", math.floor(height * Pixel)))
  end
end

function on_show(document)
  refresh(document)
end

function on_setting(document, name)
  if name == "tournament_match.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end
