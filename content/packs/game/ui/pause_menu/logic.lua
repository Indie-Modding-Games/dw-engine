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
  document:get("player-tab-chart"):set_class("unavailable", count == 0)
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
        icon:set_attribute("source", ui.setting(base .. "source"))
        icon:set_attribute("rect", string.format("%d %d 16 16", cell.u, cell.v))
      end
    end
  end
  for id, index in pairs({ ["chart-cross"] = 5, ["chart-triangle"] = 7 }) do
    local key = string.format("main.status_view_labels.%04d", index)
    local label = t(key)
    document:get(id):set_text(label ~= key and string.match(label, "^(%S+)") or "")
  end
  chart_cursor(document)
end

function show_player_tab(document, tab)
  if tab == "chart" and (tonumber(ui.setting("player.chart.count")) or 0) == 0 then
    return
  end
  player_tab = tab
  chart_selecting = false
  document:get("panel"):set_class("player-chart", tab == "chart")
  document:get("player-tab-player"):set_class("selected", tab == "player")
  document:get("player-tab-chart"):set_class("selected", tab == "chart")
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

local function refresh(document)
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
    chart_cursor(document)
  end
  current = name
  document:get("panel"):set_class("player-chart", name == "player" and player_tab == "chart")
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

function back(document)
  if current == "player" and player_tools_open then
    player_tools_open = false
    document:get("panel"):set_class("player-tools-open", false)
    document:get("title"):set_text(t("pause_menu.title.player"))
    document:get("player-tab-" .. player_tab):focus()
    return
  end
  if current == "main" then
    ui.hide(document_name)
  else
    local parent = parents[current] or "main"
    local focus = parent == "player" and "player-" .. current or opener[current]
    show_page(document, parent, focus, parent == "player" and ui.setting("ui_skin") == "original")
  end
end

function open_partner(page)
  ui.send("open_partner_menu", page)
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
  end
  refresh_prompts(document)
end

function on_tick(document, seconds)
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

function on_setting(document, _name)
  refresh(document)
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
  if current == "player" and not player_tools_open and ui.setting("ui_skin") == "original" then
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
        return true
      end -- Detail view is a separate unfinished unit.
    else
      if action == "right" then
        show_player_tab(document, "chart")
        return true
      end
      if action == "left" then
        show_player_tab(document, "player")
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
    ui.hide(document_name)
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
