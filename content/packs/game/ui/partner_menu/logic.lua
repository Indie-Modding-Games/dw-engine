-- The partner's menu. The game publishes the partner's status and the bag as settings
-- (apps/screens: partner_status_view.hpp, bag_view.hpp) and bumps partner.revision and bag.revision
-- once per change; this script draws them and sends the player's choices with ui.send. What an item
-- does, whether the partner takes it, how the bag sorts: all of it is the game's.

local pages = { "status", "tech", "items", "care" }
local first_entry =
  { status = "tab-status", tech = "tech-change", items = "slot-0", care = "care-praise" }
local max_slots = 30
local pool_bars = 2
local stat_bars = 4
local meters = 3
local max_conditions = 7

-- The original's labels: the item options (main.item_menu_labels: use, move, sort, drop), the sort
-- orders (main.inventory_sort_labels), the drop's question (main.inventory_confirm_labels) and
-- bits.
local option_names = { "use", "move", "sort", "drop" }
local sort_orders = 3
local bits_label_key = "main.battle_ui_bits_labels.0000"

local function original_label(block, slot)
  return t(string.format("main.%s.%04d", block, slot))
end

local current = "status"
local editing_tech = false
local tech_icon_time = 0
local tech_info_focus = nil
local tech_info_revision = nil
local status_condition_bob = nil
-- The open box over the bag (options, sort-box, confirm-box) and its entries, or nil.
local box = nil
local box_parent = nil
local box_entries = {
  options = { "opt-use", "opt-move", "opt-sort", "opt-drop" },
  ["sort-box"] = { "sort-0", "sort-1", "sort-2" },
  ["confirm-box"] = { "confirm-yes", "confirm-no" },
}
-- The slot the options are for, and the slot being moved (move picks its destination next).
local picked = nil
local moving = nil
local row_offset = 0

local function setting(name, fallback)
  local value = ui.setting(name)
  if value == nil then
    return fallback
  end
  return value
end

local function number(name)
  return math.floor(setting(name, 0))
end

local function text(key, arguments)
  if key == nil or key == "" then
    return ""
  end
  return t(key, arguments)
end

local function percent(value, minimum, maximum)
  if maximum <= minimum then
    return 0
  end
  return math.max(0, math.min(100, (value - minimum) * 100 / (maximum - minimum)))
end

local function slot_setting(index, field)
  return "bag.slot." .. index .. "." .. field
end

local function slot_filled(index)
  return index < number("bag.slot.count") and not setting(slot_setting(index, "empty"), true)
end

local function draw_bar(document, prefix, list, index)
  local base = list .. "." .. index .. "."
  local value = number(base .. "value")
  document:get(prefix .. "-label"):set_text(text(setting(base .. "label", "")))
  document:get(prefix):set_text(tostring(value))
  document
    :get(prefix .. "-fill")
    :set_property("width", percent(value, number(base .. "min"), number(base .. "max")) .. "%")
end

-- A care meter. A two-tone one fills the bar from the minimum to the middle of its range, then
-- fills it again in its second colour from the middle to the maximum (game_menu.c:
-- renderDigimonStatsView: happiness, discipline).
local function draw_meter(document, index)
  local prefix = "meter-" .. index
  local base = "partner.meter." .. index .. "."
  draw_bar(document, prefix, "partner.meter", index)
  local high = 0
  if setting(base .. "two_tone", false) then
    local value, low, top = number(base .. "value"), number(base .. "min"), number(base .. "max")
    local middle = (low + top) / 2
    document:get(prefix .. "-fill"):set_property("width", percent(value, low, middle) .. "%")
    high = percent(value, middle, top)
  end
  if ui.setting("ui_skin") == "original" then
    local value, low, top = number(base .. "value"), number(base .. "min"), number(base .. "max")
    local pink = percent(value, low, (low + top) / 2)
    if index == 2 then
      document
        :get(prefix .. "-fill")
        :set_property("width", math.max(0, math.min(150, value * 9)) .. "dp")
    else
      document:get(prefix .. "-fill"):set_property("width", math.floor(pink / 2) * 3 .. "dp")
    end
    document:get(prefix .. "-high"):set_property("width", math.floor(high / 2) * 3 .. "dp")
    return
  end
  document:get(prefix .. "-high"):set_property("width", high .. "%")
end

local function draw_status(document)
  -- The partner's own name when the player gave one, else its species' (PARTNER_ENTITY.name); the
  -- species on its own line.
  local species = text(setting("partner.name", ""))
  local nickname = setting("partner.nickname", "")
  document:get("status-name"):set_text(nickname ~= "" and nickname or species)
  document:get("status-species"):set_text(species)
  document
    :get("status-lives")
    :set_text(t("partner_menu.lives", { lives = number("partner.lives") }))
  local original = ui.setting("ui_skin") == "original"
  document:get("status-age"):set_text(
    original and tostring(number("partner.age"))
      or t("partner_menu.age.value", { days = number("partner.age") })
  )
  document:get("status-weight"):set_text(
    original and tostring(number("partner.weight"))
      or t("partner_menu.weight.value", { weight = number("partner.weight") })
  )
  for index = 0, 2 do
    local icon = document:get("status-special-" .. index)
    local value = math.floor(setting("partner.profile.special." .. index, -1))
    icon:set_class("hidden", value < 0 or value > 6)
    for special = 0, 6 do
      icon:set_class("profile-special-" .. special, value == special)
    end
  end
  local kind = number("partner.profile.type")
  document:get("status-type"):set_class("hidden", kind < 1 or kind > 3)
  for value = 1, 3 do
    document:get("status-type"):set_class("profile-type-" .. value, kind == value)
  end
  local pattern = math.floor(setting("partner.profile.active", -1))
  local activity = ""
  if pattern >= 0 and pattern < 5 then
    local labels = original_label("status_view_labels", 2)
    local joined = labels:gsub("%s+", "")
    local starts, widths = { 1, 6, 9, 15, 21 }, { 5, 3, 6, 6, 6 }
    if #labels == 36 and #joined == 26 then
      local start = starts[pattern + 1]
      activity = string.sub(joined, start, start + widths[pattern + 1] - 1)
    end
  elseif pattern == 5 then
    activity = string.sub(original_label("status_view_labels", 1), -5)
  end
  document:get("status-active"):set_text(activity)
  local lives = math.max(0, math.min(3, number("partner.lives")))
  for index = 0, 2 do
    document:get("life-fill-" .. index):set_class("hidden", index >= lives)
  end
  local words = {}
  for word in string.gmatch(original_label("status_view_labels", 1), "%S+") do
    words[#words + 1] = word
  end
  document:get("status-life-label"):set_text(words[2] or "")
  -- HP and MP: the numbers, now and at most, and a bar of the maximum (game_menu.c:
  -- renderDigimonStatsView).
  local pool_now = { number("partner.hp"), number("partner.mp") }
  for index = 0, pool_bars - 1 do
    draw_bar(document, "pool-" .. index, "partner.pool", index)
    local maximum = number("partner.pool." .. index .. ".value")
    document:get("pool-" .. index):set_text(
      original and tostring(pool_now[index + 1]) or (pool_now[index + 1] .. " / " .. maximum)
    )
    document:get("pool-" .. index .. "-max"):set_text(tostring(maximum))
  end
  for index = 0, stat_bars - 1 do
    draw_bar(document, "stat-" .. index, "partner.stat", index)
  end
  for index = 0, meters - 1 do
    draw_meter(document, index)
  end
  document:get("meter-0-label"):set_class("negative", number("partner.meter.0.value") < 0)
  document:get("meter-1-label"):set_class("low", number("partner.meter.1.value") < 50)
  if ui.setting("ui_skin") == "original" then
    document:get("meter-1-label"):set_text(words[1] or "")
    document:get("meter-2-label"):set_text(words[3] or "")
  end
  local count = number("partner.condition.count")
  for index = 0, max_conditions - 1 do
    local condition = document:get("condition-" .. index)
    condition:set_class("hidden", index >= count)
    if index < count then
      local key = setting("partner.condition." .. index .. ".key", "")
      condition:set_text(text(key))
      for _, name in ipairs({
        "sleepy",
        "tired",
        "hungry",
        "needs_toilet",
        "sulking",
        "injured",
        "sick",
      }) do
        condition:set_class("profile-condition-" .. name, key == "partner_menu.condition." .. name)
      end
    end
  end
  document:get("no-condition"):set_class("hidden", count > 0)
end

-- Las abreviaturas se leen de las tablas del disco local, sin duplicar sus textos en el pack.
local function tech_value_label(field, value)
  if value <= 0 then
    return ""
  end
  if ui.setting("ui_skin") == "original" then
    if field == "range" then
      local labels = original_label("tech_view_labels", 0)
      local entries = {}
      for label in string.gmatch(labels, "%S+") do
        entries[#entries + 1] = label
      end
      if #entries == 4 and entries[value] ~= nil then
        return entries[value]
      end
    elseif field == "effect" then
      local labels = original_label("status_view_labels", 6)
      if #labels == 16 then
        return string.sub(labels, (value - 1) * 4 + 1, value * 4)
      end
    end
  end
  return t("partner_menu.tech." .. field .. "." .. value)
end

local function draw_tech(document)
  if ui.setting("ui_skin") == "original" then
    -- Use the disc's control glyphs: their bitmap meanings need not match Unicode shapes.
    document
      :get("tech-change-icon")
      :set_text(string.match(original_label("status_view_labels", 5), "^(%S+)") or "")
    document
      :get("tech-cancel-icon")
      :set_text(string.match(original_label("status_view_labels", 7), "^(%S+)") or "")
  end
  for index = 0, 3 do
    local base = "partner.technique." .. index .. "."
    local filled = not setting(base .. "empty", true)
    document:get("tech-name-" .. index):set_text(filled and text(setting(base .. "name", "")) or "")
    for _, field in ipairs({ "power", "mp" }) do
      document
        :get("tech-" .. field .. "-" .. index)
        :set_text(filled and tostring(number(base .. field)) or "")
    end
    for _, field in ipairs({ "range", "effect" }) do
      local value = number(base .. field)
      document
        :get("tech-" .. field .. "-" .. index)
        :set_text(filled and value > 0 and tech_value_label(field, value) or "")
    end
    for effect = 1, 4 do
      document
        :get("tech-effect-" .. index)
        :set_class("effect-" .. effect, filled and number(base .. "effect") == effect)
    end
  end
end

local function draw_tech_info(document)
  for index = 0, 55 do
    if document:get("tech-choice-" .. index):has_focus() then
      local revision = ui.setting("partner.revision")
      if tech_info_focus == index and tech_info_revision == revision then
        return
      end
      tech_info_focus, tech_info_revision = index, revision
      local base = "partner.technique.choice." .. index .. "."
      local known = setting(base .. "known", false)
      document:get("tech-info"):set_class("info-right", index % 8 <= 2)
      document:get("tech-info-name"):set_text(known and text(setting(base .. "name", "")) or "")
      for _, field in ipairs({ "power", "mp", "range", "effect" }) do
        local value = number(base .. field)
        local label = ""
        if known then
          if field == "power" or field == "mp" then
            label = tostring(value)
          elseif value > 0 then
            label = tech_value_label(field, value)
          end
        end
        document:get("tech-info-" .. field):set_text(label)
      end
      return
    end
  end
end

local function draw_tech_choices(document)
  for index = 0, 55 do
    local base = "partner.technique.choice." .. index .. "."
    local button = document:get("tech-choice-" .. index)
    local label = text(setting(base .. "name", ""))
    document:get("tech-cell-" .. index):set_text(label ~= "" and label or "-")
    button:set_class("known", setting(base .. "known", false))
    button:set_class("compatible", number(base .. "slot") >= 0)
    button:set_class("equipped", setting(base .. "equipped", false))
    for special = 0, 6 do
      button:set_class("special-" .. special, number(base .. "special") == special)
    end
    button:set_class("unavailable", not setting(base .. "available", false))
  end
end

local function finish_tech_edit(document)
  if not setting("partner.technique.can_finish", false) then
    document:get("tech-message"):set_text(t("partner_menu.tech.need_attack"))
    return
  end
  editing_tech = false
  document:get("tech-info"):set_class("hidden", true)
  document:get("scrim"):set_class("tech-editing", false)
  document:get("tech-picker"):set_class("hidden", true)
  document:get("tech-message"):set_text("")
  document:get("tech-change"):focus()
end

function start_tech_edit(document)
  local available = false
  for slot = 0, 55 do
    available = available or setting("partner.technique.choice." .. slot .. ".available", false)
  end
  if not available then
    document:get("tech-message"):set_text(t("partner_menu.tech.unavailable"))
    return
  end
  editing_tech = true
  document:get("tech-info"):set_class("hidden", false)
  document:get("scrim"):set_class("tech-editing", true)
  document:get("tech-picker"):set_class("hidden", false)
  document:get("tech-message"):set_text("")
  draw_tech_choices(document)
  document:get("tech-choice-0"):focus()
end

function toggle_technique(document, slot)
  if not setting("partner.technique.choice." .. slot .. ".available", false) then
    document:get("tech-message"):set_text(t("partner_menu.tech.unavailable"))
    return
  end
  ui.send("partner_toggle_technique", number("partner.technique.choice." .. slot .. ".slot"))
end

local function draw_bag(document)
  local count = number("bag.slot.count")
  local original = ui.setting("ui_skin") == "original"
  local rows = count > 10 and 9 or 5
  row_offset = math.max(0, math.min(row_offset, math.ceil(count / 2) - rows))
  local total_rows = math.ceil(count / 2)
  local scrollbar = document:get("bag-scrollbar")
  scrollbar:set_class("hidden", not original or total_rows <= rows)
  if original and total_rows > rows then
    local track_height = 182 - 25
    local thumb_height = math.floor(track_height * rows / total_rows)
    local thumb_top = math.floor((track_height - thumb_height) * row_offset / (total_rows - rows))
    document:get("bag-scroll-thumb"):set_property("height", ((thumb_height + 1) * 3) .. "dp")
    document:get("bag-scroll-thumb"):set_property("top", (thumb_top * 3) .. "dp")
  end
  for index = 0, max_slots - 1 do
    local slot = document:get("slot-" .. index)
    slot:set_class(
      "hidden",
      index >= count or (original and (index < row_offset * 2 or index >= (row_offset + rows) * 2))
    )
    if index < count then
      local filled = slot_filled(index)
      document:get("slot-" .. index .. "-name"):set_text(
        filled and text(setting(slot_setting(index, "name"), ""))
          or (ui.setting("ui_skin") == "original" and "" or t("partner_menu.bag.empty"))
      )
      document
        :get("slot-" .. index .. "-count")
        :set_text(filled and tostring(number(slot_setting(index, "count"))) or "")
      slot:set_class("dimmed", filled and setting(slot_setting(index, "dimmed"), false))
      local color = setting(slot_setting(index, "item_color"), 255)
      slot:set_class("item-field", filled and color == 0)
      slot:set_class("item-battle", filled and color == 1)
      slot:set_class("item-shared", filled and color == 2)
      -- The item's icon in the gap before its name (utils2.c: setItemTexture).
      local icon = document:get("slot-" .. index .. "-icon")
      local source = filled and setting(slot_setting(index, "icon"), "") or ""
      icon:set_class("hidden", source == "")
      if source ~= "" then
        icon:set_attribute("src", source)
        icon:set_attribute("rect", setting(slot_setting(index, "icon_rect"), ""))
      end
      slot:set_class("moving", moving == index)
    end
  end
  document:get("bag-money"):set_text(t(bits_label_key) .. " " .. number("bag.money"))
  local result = setting("bag.result", "")
  document:get("bag-result"):set_text(text(result, { name = text(setting("bag.result_item", "")) }))
end

local function focus_slot(document, index)
  if ui.setting("ui_skin") == "original" then
    local row = math.floor(index / 2)
    local rows = number("bag.slot.count") > 10 and 9 or 5
    if row < row_offset then
      row_offset = row
    elseif row >= row_offset + rows then
      row_offset = row - rows + 1
    end
    draw_bag(document)
  end
  document:get("slot-" .. index):focus()
end

-- The description of the focused item, below the list.
local function draw_description(document)
  local description = ""
  if moving ~= nil then
    description =
      t("partner_menu.bag.move_prompt", { name = text(setting(slot_setting(moving, "name"), "")) })
  elseif box ~= nil and picked ~= nil and slot_filled(picked) then
    -- A box over the bag keeps the description of the item it was opened on, as the original's
    -- options box does.
    description = text(setting(slot_setting(picked, "description"), ""))
  else
    for index = 0, number("bag.slot.count") - 1 do
      if document:get("slot-" .. index):has_focus() and slot_filled(index) then
        description = text(setting(slot_setting(index, "description"), ""))
      end
    end
  end
  document:get("bag-description"):set_text(description)
end

local function draw_care(document)
  document:get("care-result"):set_text(text(setting("partner.care_result", "")))
end

local function draw_boxes(document)
  for index, name in ipairs(option_names) do
    document:get("opt-" .. name):set_text(original_label("item_menu_labels", index - 1))
  end
  for index = 0, sort_orders - 1 do
    document:get("sort-" .. index):set_text(original_label("inventory_sort_labels", index))
  end
  document:get("confirm-text"):set_text(original_label("inventory_confirm_labels", 0))
  document:get("confirm-yes"):set_text(original_label("inventory_confirm_labels", 1))
  document:get("confirm-no"):set_text(original_label("inventory_confirm_labels", 2))
end

local function draw_prompts(document)
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
  document:get("prompt-cancel"):set_text(ui.prompt("cancel"))
end

local function draw(document)
  draw_status(document)
  draw_tech(document)
  draw_tech_choices(document)
  draw_bag(document)
  draw_description(document)
  draw_care(document)
end

local function close_box(document, focus)
  if box ~= nil then
    document:get(box):set_class("hidden", true)
    box = nil
  end
  if box_parent ~= nil then
    document:get(box_parent):set_class("hidden", true)
    box_parent = nil
  end
  document:get("scrim"):set_class("sub-box-open", false)
  for _, name in ipairs(option_names) do
    document:get("opt-" .. name):set_class("active-option", false)
  end
  if focus ~= nil then
    document:get(focus):focus()
  end
end

local function back_box(document)
  if box_parent ~= nil then
    local child = box
    document:get(child):set_class("hidden", true)
    box = box_parent
    box_parent = nil
    document:get("scrim"):set_class("sub-box-open", false)
    for _, name in ipairs(option_names) do
      document:get("opt-" .. name):set_class("active-option", false)
    end
    document:get(child == "sort-box" and "opt-sort" or "opt-drop"):focus()
  else
    close_box(document, "slot-" .. picked)
  end
end

local function open_box(document, name)
  if name == "options" then
    local filled = picked ~= nil and slot_filled(picked)
    document
      :get("opt-use")
      :set_class("unavailable", not filled or not setting(slot_setting(picked, "usable"), false))
    document
      :get("opt-drop")
      :set_class(
        "unavailable",
        not filled or not setting(slot_setting(picked, "discardable"), false)
      )
  end
  local parent = ui.setting("ui_skin") == "original"
      and box == "options"
      and name ~= "options"
      and "options"
    or nil
  if parent == nil then
    close_box(document)
  else
    for _, option in ipairs(option_names) do
      local element = document:get("opt-" .. option)
      element:set_class("active-option", element:has_focus())
    end
  end
  box_parent = parent
  document:get("scrim"):set_class("sub-box-open", parent ~= nil)
  box = name
  document:get(name):set_class("hidden", false)
  document:get(box_entries[name][1]):focus()
  -- createItemOption: the options grow from the slot's row.
  if name == "options" and parent == nil and picked ~= nil then
    original_box.open(document, "options", original_box.rect(document, "slot-" .. picked))
  end
end

local function show_page(document, name, focus)
  close_box(document)
  moving = nil
  row_offset = 0
  for _, page in ipairs(pages) do
    document:get("page-" .. page):set_class("hidden", page ~= name)
    document:get("tab-" .. page):set_class("selected", page == name)
  end
  current = name
  document:get("scrim"):set_class(
    "digimon-screen",
    (name == "status" or name == "tech") and ui.setting("ui_skin") == "original"
  )
  document
    :get("scrim")
    :set_class("item-screen", name == "items" and ui.setting("ui_skin") == "original")
  document:get("scrim"):set_class("large-bag", number("bag.slot.count") > 10)
  draw(document)
  document:get(focus or first_entry[name]):focus()
end

function open_page(document, name)
  if editing_tech then
    return
  end
  show_page(document, name)
end

function pick_slot(document, index)
  if moving ~= nil then
    local from = moving
    moving = nil
    if from ~= index then
      ui.send("bag_move", from .. " " .. index)
    end
    draw_bag(document)
    return
  end
  if not slot_filled(index) and ui.setting("ui_skin") ~= "original" then
    return
  end
  picked = index
  document:get("scrim"):set_class("item-column-right", index % 2 == 1)
  for row = 0, 8 do
    document:get("scrim"):set_class("option-row-" .. row, math.floor(index / 2) - row_offset == row)
  end
  open_box(document, "options")
end

function choose_option(document, name)
  if (name == "use" or name == "drop") and not slot_filled(picked) then
    return
  end
  if name == "use" and not setting(slot_setting(picked, "usable"), false) then
    return
  end
  if name == "drop" and not setting(slot_setting(picked, "discardable"), false) then
    return
  end
  local slot = "slot-" .. picked
  if name == "use" then
    close_box(document, slot)
    ui.send("bag_use", picked)
  elseif name == "move" then
    moving = picked
    close_box(document, slot)
    draw_bag(document)
  elseif name == "sort" then
    open_box(document, "sort-box")
  elseif name == "drop" then
    open_box(document, "confirm-box")
    document:get("confirm-no"):focus()
  end
end

function choose_sort(document, mode)
  close_box(document)
  focus_slot(document, 0)
  ui.send("bag_sort", mode)
end

function confirm_drop(document, yes)
  if not yes then
    back_box(document)
    return
  end
  close_box(document, "slot-" .. picked)
  if yes then
    ui.send("bag_drop", picked)
  end
end

-- The original's boxes (ui/common/original_box.lua): the bag and the partner's page grow from the
-- player and shrink back into it (inventory.c: createInventoryView; game_menu.c: createMenuBox);
-- the bag's description box grows from its first slot once the bag is in place (16 x 16 pixels at
-- 8, 14 of the bag) and closes first, into the slot under the cursor (inventory.c:
-- closeInventoryBoxes).
local function main_box()
  return current == "items" and "bag-grid" or "page-" .. current
end

local function slot_start(document, index)
  local left, top = document:get("bag-grid"):rect()
  if index ~= nil then
    local slot = document:get("slot-" .. index)
    if slot ~= nil and slot:valid() then
      left, top = slot:rect()
      return { left, top, 48, 48 }
    end
  end
  return { left + 24, top + 42, 48, 48 }
end

local function open_boxes(document)
  if current ~= "items" then
    original_box.open(document, main_box(), original_box.player_rect(document))
    return
  end
  document:get("bag-description"):set_class("box-closed", original_box.enabled())
  original_box.open(document, "bag-grid", original_box.player_rect(document), function()
    original_box.open(document, "bag-description", slot_start(document))
  end)
end

-- Back: out of a box, out of a move, from a page to its tab, from the tabs out of the menu.
function close_menu(document)
  local function leave()
    ui.hide(document_name)
    if ui.setting("ui_skin") == "original" then
      ui.show("pause_menu", true)
    end
  end
  if document == nil or not original_box.enabled() then
    leave()
    return
  end
  local function close_main()
    original_box.close(document, main_box(), original_box.player_rect(document), leave)
  end
  if current == "items" then
    original_box.close(document, "bag-description", slot_start(document, picked or 0), close_main)
  else
    close_main()
  end
end

local function back(document)
  if editing_tech then
    finish_tech_edit(document)
  elseif box ~= nil then
    back_box(document)
  elseif moving ~= nil then
    local index = moving
    moving = nil
    focus_slot(document, index)
  elseif ui.setting("ui_skin") == "original" then
    close_menu(document)
  elseif not document:get("tab-" .. current):has_focus() then
    document:get("tab-" .. current):focus()
  else
    close_menu(document)
  end
end

-- Up and down go through the entries of an open box only.
local function step_box(document, step)
  local entries = box_entries[box]
  local at = 1
  for index, id in ipairs(entries) do
    if document:get(id):has_focus() then
      at = index
    end
  end
  local next_index = ui.setting("ui_skin") == "original"
      and math.max(1, math.min(#entries, at + step))
    or ((at - 1 + step) % #entries) + 1
  document:get(entries[next_index]):focus()
end

function on_show(document)
  status_condition_bob = nil
  tech_info_focus = nil
  editing_tech = false
  document:get("tech-info"):set_class("hidden", true)
  document:get("scrim"):set_class("tech-editing", false)
  document:get("tech-picker"):set_class("hidden", true)
  document:get("tech-message"):set_text("")
  box = nil
  box_parent = nil
  document:get("scrim"):set_class("sub-box-open", false)
  moving = nil
  for name, _ in pairs(box_entries) do
    document:get(name):set_class("hidden", true)
  end
  draw_boxes(document)
  draw_prompts(document)
  show_page(document, ui.setting("partner_menu.start_page") == "items" and "items" or "status")
  open_boxes(document)
end

function on_hide(document)
  original_box.cancel(document)
end

function on_setting(document, name)
  if
    name == "partner.revision"
    or name == "bag.revision"
    or name == "bag.result"
    or name == "partner.care_result"
    or name == "partner.technique.result"
  then
    draw(document)
    if editing_tech then
      document:get("tech-message"):set_text(text(setting("partner.technique.result", "")))
    end
  end
end

function on_tick(document, seconds)
  original_box.tick(document, seconds)
  tech_icon_time = tech_icon_time + seconds
  if current == "status" and ui.setting("ui_skin") == "original" then
    document:get("scrim"):set_class("status-alternate", math.floor(tech_icon_time * 20) % 10 >= 5)
    local phase = math.floor(tech_icon_time * 20) % 16
    local bob = (phase < 3 or phase >= 13) and 130
      or (phase < 5 or phase >= 11) and 128
      or (phase < 7 or phase >= 9) and 125
      or 121
    if status_condition_bob ~= bob then
      status_condition_bob = bob
      document:get("conditions"):set_property("top", ((bob - 32) * 3) .. "dp")
    end
  end
  if editing_tech then
    draw_tech_info(document)
    document:get("tech-picker"):set_class("alternate", math.floor(tech_icon_time * 20) % 20 >= 10)
  end
  if current == "items" then
    draw_description(document)
  end
end

function on_device(document)
  draw_prompts(document)
end

function on_language(document)
  tech_info_focus = nil
  draw_boxes(document)
  draw(document)
  draw_prompts(document)
end

function on_action(document, action)
  -- tickUIBox: a box takes no input until it is in place.
  if original_box.busy() then
    return true
  end
  if action == "cancel" then
    back(document)
    return true
  end
  if action == "menu" then
    if editing_tech then
      finish_tech_edit(document)
      return true
    end
    ui.hide(document_name)
    return true
  end
  if box ~= nil then
    if box == "confirm-box" and ui.setting("ui_skin") == "original" then
      if action == "left" then
        document:get("confirm-yes"):focus()
        return true
      end
      if action == "right" then
        document:get("confirm-no"):focus()
        return true
      end
      if action == "up" or action == "down" then
        return true
      end
    end
    if action == "up" or action == "down" then
      step_box(document, action == "up" and -1 or 1)
      return true
    end
    return action == "left" or action == "right"
  end
  if editing_tech then
    for index = 0, 55 do
      if document:get("tech-choice-" .. index):has_focus() then
        local target = index
        if action == "left" and index % 8 > 0 then
          target = index - 1
        elseif action == "right" and index % 8 < 7 then
          target = index + 1
        elseif action == "up" and index >= 8 then
          target = index - 8
        elseif action == "down" and index < 48 then
          target = index + 8
        end
        if target ~= index then
          document:get("tech-choice-" .. target):focus()
        end
        if action == "left" or action == "right" or action == "up" or action == "down" then
          return true
        end
      end
    end
    return false
  end
  if (current == "status" or current == "tech") and ui.setting("ui_skin") == "original" then
    if action == "right" then
      show_page(document, "tech")
      return true
    end
    if action == "left" then
      show_page(document, "status")
      return true
    end
    if action == "up" or action == "down" then
      return true
    end
  end
  if
    current == "items"
    and ui.setting("ui_skin") == "original"
    and (action == "up" or action == "down" or action == "left" or action == "right")
  then
    for index = 0, number("bag.slot.count") - 1 do
      if document:get("slot-" .. index):has_focus() then
        local next_index = index
        if action == "up" and index >= 2 then
          next_index = index - 2
        elseif action == "down" and index < number("bag.slot.count") - 2 then
          next_index = index + 2
        elseif action == "left" and index % 2 == 1 then
          next_index = index - 1
        elseif action == "right" and index % 2 == 0 and index + 1 < number("bag.slot.count") then
          next_index = index + 1
        end
        focus_slot(document, next_index)
        return true
      end
    end
    return true
  end
  return false
end
