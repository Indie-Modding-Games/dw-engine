-- The item trader the story opens (shop/script_menu.c: openMojyamonShop). The game drives it
-- (apps/game/src/trade_screen.cpp) and publishes it as settings, bumping trade.revision once per
-- change: trade.phase (greeting, list, confirm, choice, thanks, bag_full, farewell), .speaker (the
-- trader's name, a text key; empty on the yes-or-no), .player (the player's name, on the
-- yes-or-no), .line_key (what the trader says, or the yes-or-no's lines), .offered_key (the item
-- the line's {item:pstat=N} names), .label_key (the two column titles), .choice, .cursor, .total
-- and trade.row.<n>.give/get/owned/traded. Every button goes to the game as trade_action.

local Rows = 3
-- The menu stays up while the trader asks about the offered item (states 3 and 4).
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

-- renderMojyaTradeMenu cuts one string in two columns. A run of two or more spaces splits it;
-- else its words are halved, the first half for the first column.
local function columns(label)
  local first, second = string.match(label, "^(.-)%s%s+(.+)$")
  if first then
    return first, second
  end
  local words = {}
  for word in string.gmatch(label, "%S+") do
    words[#words + 1] = word
  end
  local half = math.ceil(#words / 2)
  return table.concat(words, " ", 1, half), table.concat(words, " ", half + 1)
end

local function draw_menu(document)
  local give, get = columns(text("trade.label_key"))
  document:get("label-give"):set_text(give)
  document:get("label-get"):set_text(get)
  local total = number("trade.total")
  for row = 0, Rows - 1 do
    local prefix = "trade.row." .. row .. "."
    local shown = row < total
    document:get("give-" .. row):set_text(shown and text(prefix .. "give") or "")
    document:get("get-" .. row):set_text(shown and text(prefix .. "get") or "")
    -- calculateItemListStrings: an item the bag does not hold is red (colour 3), and a trade made
    -- before has the yellow mark (colour 7) before it.
    document:get("row-" .. row):set_class("missing", shown and not ui.setting(prefix .. "owned"))
    document
      :get("row-" .. row)
      :set_class("traded", shown and ui.setting(prefix .. "traded") == true)
  end
  document
    :get("cursor")
    :set_property("top", string.format("%ddp", 75 + 54 * number("trade.cursor")))
end

local function draw_talk(document, phase)
  local choice = phase == "choice"
  local speaker = choice and (ui.setting("trade.player") or "") or text("trade.speaker")
  document:get("speaker"):set_text(speaker)
  document:get("speaker"):set_class("hidden", speaker == "")
  document:get("speaker"):set_class("player", choice)
  local line = text("trade.line_key")
  -- showTextbox writes the item variable 249 names (tickMojyaTradeMenu wrote the offered one).
  local offered = text("trade.offered_key")
  line = string.gsub(line, "{item:pstat=%d+}", function()
    return offered
  end)
  local rows = lines_of(line)
  local selected = choice and number("trade.choice") or nil
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
  local phase = ui.setting("trade.phase") or "closed"
  document:get("frame"):set_class("menu", MenuPhases[phase] == true)
  draw_menu(document)
  draw_talk(document, phase)
end

function on_show(document)
  refresh(document)
end

function on_setting(document, name)
  if name == "trade.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end

-- Every button is the game's: the document neither moves a focus nor closes by itself.
function on_action(_document, action)
  ui.send("trade_action", action)
  return true
end
