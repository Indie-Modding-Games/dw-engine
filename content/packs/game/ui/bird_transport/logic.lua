-- The bird transport the story opens (shop/script_menu.c: tickBirdraTransport). The game drives it
-- (apps/game/src/bird_transport_screen.cpp) and publishes it as settings, bumping bird.revision once
-- per change: bird.phase (greeting, list, confirm, choice, farewell), .speaker (the carrier's name, a
-- text key; empty on the yes-or-no), .player (the player's name, on the yes-or-no), .line_key (what
-- the carrier says, or the yes-or-no's lines), .bits_key and .money (the money box), .choice,
-- .cursor, .total and bird.row.<n>.name/cost/affordable. Every button goes to the game as
-- bird_action.

local Rows = 6
-- The money box and the menu stay up while the carrier asks about the picked place (states 3
-- and 4).
local MenuPhases = { list = true, confirm = true, choice = true }

local function number(name)
  return math.floor(tonumber(ui.setting(name)) or 0)
end

local function text(name)
  local key = ui.setting(name) or ""
  if key == "" then
    return ""
  end
  return t(key)
end

local function lines_of(value)
  local lines = {}
  for line in string.gmatch(value, "[^\n]+") do
    lines[#lines + 1] = line
  end
  return lines
end

-- renderShopBitsBox: the line's word on the first row, the money right-aligned on the second.
local function draw_bits(document)
  local money = number("bird.money")
  local key = ui.setting("bird.bits_key") or ""
  local bits = key ~= "" and t(key, { bits = money }) or tostring(money)
  local label, value = string.match(bits, "^(.-)%s*(%S+)$")
  document:get("bits-label"):set_text(label or "")
  document:get("bits-value"):set_text(value or bits)
end

local function draw_menu(document)
  local total = number("bird.total")
  for row = 0, Rows - 1 do
    local prefix = "bird.row." .. row .. "."
    local shown = row < total
    -- MAP_NAME_PTR: the name without the spaces that centre it on the map's banner.
    local name = shown and string.match(text(prefix .. "name"), "^%s*(.-)%s*$") or ""
    document:get("name-" .. row):set_text(name)
    -- calculateBirdramonMenuStrings: the price after the name, in four digits.
    document:get("cost-" .. row):set_text(shown and tostring(number(prefix .. "cost")) or "")
    -- A place the money does not pay for is red (colour 3).
    document
      :get("row-" .. row)
      :set_class("unaffordable", shown and not ui.setting(prefix .. "affordable"))
  end
  document
    :get("cursor")
    :set_property("top", string.format("%ddp", 54 + 54 * number("bird.cursor")))
end

local function draw_talk(document, phase)
  local choice = phase == "choice"
  local speaker = choice and (ui.setting("bird.player") or "") or text("bird.speaker")
  document:get("speaker"):set_text(speaker)
  document:get("speaker"):set_class("hidden", speaker == "")
  document:get("speaker"):set_class("player", choice)
  local rows = lines_of(text("bird.line_key"))
  local selected = choice and number("bird.choice") or nil
  -- showMapheadSelection: the bar is as wide as the longest choice, GLYPH_WIDTH (12 px) a letter
  -- plus 2 px.
  local widest = 0
  for _, value in ipairs(rows) do
    widest = math.max(widest, (utf8 and utf8.len(value)) or #value)
  end
  for index = 0, 2 do
    local value = rows[index + 1]
    local row = document:get("talk-" .. index)
    row:set_class("hidden", value == nil)
    row:set_class("selected", selected == index)
    local run = document:get("talk-run-" .. index)
    run:set_text(value or "")
    run:set_property("width", choice and string.format("%ddp", widest * 36 + 6) or "auto")
  end
end

local function refresh(document)
  local phase = ui.setting("bird.phase") or "closed"
  document:get("frame"):set_class("menu", MenuPhases[phase] == true)
  draw_bits(document)
  draw_menu(document)
  draw_talk(document, phase)
end

function on_show(document)
  refresh(document)
end

function on_setting(document, name)
  if name == "bird.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end

-- Every button is the game's: the document neither moves a focus nor closes by itself.
function on_action(_document, action)
  ui.send("bird_action", action)
  return true
end
