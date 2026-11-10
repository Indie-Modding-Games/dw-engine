-- The shop the story opens (script_common.c: openShop). The game drives it and publishes it as
-- settings: shop.phase (talk, choice, list, amount), shop.mode (buy, sell), shop.line_key (the
-- keeper's line, a text key), shop.bits_key, shop.money and shop.bits_shown (the money box),
-- shop.saved (what the baby discount saved), shop.choice_count (3, or 2 in the recycle shop: buy
-- or leave), shop.rows and shop.row.<n>.name/price/count/owned/enabled/description/
-- description_species (from 0), shop.described (the row whose description box is up, -1 for
-- none: phase description), and in the amount box shop.amount.name/price/value/max/total/step/
-- owned/money_after. It sends shop_next, shop_choice (0 buy, 1 sell, 2 leave), shop_cursor (the
-- focused row, -1 for none), shop_pick (the row, from 0), shop_amount (a change),
-- shop_amount_max, shop_confirm and shop_back. Pages of eight rows, turned with left and right.
-- Shop extras (the shop_extras setting, an engine improvement) add the focused item's description
-- under the list, the bag's count in the buy list and in the amount box, and the money a trade
-- leaves. The favorite_food option (another, on by default) marks the partner's favourite food.

local PageSize = 8
local ChoiceCount = 3
local page = 1
local shown_phase = ""
-- The list row under the cursor, nil for none (the back button).
local focused_row = nil

local function number(name)
  return math.floor(ui.setting(name) or 0)
end

local function phase()
  return ui.setting("shop.phase") or ""
end

local function rows()
  return number("shop.rows")
end

local function pages()
  return math.max(1, math.ceil(rows() / PageSize))
end

local function row_of(slot)
  local row = (page - 1) * PageSize + slot - 1
  if row < rows() then
    return row
  end
  return nil
end

local function line_text()
  local key = ui.setting("shop.line_key") or ""
  if key == "" then
    return ""
  end
  return t(key, { bits = number("shop.money"), shopvar = number("shop.saved") })
end

-- The choice line holds the options, one per line ("Buy\nSell\nSee ya").
local function choice_count()
  return math.max(1, math.min(ChoiceCount, number("shop.choice_count")))
end

local function choice_labels()
  local labels = {}
  for label in string.gmatch(line_text(), "[^\n]+") do
    labels[#labels + 1] = label
  end
  local fallback = choice_count() < ChoiceCount and { t("shop.buy"), t("shop.leave") }
    or { t("shop.buy"), t("shop.sell"), t("shop.leave") }
  for choice = 1, ChoiceCount do
    labels[choice] = labels[choice] or fallback[choice]
  end
  return labels
end

local function extras()
  return ui.setting("shop_extras") == true
end

local function row_label(row)
  local prefix = "shop.row." .. row .. "."
  local name = t(ui.setting(prefix .. "name") or "")
  local price = number(prefix .. "price")
  if ui.setting("shop.mode") == "sell" then
    return t("shop.sale_row", { name = name, price = price, count = number(prefix .. "count") })
  end
  if extras() then
    return t(
      "shop.buy_row_owned",
      { name = name, price = price, owned = number(prefix .. "owned") }
    )
  end
  return t("shop.buy_row", { name = name, price = price })
end

-- A row's item description; its {species} placeholder shows the species whose name key comes
-- with it. Empty for no row.
local function description(row)
  if row == nil or row < 0 or row >= rows() then
    return ""
  end
  local prefix = "shop.row." .. row .. "."
  local key = ui.setting(prefix .. "description") or ""
  if key == "" then
    return ""
  end
  local species_key = ui.setting(prefix .. "description_species") or ""
  if species_key == "" then
    return t(key)
  end
  return t(key, { species = t(species_key) })
end

-- The description box pause opens (tickItemMenuDescriptionBox), and with shop extras the focused
-- item's description under the list.
local function draw_descriptions(document)
  local current = phase()
  local described = number("shop.described")
  local boxed = current == "description" and described >= 0
  document:get("description"):set_class("hidden", not boxed)
  document:get("description-text"):set_text(boxed and description(described) or "")
  local listed = extras() and (current == "list" or current == "description")
  local text = listed and description(focused_row) or ""
  document:get("list-description"):set_class("hidden", text == "")
  document:get("list-description"):set_text(text)
end

local function refresh_list(document)
  for slot = 1, PageSize do
    local row = row_of(slot)
    local entry = document:get("row-" .. slot)
    entry:set_class("hidden", row == nil)
    if row ~= nil then
      entry:set_text(row_label(row))
      entry:set_class("disabled", not ui.setting("shop.row." .. row .. ".enabled"))
      -- The partner's favourite food (the favorite_food option, an engine improvement).
      local favorite = ui.setting("shop.row." .. row .. ".favorite") == true
      entry:set_class("favorite", favorite and ui.setting("favorite_food") ~= false)
    end
  end
  local several = pages() > 1
  document:get("empty"):set_class("hidden", rows() > 0)
  document:get("previous"):set_class("hidden", not several)
  document:get("next-page"):set_class("hidden", not several)
  document:get("page"):set_class("hidden", not several)
  document:get("page"):set_text(t("shop.page", { page = page, pages = pages() }))
end

local function refresh_amount(document)
  document:get("amount-name"):set_text(t(ui.setting("shop.amount.name") or ""))
  document:get("amount-price"):set_text(t("shop.price", { price = number("shop.amount.price") }))
  document:get("amount-value"):set_text(t("shop.amount", {
    value = number("shop.amount.value"),
    max = number("shop.amount.max"),
  }))
  document:get("amount-total"):set_text(t("shop.total", { total = number("shop.amount.total") }))
  local shown = extras()
  document:get("amount-owned"):set_class("hidden", not shown)
  document:get("amount-after"):set_class("hidden", not shown)
  if shown then
    document:get("amount-owned"):set_text(t("shop.owned", { owned = number("shop.amount.owned") }))
    document:get("amount-after"):set_text(t("shop.money_after", {
      bits = number("shop.amount.money_after"),
    }))
  end
end

local function focus_phase(document, current)
  if current == "talk" then
    document:get("next"):focus()
  elseif current == "choice" then
    document:get("choice-1"):focus()
  elseif current == "list" then
    document:get(rows() > 0 and "row-1" or "list-back"):focus()
  elseif current == "amount" then
    document:get("amount-confirm"):focus()
  end
end

-- The money box (renderShopBitsBox): the line's first part on the first row, the money on the
-- second. It shows from the first choice until the farewell (shop.bits_shown).
local function draw_bits(document)
  local bits_key = ui.setting("shop.bits_key") or ""
  local money = number("shop.money")
  local bits = bits_key ~= "" and t(bits_key, { bits = money }) or t("shop.bits", { bits = money })
  local label, value = string.match(bits, "^(.-)%s*(%S+)$")
  document:get("bits-label"):set_text(label or bits)
  document:get("bits-value"):set_text(value or "")
  document:get("bits-box"):set_class("hidden", not ui.setting("shop.bits_shown"))
end

local function refresh(document)
  local current = phase()
  draw_bits(document)
  document:get("line"):set_text(line_text())
  document:get("line"):set_class("hidden", current == "choice")
  document:get("next"):set_class("hidden", current ~= "talk")
  document:get("choices"):set_class("hidden", current ~= "choice")
  -- The description box opens over the list, which stays up under it.
  document:get("list"):set_class("hidden", current ~= "list" and current ~= "description")
  document:get("amount"):set_class("hidden", current ~= "amount")
  if current == "choice" then
    local labels = choice_labels()
    for choice = 1, ChoiceCount do
      local entry = document:get("choice-" .. choice)
      entry:set_class("hidden", choice > choice_count())
      if choice <= choice_count() then
        entry:set_text(labels[choice])
      end
    end
  elseif current == "list" then
    if shown_phase ~= "list" and shown_phase ~= "amount" then
      page = 1
    end
    page = math.min(page, pages())
    refresh_list(document)
  elseif current == "amount" then
    refresh_amount(document)
  end
  draw_descriptions(document)
  if current ~= shown_phase and not (current == "list" and shown_phase == "description") then
    focus_phase(document, current)
  end
  shown_phase = current
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

function choose(choice)
  ui.send("shop_choice", choice - 1)
end

-- A row took the focus (slot 0: the back button): the game learns the row under the cursor, for
-- pause's description box.
function row_focus(document, slot)
  focused_row = slot > 0 and row_of(slot) or nil
  ui.send("shop_cursor", focused_row or -1)
  draw_descriptions(document)
end

function pick(slot)
  local row = row_of(slot)
  if row ~= nil then
    ui.send("shop_pick", row)
  end
end

function turn(document, step)
  page = ((page - 1 + step) % pages()) + 1
  refresh_list(document)
  document:get("row-1"):focus()
end

function on_show(document)
  shown_phase = ""
  page = 1
  focused_row = nil
  refresh(document)
  refresh_prompts(document)
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
  refresh_prompts(document)
end

function on_device(document)
  refresh_prompts(document)
end

-- Cancel goes back a step (a line goes on); in the amount box up and down change the amount by one
-- and left and right by the step; in the list left and right turn the page. In the list the game
-- takes pause (Start, which reaches the document as menu) for the description box, and confirm or
-- cancel close the box (tickItemMenuDescriptionBox); nothing else acts while it is up.
function on_action(document, action)
  local current = phase()
  if current == "description" then
    if action == "confirm" or action == "cancel" then
      ui.send("shop_back", true)
    end
    return true
  end
  -- In the list, pause (Start) arrives as menu too: the game tells it from the menu button and does
  -- what each asks (ShopScreen::press).
  if current == "list" and action == "menu" then
    return true
  end
  if action == "cancel" or action == "menu" then
    ui.send(current == "talk" and "shop_next" or "shop_back", true)
    return true
  end
  if current == "amount" then
    local step = number("shop.amount.step")
    local changes = { up = 1, down = -1, right = step, left = -step }
    if changes[action] ~= nil then
      ui.send("shop_amount", changes[action])
      return true
    end
  end
  if current == "list" and (action == "left" or action == "right") and pages() > 1 then
    turn(document, action == "left" and -1 or 1)
    return true
  end
  return false
end
