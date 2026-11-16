-- The improved shop job's counter (game/shop_job; docs/reference/trabajo-tienda.md): the game
-- publishes the customer at the counter (shop_haggle.*, _shopjob/ui-contract.md) and shows this
-- document once per turn. A row's button sends shop_haggle_offer with its level (0 the dearest, 8
-- the cheapest); Chat sends shop_haggle_talk and Dismiss shop_haggle_dismiss. The game hides the
-- document as soon as a turn is played; the story plays its lines and opens it again for the next
-- turn. The menu button opens the rules panel (shop_job_rules); cancel does nothing, as a customer
-- must be answered.
--
-- Classes for the skins: each row takes "offered" (refused before), "regular" (the regular price),
-- "likely" (a chance of 50 % or more), "sure" (100 %), "never" (0 %), "chance-<0..10>" (the chance
-- in tenths, for the row's meter) and "face-<reaction>" (the likely reaction); #haggle-mood takes
-- "mood-<name>", #haggle-reaction "face-<reaction>", each pip "on" while the customer has that
-- patience left, #haggle-talk "disabled" when it cannot chat, and #panel "talked" once it did.

local Rows = 9
local Pips = 6
local Offers = 8
-- The mood bubble's faces are 32 pixels wide, side by side (map gcan11, texture4/000).
local FaceWidth = 32
local Reactions = { "delighted", "pleased", "doubtful", "annoyed", "furious" }
local Moods = { "cheerful", "normal", "serious" }

local function setting(name, fallback)
  local value = ui.setting("shop_haggle." .. name)
  if value == nil then
    return fallback
  end
  return value
end

local function whole(name)
  return math.floor(setting(name, 0))
end

local function face_rect(face)
  return string.format("%d 0 %d %d", face * FaceWidth, FaceWidth, FaceWidth)
end

local function set_classes(element, prefix, names, chosen)
  for _, name in ipairs(names) do
    element:set_class(prefix .. name, name == chosen)
  end
end

local function draw_customer(document)
  document:get("haggle-title"):set_text(t("shop_job.haggle.title", {
    number = whole("customer"),
    total = whole("customers"),
  }))
  local species = setting("species", "")
  document:get("haggle-species"):set_text(species ~= "" and t(species) or "")
  document:get("haggle-character"):set_text(t(setting("character", "")))
  local mood = document:get("haggle-mood")
  mood:set_text(t("shop_job.haggle.mood", { mood = t(setting("mood", "")) }))
  set_classes(mood, "mood-", Moods, setting("mood_name", ""))
  local icon = document:get("haggle-species-icon")
  local icon_source = setting("species_icon", "")
  icon:set_class("hidden", icon_source == "")
  if icon_source ~= "" then
    icon:set_attribute("src", icon_source)
    icon:set_attribute("rect", setting("species_icon_rect", ""))
  end
  local face = whole("face")
  local reaction = setting("reaction", "")
  local face_image = document:get("haggle-face")
  face_image:set_class("hidden", face < 0)
  if face >= 0 then
    face_image:set_attribute("rect", face_rect(face))
  end
  local reaction_text = document:get("haggle-reaction")
  reaction_text:set_text(reaction ~= "" and t("shop_job.reaction." .. reaction) or "")
  set_classes(reaction_text, "face-", Reactions, reaction)
  local patience = whole("patience")
  local most = whole("patience_max")
  for pip = 1, Pips do
    local element = document:get("pip-" .. pip)
    element:set_class("hidden", pip > most)
    element:set_class("on", pip <= patience)
  end
end

local function draw_item(document)
  local item = setting("item", "")
  document
    :get("haggle-wants")
    :set_text(t("shop_job.haggle.wants", { item = item ~= "" and t(item) or "" }))
  document
    :get("haggle-regular")
    :set_text(t("shop_job.haggle.regular", { price = whole("regular_price") }))
  local icon = document:get("haggle-item-icon")
  local source = setting("item_icon", "")
  icon:set_class("hidden", source == "")
  if source ~= "" then
    icon:set_attribute("src", source)
    icon:set_attribute("rect", setting("item_icon_rect", ""))
  end
end

local function row_setting(row, name, fallback)
  local value = ui.setting("shop_haggle.row." .. row .. "." .. name)
  if value == nil then
    return fallback
  end
  return value
end

local function draw_rows(document)
  local regular = whole("regular_level")
  for row = 0, Rows - 1 do
    local id = "row-" .. (row + 1)
    local chance = math.floor(row_setting(row, "chance", 0))
    local reaction = row_setting(row, "reaction", "")
    document:get(id .. "-price"):set_text(t("shop_job.haggle.row_price", {
      price = math.floor(row_setting(row, "price", 0)),
    }))
    document:get(id .. "-adds"):set_text(t("shop_job.haggle.row_adds", {
      profit = math.floor(row_setting(row, "profit", 0)),
    }))
    document:get(id .. "-chance"):set_text(t("shop_job.haggle.row_chance", { chance = chance }))
    document
      :get(id .. "-face")
      :set_attribute("rect", face_rect(math.floor(row_setting(row, "face", 0))))
    local button = document:get(id)
    button:set_class("offered", row_setting(row, "offered", false) == true)
    button:set_class("regular", row == regular)
    button:set_class("likely", chance >= 50)
    button:set_class("sure", chance >= 100)
    button:set_class("never", chance <= 0)
    -- The chance in tenths (chance-0 none .. chance-10 sure, at least chance-1 above 0 %): the
    -- skins size the row's meter and colour its chance by it.
    local tenths = chance <= 0 and 0 or math.max(1, math.min(10, math.floor((chance + 5) / 10)))
    for band = 0, 10 do
      button:set_class("chance-" .. band, band == tenths)
    end
    set_classes(button, "face-", Reactions, reaction)
  end
end

local function draw_actions(document)
  local talked = setting("talked", false) == true
  local can_talk = setting("can_talk", false) == true
  document:get("panel"):set_class("talked", talked)
  document:get("haggle-talk"):set_class("disabled", not can_talk)
  document
    :get("haggle-talk-text")
    :set_text(t(talked and "shop_job.haggle.talked" or "shop_job.haggle.talk"))
end

local function draw_history(document)
  local count = whole("offers")
  document:get("haggle-no-offers"):set_class("hidden", count > 0)
  for slot = 1, Offers do
    local entry = document:get("offer-" .. slot)
    local shown = slot <= count
    entry:set_class("hidden", not shown)
    if shown then
      local base = "shop_haggle.offer." .. (slot - 1) .. "."
      local reaction = ui.setting(base .. "reaction") or ""
      document
        :get("offer-" .. slot .. "-face")
        :set_attribute("rect", face_rect(math.floor(ui.setting(base .. "face") or 0)))
      document:get("offer-" .. slot .. "-text"):set_text(t("shop_job.haggle.offer", {
        price = math.floor(ui.setting(base .. "price") or 0),
        reaction = reaction ~= "" and t("shop_job.reaction." .. reaction) or "",
      }))
      set_classes(entry, "face-", Reactions, reaction)
    end
  end
end

local function draw_summary(document)
  local tally = whole("tally")
  document
    :get("haggle-tally")
    :set_text(t("shop_job.haggle.tally", { tally = tally, goal = whole("goal") }))
  local joins = whole("joins")
  local bonus = whole("bonus")
  local next_mark = ""
  if tally < joins then
    next_mark = t("shop_job.haggle.next_joins", { tally = joins })
  elseif bonus > 0 and tally < bonus then
    next_mark = t("shop_job.haggle.next_bonus", { tally = bonus })
  end
  document:get("haggle-next"):set_text(next_mark)
end

local function refresh(document)
  draw_customer(document)
  draw_item(document)
  draw_rows(document)
  draw_actions(document)
  draw_history(document)
  draw_summary(document)
end

local function refresh_prompts(document)
  document:get("prompt-rules"):set_prompt("menu")
end

-- The row a turn starts on: the regular price, or the next one not refused yet.
local function focus_row(document)
  local row = whole("regular_level")
  for step = 0, Rows - 1 do
    local candidate = (row + step) % Rows
    if row_setting(candidate, "offered", false) ~= true then
      row = candidate
      break
    end
  end
  document:get("row-" .. (row + 1)):focus()
end

function offer(_document, level)
  ui.send("shop_haggle_offer", level)
end

function talk(_document)
  if setting("can_talk", false) == true then
    ui.send("shop_haggle_talk")
  end
end

function dismiss(_document)
  ui.send("shop_haggle_dismiss")
end

function on_show(document)
  refresh(document)
  refresh_prompts(document)
  focus_row(document)
end

function on_setting(document, name)
  if name == "shop_haggle.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
  refresh_prompts(document)
end

function on_device(document)
  refresh_prompts(document)
end

-- Up, down, left and right move between the rows and the buttons, confirm presses the one in focus;
-- menu opens the rules; cancel does nothing.
function on_action(_document, action)
  if action == "menu" then
    ui.send("shop_job_rules")
    return true
  end
  return action == "cancel"
end
