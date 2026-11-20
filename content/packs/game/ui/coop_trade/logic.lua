-- An item trade between two players of a co-op game (apps/game/src/coop_trades.hpp). The game
-- publishes it as settings and counts coop_trade.revision up on each change:
--   coop_trade.shown, coop_trade.phase (asking: this player waits for the other's answer; invited:
--   the other asks this player; open: both put items on the table; executing: both confirmed and
--   the items are being swapped), the other player (coop_trade.other.name, else .name_key with
--   .number), this player's bag (coop_trade.bag.<n>.*: id, name_key, count left, offered,
--   tradeable, item_color, icon, icon_rect), both offers (coop_trade.own.<n>.* and
--   coop_trade.their.<n>.*: id, name_key, count, item_color, icon, icon_rect), both
--   confirmations (coop_trade.own_confirmed, coop_trade.their_confirmed) and whether this player
--   can confirm now (coop_trade.can_confirm).
-- The mouse clicks the rows and buttons. The keys: the arrows move (left and right between the bag,
-- the offer and the buttons), confirm puts one of a bag's item on the table or takes one back,
-- tab_next confirms or takes the confirmation back, and cancel calls the trade off (or refuses an
-- ask). Every change goes to the game, which checks it.

local BagRows = 10
local OfferRows = 8

local function number(name)
  local value = ui.setting(name)
  return type(value) == "number" and math.floor(value) or 0
end

local function text(name)
  local value = ui.setting(name)
  return type(value) == "string" and value or ""
end

local function named(key)
  if key == "" then
    return ""
  end
  return t(key)
end

local function other_name()
  local name = text("coop_trade.other.name")
  if name ~= "" then
    return name
  end
  local key = text("coop_trade.other.name_key")
  if key == "" then
    return ""
  end
  return (string.gsub(t(key), "{number}", tostring(number("coop_trade.other.number"))))
end

-- Where the keys are: the column (bag, own, actions), its row and the bag's first row shown.
local column = "bag"
local rows = { bag = 0, own = 0, actions = 0 }
local scroll = 0
local ask_choice = 0

local function phase()
  return text("coop_trade.phase")
end

-- Fills an item row `id` from the list entry `prefix` (".id", ".name_key", ".count"...).
local function draw_item(document, id, prefix, shown, count)
  local row = document:get(id)
  row:set_class("hidden", not shown)
  if not shown then
    return
  end
  document:get(id .. "-name"):set_text(named(text(prefix .. "name_key")))
  document:get(id .. "-count"):set_text("x" .. tostring(count))
  local color = number(prefix .. "item_color")
  row:set_class("item-field", color == 0)
  row:set_class("item-battle", color == 1)
  row:set_class("item-shared", color == 2)
  local icon = document:get(id .. "-icon")
  local source = text(prefix .. "icon")
  icon:set_class("hidden", source == "")
  if source ~= "" then
    icon:set_attribute("src", source)
    icon:set_attribute("rect", text(prefix .. "icon_rect"))
  end
end

local function draw_bag(document)
  local count = number("coop_trade.bag_count")
  scroll = math.max(0, math.min(scroll, count - BagRows))
  for row = 0, BagRows - 1 do
    local slot = scroll + row
    local prefix = "coop_trade.bag." .. slot .. "."
    local shown = slot < count
    draw_item(document, "bag-" .. row, prefix, shown, number(prefix .. "count"))
    if shown then
      local element = document:get("bag-" .. row)
      element:set_class("locked", ui.setting(prefix .. "tradeable") ~= true)
      element:set_class("offered", number(prefix .. "offered") > 0)
      element:set_class("spent", number(prefix .. "count") <= 0)
    end
  end
  document:get("bag-up"):set_class("hidden", scroll == 0)
  document:get("bag-down"):set_class("hidden", scroll + BagRows >= count)
  local none = document:get("bag-none")
  none:set_class("hidden", count > 0)
  none:set_text(t("coop_trade.empty_bag"))
end

local function draw_offer(document, side)
  local count = number("coop_trade." .. side .. "_count")
  for row = 0, OfferRows - 1 do
    local prefix = "coop_trade." .. side .. "." .. row .. "."
    draw_item(document, side .. "-" .. row, prefix, row < count, number(prefix .. "count"))
  end
  local none = document:get(side .. "-none")
  none:set_class("hidden", count > 0)
  none:set_text(t("coop_trade.nothing"))
  local state = document:get(side .. "-state")
  local ready = ui.setting("coop_trade." .. side .. "_confirmed") == true
  state:set_class("ready", ready)
  if phase() == "executing" then
    state:set_text(t("coop_trade.trading"))
  else
    state:set_text(t(ready and "coop_trade.ready" or "coop_trade.choosing"))
  end
end

local function draw_ask(document, current, name)
  local box = document:get("ask")
  local asking = current == "asking"
  local invited = current == "invited"
  box:set_class("hidden", not (asking or invited))
  if not (asking or invited) then
    return
  end
  document
    :get("ask-text")
    :set_text(t(invited and "coop_trade.invited" or "coop_trade.asking", { name = name }))
  document:get("accept"):set_text(t("coop_trade.accept"))
  document:get("refuse"):set_text(t("coop_trade.refuse"))
  document:get("ask-cancel"):set_text(t("coop_trade.cancel"))
  document:get("accept"):set_class("hidden", not invited)
  document:get("refuse"):set_class("hidden", not invited)
  document:get("ask-cancel"):set_class("hidden", not asking)
end

local function draw(document)
  local shown = ui.setting("coop_trade.shown") == true
  document:get("scrim"):set_class("hidden", not shown)
  if not shown then
    return
  end
  local current = phase()
  local name = other_name()
  draw_ask(document, current, name)
  local table_shown = current == "open" or current == "executing"
  document:get("window"):set_class("hidden", not table_shown)
  if not table_shown then
    return
  end
  document:get("title"):set_text(t("coop_trade.title", { name = name }))
  document:get("bag-title"):set_text(t("coop_trade.your_bag"))
  document:get("own-title"):set_text(t("coop_trade.your_offer"))
  document:get("their-title"):set_text(t("coop_trade.their_offer", { name = name }))
  draw_bag(document)
  draw_offer(document, "own")
  draw_offer(document, "their")
  local own_ready = ui.setting("coop_trade.own_confirmed") == true
  local confirm = document:get("confirm")
  confirm:set_text(t(own_ready and "coop_trade.unconfirm" or "coop_trade.confirm"))
  confirm:set_class(
    "unavailable",
    current ~= "open" or not (own_ready or ui.setting("coop_trade.can_confirm") == true)
  )
  confirm:set_class("ready", own_ready)
  local cancel = document:get("cancel")
  cancel:set_text(t("coop_trade.cancel"))
  cancel:set_class("unavailable", current ~= "open")
  document:get("help"):set_prompt_text(t("coop_trade.help", {
    add = "{prompt:confirm}",
    ready = "{prompt:tab_next}",
    cancel = "{prompt:cancel}",
  }))
end

-- ---- Commands ----

function pick_bag(row)
  local slot = scroll + row
  local prefix = "coop_trade.bag." .. slot .. "."
  local id = text(prefix .. "id")
  if phase() == "open" and id ~= "" and ui.setting(prefix .. "tradeable") == true then
    ui.send("coop_trade_add", id)
  end
end

function pick_own(row)
  local id = text("coop_trade.own." .. row .. ".id")
  if phase() == "open" and id ~= "" then
    ui.send("coop_trade_take", id)
  end
end

function toggle_confirm()
  if phase() ~= "open" then
    return
  end
  local ready = ui.setting("coop_trade.own_confirmed") == true
  if ready or ui.setting("coop_trade.can_confirm") == true then
    ui.send("coop_trade_confirm", not ready)
  end
end

function answer(accept)
  if phase() == "invited" then
    ui.send("coop_trade_answer", accept)
  end
end

function cancel_trade()
  local current = phase()
  if current == "invited" then
    ui.send("coop_trade_answer", false)
  elseif current == "asking" or current == "open" then
    ui.send("coop_trade_cancel")
  end
end

-- ---- Keys ----

local function count_of(name)
  if name == "bag" then
    return math.min(BagRows, number("coop_trade.bag_count"))
  elseif name == "own" then
    return number("coop_trade.own_count")
  end
  return 2
end

local function place_focus(document)
  local current = phase()
  if current == "asking" or current == "invited" then
    local buttons = current == "invited" and { "accept", "refuse" } or { "ask-cancel" }
    ask_choice = math.max(0, math.min(ask_choice, #buttons - 1))
    document:get(buttons[ask_choice + 1]):focus()
    return
  end
  if current ~= "open" then
    return
  end
  if count_of(column) == 0 then
    column = number("coop_trade.bag_count") > 0 and "bag" or "actions"
  end
  local row = math.max(0, math.min(rows[column], count_of(column) - 1))
  rows[column] = row
  if column == "bag" then
    document:get("bag-" .. row):focus()
  elseif column == "own" then
    document:get("own-" .. row):focus()
  else
    document:get(row == 0 and "confirm" or "cancel"):focus()
  end
end

local Columns = { "bag", "own", "actions" }

local function step_column(step)
  local at = 1
  for index, name in ipairs(Columns) do
    if name == column then
      at = index
    end
  end
  for _ = 1, #Columns do
    at = (at - 1 + step) % #Columns + 1
    if count_of(Columns[at]) > 0 then
      column = Columns[at]
      return
    end
  end
end

local function step_row(document, step)
  if column == "bag" then
    local total = number("coop_trade.bag_count")
    local slot = scroll + rows.bag + step
    if slot < 0 or slot >= total then
      return
    end
    if slot < scroll then
      scroll = slot
    elseif slot >= scroll + BagRows then
      scroll = slot - BagRows + 1
    end
    rows.bag = slot - scroll
    draw(document)
  else
    rows[column] = rows[column] + step
  end
end

function on_show(document)
  column = "bag"
  rows = { bag = 0, own = 0, actions = 0 }
  scroll = 0
  ask_choice = 0
  draw(document)
  place_focus(document)
end

function on_setting(document, name)
  if name == "coop_trade.revision" or name == "coop_trade.shown" then
    draw(document)
    place_focus(document)
  end
end

function on_action(document, action)
  local current = phase()
  if action == "cancel" then
    cancel_trade()
  elseif current == "asking" or current == "invited" then
    if action == "left" or action == "up" then
      ask_choice = ask_choice - 1
    elseif action == "right" or action == "down" then
      ask_choice = ask_choice + 1
    elseif action == "confirm" then
      if current == "invited" then
        answer(ask_choice == 0)
      else
        cancel_trade()
      end
    end
  elseif current == "open" then
    if action == "left" then
      step_column(-1)
    elseif action == "right" then
      step_column(1)
    elseif action == "up" then
      step_row(document, -1)
    elseif action == "down" then
      step_row(document, 1)
    elseif action == "tab_next" then
      toggle_confirm()
    elseif action == "confirm" then
      if column == "bag" then
        pick_bag(rows.bag)
      elseif column == "own" then
        pick_own(rows.own)
      elseif rows.actions == 0 then
        toggle_confirm()
      else
        cancel_trade()
      end
    end
  end
  place_focus(document)
  return true
end

function on_language(document)
  draw(document)
end
