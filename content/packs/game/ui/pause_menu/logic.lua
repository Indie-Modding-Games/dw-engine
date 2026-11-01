-- The pause menu: main, player, minimap and options. The menu never changes a setting itself: it
-- sends a command to the game with ui.send and shows what the game reports back through ui.setting,
-- so the labels always tell the truth. Every text the player sees comes from the texts in the
-- player's language: t(key) here, text-key in the document.

local pages = { "main", "minimap", "options", "player" }
local first_entry = {
  main = "continue",
  minimap = "minimap-mode",
  options = "display-mode",
  player = "player-minimap",
}
-- The entry of the main page that opened each page, focused again when going back.
local opener = { minimap = "open-minimap", options = "open-options", player = "open-player" }

local current = "main"
local parents = {}
local icon_time = 0
local icon_flash = false
local player_tools_open = false
local player_tab = "player"
local chart_selecting = false
local chart_detail = false
local chart_transition = nil
local medals_selecting = false
local medal_index = 0
local cards_selecting = false
local card_index = 0
local card_open = false
local card_palettes = {}
local chart_index = 0
local chart_cells = {}
local chart_signature = ""
local chart_flash = false

local minimap_sizes = { "small", "medium", "large" }
-- Whether the HUD shows the minimap: as the skin decides (auto), always (on) or never (off).
local minimap_modes = { "auto", "on", "off" }
local ui_scales = { 0.75, 0.9, 1.0, 1.15, 1.3, 1.5, 1.75, 2.0 }

local function on_off(value)
  return t(value and "common.yes" or "common.no")
end

-- A list the game publishes as "0,2,1": the value of each position, from 0.
local function number_list(name)
  local values = {}
  local position = 0
  for value in string.gmatch(ui.setting(name) or "", "%d+") do
    values[position] = tonumber(value)
    position = position + 1
  end
  return values
end

local function index_of(list, value)
  for index, item in ipairs(list) do
    if item == value then
      return index
    end
  end
  return nil
end

-- The nearest UI scale step to a value, so a scale set elsewhere still cycles from the right place.
local function nearest_scale(value)
  local best = 3
  for index, scale in ipairs(ui_scales) do
    if math.abs(scale - value) < math.abs(ui_scales[best] - value) then
      best = index
    end
  end
  return best
end

local function cycle(list, index, step)
  return ((index - 1 + step) % #list) + 1
end

-- What each option entry does when confirmed or moved left and right.
local options = {
  ["minimap-mode"] = function(step)
    local index = index_of(minimap_modes, ui.setting("minimap")) or 1
    ui.send("minimap", minimap_modes[cycle(minimap_modes, index, step)])
  end,
  ["minimap-size"] = function(step)
    local index = index_of(minimap_sizes, ui.setting("minimap_size")) or 2
    ui.send("minimap_size", minimap_sizes[cycle(minimap_sizes, index, step)])
  end,
  ["display-mode"] = function(_step)
    ui.send("fullscreen", not ui.setting("fullscreen"))
  end,
  ["ui-scale"] = function(step)
    local index = nearest_scale(ui.setting("ui_scale") or 1.0)
    ui.send("ui_scale", ui_scales[math.max(1, math.min(#ui_scales, index + step))])
  end,
  ["vsync"] = function(_step)
    ui.send("vsync", not ui.setting("vsync"))
  end,
  ["ui-skin"] = function(step)
    skin_choice.change(step)
  end,
  -- Free exploration: the scripts' map changes go anywhere they can, whatever the story (off by
  -- default).
  ["free-exploration"] = function(_step)
    ui.send("free_exploration", not ui.setting("free_exploration"))
  end,
  -- Smooth motion: the characters and the camera glide between the original's frames (on by
  -- default; off draws them where each frame leaves them, as the original).
  ["smooth-motion"] = function(_step)
    ui.send("smooth_motion", not ui.setting("smooth_motion"))
  end,
  -- Save notice: a small figure and "Saving" in a corner while the game saves (on by default).
  ["save-notice"] = function(_step)
    ui.send("save_notice", not ui.setting("save_notice"))
  end,
  -- Time in digits: the game time as HH:MM under the HUD's clock (on by default).
  ["digital-time"] = function(_step)
    ui.send("digital_time", not ui.setting("digital_time"))
  end,
}

local function can_sleep()
  return ui.setting("pause_menu.sleep_enabled") == true
end

-- main/fish.c: getFishingAvailability; 0 off a fishing map, 1 without a rod or bait, 2 ready.
local function fishing()
  return math.floor(ui.setting("pause_menu.fishing") or 0)
end

local function draw_date_digits(document, prefix, count, value)
  local text = tostring(math.max(0, math.floor(tonumber(value) or 0)))
  for index = 1, count do
    local digit = text:sub(index, index)
    local element = document:get(prefix .. index)
    element:set_class("hidden", digit == "")
    element:set_text(digit)
    for number = 0, 9 do
      element:set_class("digit-" .. number, digit == tostring(number))
    end
  end
end

local function chart_cursor(document)
  local cell = chart_cells[chart_index + 1]
  document:get("chart-cursor"):set_class("hidden", not chart_selecting or cell == nil)
  if not cell then
    return
  end
  local cursor = document:get("chart-cursor")
  cursor:set_property("left", ((cell.x - 15) * 3) .. "dp")
  cursor:set_property("top", ((cell.y - 33) * 3) .. "dp")
end

local function refresh_chart(document)
  local count = math.min(61, tonumber(ui.setting("player.chart.count")) or 0)
  local raised_text = ui.setting("player.chart.raised") or ""
  local signature = tostring(count) .. ":" .. raised_text
  if signature == chart_signature then
    return
  end
  chart_signature = signature
  local raised = {}
  for index in string.gmatch(raised_text, "%d+") do
    raised[tonumber(index)] = true
  end
  chart_cells = {}
  for i = 0, 60 do
    local button = document:get("chart-cell-" .. i)
    button:set_class("hidden", i >= count)
    if i < count then
      local base = string.format("player.chart.%d.", i)
      local cell = {
        x = ui.setting(base .. "x"),
        y = ui.setting(base .. "y"),
        u = ui.setting(base .. "u"),
        v = ui.setting(base .. "v"),
        raised = raised[ui.setting(base .. "index")] == true,
      }
      chart_cells[i + 1] = cell
      button:set_property("left", ((cell.x - 12) * 3) .. "dp")
      button:set_property("top", ((cell.y - 33) * 3) .. "dp")
      local icon = document:get("chart-icon-" .. i)
      icon:set_class("hidden", not cell.raised)
      if cell.raised then
        icon:set_attribute("src", "/" .. ui.setting(base .. "source"))
        icon:set_attribute("rect", string.format("%d %d 16 16", cell.u, cell.v))
      end
    end
  end
  chart_cursor(document)
end

local function detail_value(group, row, column)
  return tonumber(ui.setting(string.format("player.chart.detail.%s.%d.%d", group, row, column)))
    or 0
end

local function transition_box(document, frame)
  local cell = chart_cells[chart_index + 1]
  local start_x, start_y = cell.x + 5, cell.y + 8
  local function interpolate(start, finish)
    local delta = (finish - start) * frame / 4
    return start + (delta < 0 and math.ceil(delta) or math.floor(delta))
  end
  local x = interpolate(start_x, 10)
  local y = interpolate(start_y, 31)
  local right = interpolate(start_x + 10, 310)
  local bottom = interpolate(start_y + 10, 221)
  local box = document:get("chart-detail-transition")
  box:set_property("left", ((x - 11) * 3) .. "dp")
  box:set_property("top", ((y - 32) * 3) .. "dp")
  box:set_property("width", ((right - x + 1) * 3) .. "dp")
  box:set_property("height", ((bottom - y + 1) * 3) .. "dp")
end

local function begin_chart_transition(document, closing)
  chart_transition = { closing = closing, elapsed = 0 }
  document:get("panel"):set_class("chart-detail-open", false)
  document:get("chart-detail-transition"):set_class("hidden", false)
  transition_box(document, closing and 4 or 0)
end

local function open_chart_detail(document)
  local selected = chart_cells[chart_index + 1]
  if
    not selected
    or not selected.raised
    or ui.setting("player.chart.detail.layout_ready") ~= true
    or ui.setting("player.chart.detail.paths_ready") ~= true
  then
    return
  end
  for i = 0, 98 do
    document:get("chart-detail-line-" .. i):set_class("hidden", true)
  end
  for i = 0, 11 do
    document:get("chart-detail-cell-" .. i):set_class("hidden", true)
  end
  for i = 0, 5 do
    local inset = document:get("chart-detail-inset-" .. i)
    inset:set_property("left", ((detail_value("insets", i, 0) - 11) * 3) .. "dp")
    inset:set_property("top", ((detail_value("insets", i, 1) - 32) * 3) .. "dp")
    inset:set_property("width", ((detail_value("insets", i, 2) + 1) * 3) .. "dp")
    inset:set_property("height", ((detail_value("insets", i, 3) + 1) * 3) .. "dp")
  end
  local function draw_cell(slot, target, x, y)
    local box = document:get("chart-detail-cell-" .. slot)
    box:set_class("hidden", false)
    box:set_property("left", ((x - 11) * 3) .. "dp")
    box:set_property("top", ((y - 32) * 3) .. "dp")
    local cell = chart_cells[target + 1]
    local icon = document:get("chart-detail-icon-" .. slot)
    icon:set_class("hidden", not cell or not cell.raised)
    if cell and cell.raised then
      icon:set_attribute("src", "/" .. ui.setting(string.format("player.chart.%d.source", target)))
      icon:set_attribute("rect", string.format("%d %d 16 16", cell.u, cell.v))
    end
  end
  draw_cell(0, chart_index, 151, 99)
  local line_slot = 0
  local cell_slot = 1
  local function color(row)
    return string.format(
      "#%02x%02x%02x",
      detail_value("colors", row, 0),
      detail_value("colors", row, 1),
      detail_value("colors", row, 2)
    )
  end
  local function draw_path(row, primary, edge)
    for segment = 0, 2 do
      local x1 = detail_value("lines", row, segment)
      local y1 = detail_value("lines", row, segment + 4)
      local dx = detail_value("lines", row, segment + 1) - x1
      local dy = detail_value("lines", row, segment + 5) - y1
      for band = -1, 1 do
        local line = document:get("chart-detail-line-" .. line_slot)
        line_slot = line_slot + 1
        line:set_class("hidden", false)
        line:set_property("left", ((x1 + 149) * 3) .. "dp")
        line:set_property("top", ((y1 + 88 + band) * 3) .. "dp")
        line:set_property("width", ((math.sqrt(dx * dx + dy * dy) + 1) * 3) .. "dp")
        line:set_property("background-color", band == 0 and primary or edge)
        line:set_property(
          "transform",
          string.format("rotate(%fdeg)", math.atan(dy, dx) * 180 / math.pi)
        )
      end
    end
  end
  for _, side in ipairs({ "from", "to" }) do
    local refs = {}
    local count = 0
    local slots = side == "from" and 5 or 6
    for j = 0, slots - 1 do
      local value = tonumber(
        ui.setting(string.format("player.chart.%d.%s.%d", chart_index, side, j))
      ) or -1
      refs[j + 1] = value
      if value ~= -1 then
        count = count + 1
      end
    end
    local odd = count % 2 == 1
    local offset = side == "from" and (odd and 4 or 0) or (odd and 15 or 9)
    local length = side == "from" and (odd and 5 or 4) or (odd and 5 or 6)
    for j = 0, length - 1 do
      local target = refs[j + 1]
      if target and target >= 0 then
        draw_path(
          offset + j,
          side == "from" and "#e39407" or color(j * 2),
          side == "from" and "#db6500" or color(j * 2 + 1)
        )
        draw_cell(
          cell_slot,
          target,
          detail_value("sprites", offset + j, 0) + 159,
          detail_value("sprites", offset + j, 1) + 119
        )
        cell_slot = cell_slot + 1
      end
    end
  end
  local base = string.format("player.chart.%d.", chart_index)
  document:get("chart-detail-name"):set_text(t(ui.setting(base .. "name") or ""))
  document
    :get("chart-detail-level")
    :set_text(
      t("pause_menu.chart_level." .. tostring(math.floor(ui.setting(base .. "level") or 0)))
    )
  chart_detail = true
  begin_chart_transition(document, false)
  document:get("player-tab-chart"):focus()
end

local function medals_cursor(document)
  local cursor = document:get("medals-cursor")
  cursor:set_class("hidden", not medals_selecting)
  cursor:set_property("left", ((23 + (medal_index % 5) * 38) * 3) .. "dp")
  cursor:set_property("top", ((27 + math.floor(medal_index / 5) * 24) * 3) .. "dp")
end

-- renderMedalView: a medal the player holds shows without its dark cover, and the one under the
-- selector (held) shows its name and its three lines below the medals.
local function refresh_medals(document)
  local held = number_list("player.medals.held")
  for i = 0, 14 do
    local cell = document:get("medal-" .. i)
    cell:set_property("left", ((25 + (i % 5) * 38) * 3) .. "dp")
    cell:set_property("top", ((27 + math.floor(i / 5) * 24) * 3) .. "dp")
    cell:set_class("held", (held[i] or 0) ~= 0)
  end
  local shown = (held[medal_index] or 0) ~= 0
  local base = string.format("player.medal.%d.", medal_index)
  document:get("medal-name"):set_text(shown and t(ui.setting(base .. "name") or "") or "")
  for line = 0, 2 do
    document
      :get("medal-line-" .. line)
      :set_text(shown and t(ui.setting(base .. "line." .. line) or "") or "")
  end
  medals_cursor(document)
end

function medal_pick(document, index)
  medal_index = math.max(0, math.min(14, index))
  medals_selecting = true
  refresh_medals(document)
  document:get("medal-" .. medal_index):focus()
end

-- The card list (game_menu.c: renderCardsView, tickPlayerMenu): 11 by 6 slots of 18 x 18 at 24
-- pixels from (19, 25) of the page; the cursor's 24 x 20 frame from (17, 24).
local card_columns = 11
local card_slots = 66

local function cards_cursor(document)
  local cursor = document:get("cards-cursor")
  cursor:set_class("hidden", not cards_selecting)
  cursor:set_property("left", ((17 + (card_index % card_columns) * 24) * 3) .. "dp")
  cursor:set_property("top", ((24 + math.floor(card_index / card_columns) * 24) * 3) .. "dp")
end

-- renderCardsView: a card the player holds shows its 16 x 16 icon of SBOY, in its own palette.
local function refresh_cards(document)
  local copies = number_list("player.cards.copies")
  for i = 0, card_slots - 1 do
    local cell = document:get("card-" .. i)
    cell:set_property("left", ((19 + (i % card_columns) * 24) * 3) .. "dp")
    cell:set_property("top", ((25 + math.floor(i / card_columns) * 24) * 3) .. "dp")
    local icon = document:get("card-icon-" .. i)
    local held = (copies[i] or 0) > 0
    icon:set_class("hidden", not held)
    local palette =
      math.floor(tonumber(ui.setting(string.format("player.card.%d.palette", i))) or 0)
    if held and card_palettes[i] ~= palette then
      card_palettes[i] = palette
      icon:set_attribute("src", "/texture/etcdat/sboy?palette=" .. palette)
    end
  end
  cards_cursor(document)
end

-- The card's picture and copies (tickPlayerMenu: renderCardImage, renderCardCount) open from the
-- middle of its slot: the picture 150 x 180 at (74, 5) of the page, the copies 54 x 24 at
-- (223, 157). Only a card the player holds opens.
local function card_from(document)
  local rect = original_box.rect(document, "card-" .. card_index)
  return { rect[1] + 27, rect[2] + 27, 3, 3 }
end

local function open_card(document)
  local copies = number_list("player.cards.copies")[card_index] or 0
  if copies == 0 then
    return
  end
  card_open = true
  document:get("card-detail-image"):set_attribute("src", string.format("/card/%03d", card_index))
  document:get("card-count"):set_text(string.format("%d", copies))
  local from = card_from(document)
  original_box.open(document, "card-detail", from)
  original_box.open(document, "card-count-box", from)
end

local function close_card(document)
  card_open = false
  local to = card_from(document)
  original_box.close(document, "card-detail", to)
  original_box.close(document, "card-count-box", to)
end

-- Picks a card of the list (tickPlayerMenu, PLAYER_MENU_STATE 3): left and right stop at the row's
-- ends, up and down at the first and last rows.
local function card_move(document, action)
  local column = card_index % card_columns
  if action == "left" and column ~= 0 then
    card_index = card_index - 1
  elseif action == "right" and column ~= card_columns - 1 then
    card_index = card_index + 1
  elseif action == "up" and card_index >= card_columns then
    card_index = card_index - card_columns
  elseif action == "down" and card_index < card_slots - card_columns then
    card_index = card_index + card_columns
  else
    return
  end
  cards_cursor(document)
end

function show_player_tab(document, tab)
  chart_detail = false
  chart_transition = nil
  document:get("chart-detail-transition"):set_class("hidden", true)
  document:get("panel"):set_class("chart-detail-open", false)
  medals_selecting = false
  medals_cursor(document)
  cards_selecting = false
  cards_cursor(document)
  card_open = false
  document:get("card-detail"):set_class("box-closed", true)
  document:get("card-count-box"):set_class("box-closed", true)
  player_tab = tab
  chart_selecting = false
  document:get("panel"):set_class("player-chart", tab == "chart")
  document:get("player-tab-player"):set_class("selected", tab == "player")
  document:get("player-tab-chart"):set_class("selected", tab == "chart")
  document:get("player-tab-medals"):set_class("selected", tab == "medals")
  document:get("panel"):set_class("player-medals", tab == "medals")
  document:get("player-tab-card"):set_class("selected", tab == "card")
  document:get("panel"):set_class("player-cards", tab == "card")
  chart_cursor(document)
  document:get("player-tab-" .. tab):focus()
end

function chart_pick(document, index)
  if not chart_cells[index + 1] then
    return
  end
  chart_index = index
  chart_selecting = true
  chart_cursor(document)
  document:get("chart-cell-" .. index):focus()
end

local function chart_move(document, action)
  local cell = chart_cells[chart_index + 1]
  if not cell then
    return
  end
  local target = cell
  if action == "up" or action == "down" then
    local delta = action == "up" and -1 or 1
    local distance = math.huge
    for _, candidate in ipairs(chart_cells) do
      local dy = (candidate.y - cell.y) * delta
      if candidate.x == cell.x and dy > 0 and dy < distance then
        target = candidate
        distance = dy
      end
    end
  else
    local delta = action == "left" and -1 or 1
    local distance = math.huge
    for _, candidate in ipairs(chart_cells) do
      local dx = (candidate.x - cell.x) * delta
      if dx > 0 and dx < distance then
        distance = dx
        target = candidate
      end
    end
    if target ~= cell then
      local column_x = target.x
      local best_y = -math.huge
      local first_y = math.huge
      local first = target
      for _, candidate in ipairs(chart_cells) do
        if candidate.x == column_x then
          if candidate.y < first_y then
            first = candidate
            first_y = candidate.y
          end
          if candidate.y <= cell.y and candidate.y > best_y then
            target = candidate
            best_y = candidate.y
          end
        end
      end
      if best_y == -math.huge then
        target = first
      end
    end
  end
  for i, candidate in ipairs(chart_cells) do
    if candidate == target then
      chart_pick(document, i - 1)
      return
    end
  end
end

local refresh_player

local function refresh(document)
  refresh_medals(document)
  refresh_cards(document)
  refresh_chart(document)
  draw_date_digits(document, "field-year-", 3, ui.setting("hud.clock.year"))
  draw_date_digits(document, "field-day-", 2, ui.setting("hud.clock.day"))
  local original = ui.setting("ui_skin") == "original"
  document:get("panel"):set_class("use-menu-art", (ui.setting("language") or "en") == "en")
  document:get("sleep"):set_class("unavailable", not can_sleep())
  document:get("panel"):set_class("has-fishing", original and fishing() ~= 0)
  document:get("sleep"):set_class("hidden", false)
  document:get("fish"):set_class("hidden", fishing() == 0)
  document:get("fish"):set_class("unavailable", fishing() ~= 2)
  local mode = index_of(minimap_modes, ui.setting("minimap")) and ui.setting("minimap") or "auto"
  document:get("minimap-mode-value"):set_text(t("pause_menu.minimap_mode." .. mode))
  local size = index_of(minimap_sizes, ui.setting("minimap_size")) and ui.setting("minimap_size")
    or "medium"
  document:get("minimap-size-value"):set_text(t("pause_menu.minimap_size." .. size))
  document:get("display-mode-value"):set_text(
    t(
      ui.setting("fullscreen") and "pause_menu.display_mode.fullscreen"
        or "pause_menu.display_mode.window"
    )
  )
  local percent = math.floor((ui.setting("ui_scale") or 1.0) * 100 + 0.5)
  document:get("ui-scale-value"):set_text(t("pause_menu.ui_scale.value", { percent = percent }))
  document:get("vsync-value"):set_text(on_off(ui.setting("vsync")))
  document:get("ui-skin-value"):set_text(skin_choice.name())
  document:get("free-exploration-value"):set_text(on_off(ui.setting("free_exploration")))
  document:get("smooth-motion-value"):set_text(on_off(ui.setting("smooth_motion")))
  document:get("save-notice-value"):set_text(on_off(ui.setting("save_notice")))
  document:get("digital-time-value"):set_text(on_off(ui.setting("digital_time")))
  refresh_player(document)
end

-- The PLAYER page's own fields: its name, level, rank, bits, counts, items and play time.
refresh_player = function(document)
  document:get("player-name"):set_text(ui.setting("player.name") or "")
  local level = ui.setting("player.level")
  document
    :get("player-level")
    :set_text(type(level) == "number" and string.format("%d", level) or "")
  local rank = ""
  if type(level) == "number" and level >= 0 and level % 1 == 0 then
    local key = string.format("main.game_menu_level_titles.%04d", level)
    local label = t(key)
    if label ~= key then
      rank = label
    end
  end
  document:get("player-rank"):set_text(rank)
  local bits = ui.setting("player.bits")
  document:get("player-bits"):set_text(type(bits) == "number" and string.format("%d", bits) or "")
  for _, field in ipairs({ "raised", "medals" }) do
    local value = ui.setting("player." .. field)
    document
      :get("player-" .. field)
      :set_text(type(value) == "number" and string.format("%d", value) or "")
  end
  -- renderPlayerInfoView: the item boxes show the rods and the two other items the player has.
  local items = number_list("player.items.held")
  for slot = 0, 3 do
    document:get("player-item-" .. slot):set_class("hidden", (items[slot] or 0) == 0)
  end
  local seconds = ui.setting("player.seconds")
  document:get("player-time"):set_text(
    type(seconds) == "number"
        and string.format(
          ui.setting("ui_skin") == "original" and "%d : %d" or "%d:%02d",
          math.floor(seconds / 3600),
          math.floor(seconds / 60) % 60
        )
      or ""
  )
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
  document:get("prompt-cancel"):set_text(ui.prompt("cancel"))
  -- The player page's prompts name the key of the device in use (a PC game: the keyboard's keys
  -- first, a pad's buttons when a pad plays), not the disc's fixed pad glyphs.
  for _, page in ipairs({ "chart", "medals", "cards" }) do
    document:get(page .. "-confirm-key"):set_text(ui.prompt("confirm"))
    document:get(page .. "-cancel-key"):set_text(ui.prompt("cancel"))
  end
end

local function show_page(document, name, focus, tools)
  for _, page in ipairs(pages) do
    document:get("page-" .. page):set_class("hidden", page ~= name)
  end
  if name == "player" and current == "main" then
    player_tab = "player"
    chart_selecting = false
    document:get("player-tab-player"):set_class("selected", true)
    document:get("player-tab-chart"):set_class("selected", false)
    document:get("player-tab-medals"):set_class("selected", false)
    chart_cursor(document)
  end
  current = name
  document:get("panel"):set_class("player-chart", name == "player" and player_tab == "chart")
  document:get("panel"):set_class("player-medals", name == "player" and player_tab == "medals")
  player_tools_open = tools == true
  document:get("panel"):set_class("player-tools-open", player_tools_open)
  document
    :get("panel")
    :set_class("on-player", name == "player" and ui.setting("ui_skin") == "original")
  document
    :get("title")
    :set_text(t(player_tools_open and "pause_menu.title.tools" or "pause_menu.title." .. name))
  -- The original's menu box heads its icons with the date (game_menu.c: renderGameMenu); a skin
  -- shows it on the main page through the panel's class "on-main".
  document:get("panel"):set_class("on-main", name == "main")
  -- The settings pages: a skin may give their label-and-value rows a wider box.
  document:get("panel"):set_class("on-settings", name == "options" or name == "minimap")
  local first = name == "main"
      and ui.setting("ui_skin") == "original"
      and (fishing() ~= 0 and "fish" or "open-items")
    or (
      name == "player"
        and ui.setting("ui_skin") == "original"
        and (player_tools_open and "player-minimap" or "player-tab-" .. player_tab)
      or first_entry[name]
    )
  document:get(focus or first):focus()
end

function open_page(document, name)
  parents[name] = current
  show_page(document, name)
end

-- The original's boxes (ui/common/original_box.lua): the menu box grows from the player and shrinks
-- back into it (game_menu.c: createMenuBox, closeUIBoxIfOpen); going to a page closes it first and
-- opens the page's box from the player then, ten frames in all (measured: the outline's frames
-- 4..0, then 0..4).
local function close_menu_box(document, done)
  original_box.close(document, "panel", original_box.player_rect(document), done)
end

local function open_menu_box(document)
  original_box.open(document, "panel", original_box.player_rect(document))
end

function open_player(document)
  if current ~= "main" or not original_box.enabled() then
    open_page(document, "player")
    return
  end
  close_menu_box(document, function()
    open_page(document, "player")
    open_menu_box(document)
  end)
end

local function close_menu(document)
  close_menu_box(document, function()
    ui.hide(document_name)
  end)
end

function back(document)
  if current == "player" and player_tools_open then
    player_tools_open = false
    document:get("panel"):set_class("player-tools-open", false)
    document:get("title"):set_text(t("pause_menu.title.player"))
    document:get("player-tab-" .. player_tab):focus()
    return
  end
  if current == "main" then
    close_menu(document)
  elseif current == "player" and original_box.enabled() then
    close_menu_box(document, function()
      show_page(document, "main", opener.player)
      open_menu_box(document)
    end)
  else
    local parent = parents[current] or "main"
    local focus = parent == "player" and "player-" .. current or opener[current]
    show_page(document, parent, focus, parent == "player" and ui.setting("ui_skin") == "original")
  end
end

-- The document shown last, for the buttons' event attributes that only name the page.
local shown_document = nil

function open_partner(page)
  close_menu_box(shown_document, function()
    ui.send("open_partner_menu", page)
  end)
end

-- The game closes the menu and puts the partner to bed (docs/architecture/guardar-y-cargar.md,
-- 3.6).
function sleep()
  if can_sleep() then
    ui.send("sleep")
  end
end

-- The game closes the menu and the player takes out the rod (game_menu.c: initializeFishing).
function fish()
  if fishing() == 2 then
    ui.send("fish")
  end
end

function resume()
  ui.hide(document_name)
end

function adjust(_document, id, step)
  options[id](step)
end

function on_show(document)
  shown_document = document
  chart_detail = false
  chart_transition = nil
  document:get("chart-detail-transition"):set_class("hidden", true)
  document:get("panel"):set_class("chart-detail-open", false)
  parents = {}
  icon_time = 0
  refresh(document)
  local entry = ui.setting("ui_skin") == "original" and ui.setting("pause_menu.start_entry") or nil
  if ui.setting("pause_menu.preview_player") == true then
    parents.player = "main"
    show_page(document, "player")
    if ui.setting("player.chart.preview") == true then
      show_player_tab(document, "chart")
    end
  else
    show_page(document, "main", entry)
    open_menu_box(document)
  end
  refresh_prompts(document)
end

function on_hide(document)
  original_box.cancel(document)
end

function on_tick(document, seconds)
  original_box.tick(document, seconds)
  if chart_transition then
    chart_transition.elapsed = chart_transition.elapsed + seconds
    local step = math.floor(chart_transition.elapsed * 20 + 0.000001)
    if step >= 5 then
      local closing = chart_transition.closing
      chart_transition = nil
      document:get("chart-detail-transition"):set_class("hidden", true)
      document:get("panel"):set_class("chart-detail-open", not closing)
      if closing then
        chart_detail = false
        document:get("chart-cell-" .. chart_index):focus()
      end
    else
      transition_box(document, chart_transition.closing and 4 - step or step)
    end
  end
  if current == "player" and player_tab == "chart" and not player_tools_open then
    icon_time = icon_time + seconds
    local flash = math.floor(icon_time * 20) % 10 < 5
    if flash ~= chart_flash then
      chart_flash = flash
      for i, cell in ipairs(chart_cells) do
        if cell.raised then
          document:get("chart-icon-" .. (i - 1)):set_attribute(
            "rect",
            string.format(
              "%d %d 16 16",
              cell.u + (chart_selecting and chart_index == i - 1 and flash and 16 or 0),
              cell.v
            )
          )
        end
      end
    end
  end
  if current ~= "main" or ui.setting("ui_skin") ~= "original" then
    return
  end
  icon_time = icon_time + seconds
  local flash = math.floor(icon_time * 20) % 10 < 5
  if flash ~= icon_flash then
    icon_flash = flash
    document:get("panel"):set_class("icon-flash", flash)
  end
end

-- A setting changed: only what reads it is drawn again. The game publishes the PLAYER page's
-- values one by one and the HUD's each frame it changes, and a whole refresh for each of them made
-- opening the menu stall; any other setting refreshes the whole menu.
function on_setting(document, name)
  if name:sub(1, 15) == "save_indicator." then
    -- The save indicator's own (ui/save_indicator), which change every frame while it fades.
    return
  elseif name:sub(1, 4) == "hud." then
    if name == "hud.clock.year" or name == "hud.clock.day" then
      draw_date_digits(document, "field-year-", 3, ui.setting("hud.clock.year"))
      draw_date_digits(document, "field-day-", 2, ui.setting("hud.clock.day"))
    end
  elseif name:sub(1, 12) == "player.chart" then
    refresh_chart(document)
  elseif name:sub(1, 12) == "player.medal" then
    refresh_medals(document)
    refresh_player(document)
  elseif name:sub(1, 11) == "player.card" then
    refresh_cards(document)
  elseif name:sub(1, 7) == "player." then
    refresh_player(document)
  else
    refresh(document)
  end
end

function on_device(document)
  refresh_prompts(document)
end

-- The language changed: the elements with a text-key are already in the new one; the rest are
-- written here.
function on_language(document)
  document:get("title"):set_text(t("pause_menu.title." .. current))
  refresh(document)
  refresh_prompts(document)
end

-- Navigation steps arrive here first. Returning true means the step was handled here and suppresses
-- the default (moving the focus, confirming, closing); returning false lets the default run.
function on_action(document, action)
  -- tickUIBox: a box takes no input until it is in place.
  if chart_transition or original_box.busy() then
    return true
  end
  if chart_detail then
    if action == "cancel" then
      begin_chart_transition(document, true)
    end
    return true
  end
  if current == "player" and not player_tools_open and ui.setting("ui_skin") == "original" then
    if player_tab == "medals" and medals_selecting then
      if action == "cancel" then
        medals_selecting = false
        medals_cursor(document)
        document:get("player-tab-medals"):focus()
      elseif action == "left" and medal_index % 5 > 0 then
        medal_pick(document, medal_index - 1)
      elseif action == "right" and medal_index % 5 < 4 then
        medal_pick(document, medal_index + 1)
      elseif action == "up" and medal_index >= 5 then
        medal_pick(document, medal_index - 5)
      elseif action == "down" and medal_index < 10 then
        medal_pick(document, medal_index + 5)
      end
      return true
    end

    if player_tab == "card" and card_open then
      if action == "cancel" then
        close_card(document)
      end
      return true
    end

    if player_tab == "card" and cards_selecting then
      if action == "confirm" then
        open_card(document)
      elseif action == "cancel" then
        cards_selecting = false
        cards_cursor(document)
        document:get("player-tab-card"):focus()
      elseif action == "up" or action == "down" or action == "left" or action == "right" then
        card_move(document, action)
      end
      return true
    end

    if player_tab == "chart" and chart_selecting then
      if action == "cancel" then
        chart_selecting = false
        chart_cursor(document)
        document:get("player-tab-chart"):focus()
        return true
      end
      if action == "up" or action == "down" or action == "left" or action == "right" then
        chart_move(document, action)
        return true
      end
      if action == "confirm" then
        open_chart_detail(document)
        return true
      end
    else
      if action == "right" or action == "left" then
        -- tickPlayerMenu: the four pages are always there, in this order.
        local tabs = { "player", "chart", "medals", "card" }
        local current_index = index_of(tabs, player_tab)
        local delta = action == "left" and -1 or 1
        show_player_tab(document, tabs[math.max(1, math.min(#tabs, current_index + delta))])
        return true
      end
      if action == "confirm" and player_tab == "card" then
        card_index = 0
        cards_selecting = true
        cards_cursor(document)
        return true
      end
      if action == "confirm" and player_tab == "medals" then
        -- tickPlayerMenu: Cross starts the selector on the first medal.
        medal_pick(document, 0)
        return true
      end
      if action == "confirm" and player_tab == "chart" then
        chart_pick(document, chart_index)
        return true
      end
    end
  end
  if action == "cancel" then
    back(document)
    return true
  end
  if action == "menu" and current == "player" and ui.setting("ui_skin") == "original" then
    player_tools_open = not player_tools_open
    document:get("panel"):set_class("player-tools-open", player_tools_open)
    document
      :get("title")
      :set_text(t(player_tools_open and "pause_menu.title.tools" or "pause_menu.title.player"))
    document:get(player_tools_open and "player-minimap" or "player-tab-" .. player_tab):focus()
    return true
  end
  if action == "menu" then
    if current == "main" then
      close_menu(document)
    else
      ui.hide(document_name)
    end
    return true
  end
  if
    current == "main"
    and ui.setting("ui_skin") == "original"
    and (action == "up" or action == "down" or action == "left" or action == "right")
  then
    local grid = { "open-items", "open-partner", "open-player", "praise", "scold", "sleep" }
    if fishing() ~= 0 then
      table.insert(grid, "fish")
    end
    for index, id in ipairs(grid) do
      if document:get(id):has_focus() then
        local next_index
        if action == "left" or action == "right" then
          next_index = (index - 1 + (action == "left" and -1 or 1)) % #grid + 1
        elseif action == "up" then
          next_index = index - 3
          if next_index < 1 then
            next_index = next_index + math.floor((#grid + 2) / 3) * 3
          end
          if next_index > #grid then
            next_index = next_index - 3
          end
        else
          next_index = index + 3
          if next_index > #grid then
            next_index = next_index - math.floor((#grid + 2) / 3) * 3
          end
          if next_index < 1 then
            next_index = next_index + 3
          end
        end
        document:get(grid[next_index]):focus()
        return true
      end
    end
  end
  if action == "left" or action == "right" then
    for id, change in pairs(options) do
      local entry = document:get(id)
      if entry ~= nil and entry:has_focus() then
        change(action == "left" and -1 or 1)
        return true
      end
    end
  end
  return false
end
