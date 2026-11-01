-- The cup's bracket (std/std_main.c: STD_drawBracket, STD_renderBracketIntro). The game drives it
-- (apps/game/src/tournament_bracket_screen.cpp) and publishes it as settings, bumping
-- tournament_bracket.revision once per change: .phase (hidden, intro, board, tail), .fade (0 lit to
-- 128 dark), .card and .shade (the intro's card and shade, 128 full), .intro, .blackout (0 to 1),
-- .board and .card_src (images), .label (the round, for skins that show it), .slot.<i>.x/.y/.src/
-- .rect/.flip/.name for the eight fighters, .shots and .shot.<k>.x/.y/.src/.rect/.flip/.doubled,
-- .flashes and .flash.<k>.x/.y with .flash_src and .flash_rect. Positions are the original's
-- pixels from the screen's centre.

local Slots = 8
local MaxShots = 8
local Pixel = 3
local CentreX = 160
local CentreY = 120
-- The names stand under the first row (STD_drawBracket's glyphs start 0x12 below it).
local NameDrop = 18

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

local function picture(element, source, rect)
  if element:attribute("src") ~= source then
    element:set_attribute("src", source)
  end
  if rect ~= nil and element:attribute("rect") ~= rect then
    element:set_attribute("rect", rect)
  end
end

local function opacity(element, value)
  element:set_property("opacity", string.format("%.3f", math.max(0, math.min(1, value))))
end

local function refresh(document)
  local phase = text("tournament_bracket.phase")
  local hidden = phase == "hidden" or phase == ""
  for _, id in ipairs({ "frame", "shade", "card-frame", "blackout" }) do
    document:get(id):set_class("hidden", hidden)
  end
  if hidden then
    return
  end
  local lit = (128 - number("tournament_bracket.fade", 0)) / 128
  local board = document:get("board")
  picture(board, text("tournament_bracket.board"), nil)
  opacity(board, lit)
  local first_row = nil
  for i = 0, Slots - 1 do
    local prefix = "tournament_bracket.slot." .. i .. "."
    local slot = document:get("slot-" .. i)
    local x = number(prefix .. "x", 0)
    local y = number(prefix .. "y", 0)
    picture(slot, text(prefix .. "src"), text(prefix .. "rect"))
    place(slot, x, y)
    slot:set_class("flipped", flag(prefix .. "flip"))
    opacity(slot, lit)
    if first_row == nil or y > first_row then
      first_row = y
    end
  end
  for i = 0, Slots - 1 do
    local prefix = "tournament_bracket.slot." .. i .. "."
    local name = document:get("name-" .. i)
    name:set_text(text(prefix .. "name"))
    place(name, number(prefix .. "x", 0), (first_row or 0) + NameDrop)
    opacity(name, lit)
  end
  local shots = number("tournament_bracket.shots", 0)
  for i = 0, MaxShots - 1 do
    local prefix = "tournament_bracket.shot." .. i .. "."
    local shot = document:get("shot-" .. i)
    local second = document:get("shot-" .. i .. "-b")
    local shown = i < shots
    shot:set_class("hidden", not shown)
    second:set_class("hidden", not (shown and flag(prefix .. "doubled")))
    if shown then
      local x = number(prefix .. "x", 0)
      local y = number(prefix .. "y", 0)
      for _, element in ipairs({ shot, second }) do
        picture(element, text(prefix .. "src"), text(prefix .. "rect"))
        element:set_class("flipped", flag(prefix .. "flip"))
      end
      place(shot, x, y)
      place(second, x, y + 8)
    end
  end
  local flashes = number("tournament_bracket.flashes", 0)
  for i = 0, Slots - 1 do
    local flash = document:get("flash-" .. i)
    local shown = i < flashes
    flash:set_class("hidden", not shown)
    if shown then
      picture(flash, text("tournament_bracket.flash_src"), text("tournament_bracket.flash_rect"))
      place(
        flash,
        number("tournament_bracket.flash." .. i .. ".x", 0),
        number("tournament_bracket.flash." .. i .. ".y", 0)
      )
    end
  end
  local intro = flag("tournament_bracket.intro")
  local card = document:get("card")
  local card_src = text("tournament_bracket.card_src")
  card:set_class("hidden", not intro or card_src == "")
  if card_src ~= "" then
    picture(card, card_src, nil)
  end
  opacity(card, number("tournament_bracket.card", 0) / 128)
  local shade = document:get("intro-shade")
  shade:set_class("hidden", not intro)
  opacity(shade, number("tournament_bracket.shade", 0) / 256)
  document:get("label"):set_text(text("tournament_bracket.label"))
  opacity(document:get("blackout"), number("tournament_bracket.blackout", 0))
end

function on_show(document)
  refresh(document)
end

function on_setting(document, name)
  if name == "tournament_bracket.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end
