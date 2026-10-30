-- The merit exchange the story opens (shop/script_menu.c: openMeritShop). The game drives it
-- (apps/game/src/merit_shop_screen.cpp) and publishes it as settings, bumping
-- merit_shop.revision once per change: merit_shop.phase (greeting, choice, card_list, card_worth,
-- card_choice, item_list, item_choice), .list (cards, items or nothing), .speaker (the keeper's
-- name, a text key; empty on the player's choices), .player (the player's name, on the choices),
-- .line_key (what is said), .shopvar (the card's worth for the line's {shopvar}), .choice,
-- .points_shown, .points_key and .points (the points box), .total, .top and .cursor of the list,
-- and merit_shop.row.<n>.name, .value (the copies of a card, the price of an item), .rarity (-1
-- for none), .stuck, .icon and .icon_rect for the six rows shown. Every button goes to the game as
-- merit_shop_action.

local Rows = 6
-- renderItemMenuScrollBar: the track is 90 original pixels (3dp each).
local ThumbTrack = 270
-- The player's choices.
local ChoicePhases = { choice = true, card_choice = true, item_choice = true }

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

-- renderCardSprite: the rarity's 12 x 12 card of SYSTEM_W.
local function set_rarity(element, rarity)
  for value = 0, 4 do
    element:set_class("rarity-" .. value, rarity == value)
  end
  element:set_class("hidden", rarity < 0)
end

local function draw_list(document)
  local total = number("merit_shop.total")
  local top = number("merit_shop.top")
  for row = 0, Rows - 1 do
    local prefix = "merit_shop.row." .. row .. "."
    local name = ui.setting(prefix .. "name") or ""
    local shown = name ~= ""
    document:get("name-" .. row):set_text(shown and t(name) or "")
    document:get("value-" .. row):set_text(shown and tostring(number(prefix .. "value")) or "")
    document:get("row-" .. row):set_class("stuck", shown and ui.setting(prefix .. "stuck") == true)
    set_rarity(document:get("rarity-" .. row), shown and number(prefix .. "rarity") or -1)
    local icon = document:get("icon-" .. row)
    local source = shown and (ui.setting(prefix .. "icon") or "") or ""
    icon:set_class("hidden", source == "")
    if source ~= "" then
      icon:set_attribute("src", source)
      icon:set_attribute("rect", ui.setting(prefix .. "icon_rect") or "")
    end
  end
  document
    :get("cursor")
    :set_property("top", string.format("%ddp", 48 + 54 * number("merit_shop.cursor")))
  local height = ThumbTrack
  local offset = 0
  if total > Rows then
    height = ThumbTrack * Rows / total
    offset = ThumbTrack * top / total
  end
  local thumb = document:get("thumb")
  thumb:set_property("height", string.format("%.2fdp", height))
  thumb:set_property("top", string.format("%.2fdp", offset))
end

local function draw_talk(document, phase)
  local choice = ChoicePhases[phase] == true
  local speaker = choice and (ui.setting("merit_shop.player") or "") or text("merit_shop.speaker")
  document:get("speaker"):set_text(speaker)
  document:get("speaker"):set_class("hidden", speaker == "")
  document:get("speaker"):set_class("player", choice)
  -- showTextbox writes SHOP_VARIABLE, the card's worth, for {shopvar}.
  local line = string.gsub(text("merit_shop.line_key"), "{shopvar}", function()
    return tostring(number("merit_shop.shopvar"))
  end)
  local rows = lines_of(line)
  local selected = choice and number("merit_shop.choice") or nil
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

-- renderShopBitsBox: the line's word on the first row, the points right-aligned on the second.
local function draw_points(document)
  local points = number("merit_shop.points")
  local key = ui.setting("merit_shop.points_key") or ""
  local line = key ~= "" and t(key, { merit = points }) or tostring(points)
  line = string.gsub(line, "{merit}", tostring(points))
  local label, value = string.match(line, "^(.-)%s*(%S+)$")
  document:get("points-label"):set_text(label or line)
  document:get("points-value"):set_text(value or "")
  document:get("points-box"):set_class("hidden", not ui.setting("merit_shop.points_shown"))
end

local function refresh(document)
  local phase = ui.setting("merit_shop.phase") or "closed"
  local list = ui.setting("merit_shop.list") or ""
  local frame = document:get("frame")
  frame:set_class("list-cards", list == "cards")
  frame:set_class("list-items", list == "items")
  if list ~= "" then
    draw_list(document)
  end
  draw_talk(document, phase)
  draw_points(document)
end

function on_show(document)
  refresh(document)
end

function on_setting(document, name)
  if name == "merit_shop.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end

-- Every button is the game's: the document neither moves a focus nor closes by itself.
function on_action(_document, action)
  ui.send("merit_shop_action", action)
  return true
end
