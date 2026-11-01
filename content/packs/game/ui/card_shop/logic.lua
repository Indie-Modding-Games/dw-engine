-- The card shop's menus (shop/script_menu.c: openBuyCardMenu, openSellCardMenu, createCardMenu,
-- tickCardMenu). The game drives them (apps/game/src/card_shop_screen.cpp) and publishes them as
-- settings, bumping card_shop.revision once per change: card_shop.phase (talk, list, confirm,
-- amount), .mode (buy or sell), .speaker (the keeper's name, a text key), .line_key (what the
-- keeper says), .total, .top and .cursor of the list, .choice (0 yes, 1 no), .confirm_key (the "is
-- it OK?" box: the question, then the two answers on one line), card_shop.row.<n>.name, .price,
-- .count, .rarity (-1 for an empty row) and .ink (new, stuck or nothing) for the six rows shown,
-- and card_shop.amount.name, .rarity, .price, .value and .total for the amount box. The bulk
-- purchase (an engine improvement, mode packs) adds the phases packs ("How many packs?" in the
-- amount box) and summary (the cards they gave on the list, new ones in the new ink), with
-- card_shop.packs.value, .most, .price, .total, .opened, .cards and .new. Every button goes to the
-- game as card_shop_action.

local Rows = 6
-- renderItemMenuScrollBar: the track is 90 original pixels (3dp each).
local ThumbTrack = 270
-- calculateCardMenuStrings: the inks of a buy row.
local Inks = { "new", "stuck" }

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
  local total = number("card_shop.total")
  local top = number("card_shop.top")
  local mode = ui.setting("card_shop.mode")
  -- The bulk purchase's summary shows how many of each card, in the sell list's count column.
  local counted = mode == "sell" or mode == "packs"
  local priced = mode ~= "packs"
  for row = 0, Rows - 1 do
    local prefix = "card_shop.row." .. row .. "."
    local name = ui.setting(prefix .. "name") or ""
    local rarity = number(prefix .. "rarity")
    local shown = rarity >= 0
    document:get("name-" .. row):set_text(name ~= "" and t(name) or "")
    document
      :get("price-" .. row)
      :set_text((shown and priced) and tostring(number(prefix .. "price")) or "")
    document
      :get("count-" .. row)
      :set_text((shown and counted) and tostring(number(prefix .. "count")) or "")
    local ink = ui.setting(prefix .. "ink") or ""
    for _, value in ipairs(Inks) do
      document:get("row-" .. row):set_class(value, ink == value)
    end
    set_rarity(document:get("rarity-" .. row), rarity)
  end
  document
    :get("cursor")
    :set_property("top", string.format("%ddp", 48 + 54 * number("card_shop.cursor")))
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

-- The bulk purchase's own lines: how many packs, then what they gave.
local function packs_line(phase)
  if phase == "packs" then
    return t("bulk.packs_ask", { most = number("card_shop.packs.most") })
  end
  return t("bulk.packs_got", {
    packs = number("card_shop.packs.opened"),
    cards = number("card_shop.packs.cards"),
    new = number("card_shop.packs.new"),
  })
end

local function draw_talk(document, phase)
  local speaker = text("card_shop.speaker")
  document:get("speaker"):set_text(speaker)
  document:get("speaker"):set_class("hidden", speaker == "")
  local line = ui.setting("card_shop.mode") == "packs" and packs_line(phase)
    or text("card_shop.line_key")
  local rows = lines_of(line)
  for index = 0, 2 do
    document:get("talk-" .. index):set_text(rows[index + 1] or "")
  end
end

local function draw_confirm(document)
  local lines = lines_of(text("card_shop.confirm_key"))
  document:get("question"):set_text(lines[1] or "")
  local answers = {}
  for answer in string.gmatch(lines[2] or "", "%S+") do
    answers[#answers + 1] = answer
  end
  document:get("yes"):set_text(answers[1] or t("common.yes"))
  document:get("no"):set_text(answers[2] or t("common.no"))
  local choice = number("card_shop.choice")
  document:get("yes"):set_class("selected", choice == 0)
  document:get("no"):set_class("selected", choice == 1)
end

local function draw_packs(document)
  document:get("amount-name"):set_text(t("bulk.packs"))
  set_rarity(document:get("amount-rarity"), -1)
  document:get("amount-price"):set_text(tostring(number("card_shop.packs.price")))
  document:get("amount-value"):set_text(tostring(number("card_shop.packs.value")))
  document:get("amount-total"):set_text(tostring(number("card_shop.packs.total")))
end

local function draw_amount(document)
  local name = ui.setting("card_shop.amount.name") or ""
  document:get("amount-name"):set_text(name ~= "" and t(name) or "")
  set_rarity(document:get("amount-rarity"), number("card_shop.amount.rarity"))
  document:get("amount-price"):set_text(tostring(number("card_shop.amount.price")))
  document:get("amount-value"):set_text(tostring(number("card_shop.amount.value")))
  document:get("amount-total"):set_text(tostring(number("card_shop.amount.total")))
end

local function refresh(document)
  local phase = ui.setting("card_shop.phase") or "closed"
  local frame = document:get("frame")
  for _, name in ipairs({ "talk", "list", "confirm", "amount", "packs", "summary" }) do
    frame:set_class("phase-" .. name, phase == name)
  end
  frame:set_class("mode-sell", ui.setting("card_shop.mode") == "sell")
  frame:set_class("mode-packs", ui.setting("card_shop.mode") == "packs")
  -- The summary's columns are titled in words; the other lists keep the skin's labels.
  local packs = ui.setting("card_shop.mode") == "packs"
  document:get("label-menu"):set_text(packs and t("bulk.card") or "")
  document:get("label-count"):set_text(packs and t("bulk.copies") or "")
  draw_list(document)
  draw_talk(document, phase)
  draw_confirm(document)
  if phase == "amount" then
    draw_amount(document)
  elseif phase == "packs" then
    draw_packs(document)
  end
end

function on_show(document)
  refresh(document)
end

function on_setting(document, name)
  if name == "card_shop.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end

-- Every button is the game's: the document neither moves a focus nor closes by itself.
function on_action(_document, action)
  ui.send("card_shop_action", action)
  return true
end
