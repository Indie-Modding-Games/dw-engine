-- The pause menu: main, player, minimap and options. The menu never changes a setting itself: it
-- sends a command to the game with ui.send and shows what the game reports back through ui.setting,
-- so the labels always tell the truth. Every text the player sees comes from the texts in the
-- player's language: t(key) here, text-key in the document.

local pages = { "main", "minimap", "options", "player" }
local first_entry = { main = "continue", minimap = "minimap-mode", options = "display-mode", player = "player-minimap" }
-- The entry of the main page that opened each page, focused again when going back.
local opener = { minimap = "open-minimap", options = "open-options", player = "open-player" }

local current = "main"
local parents = {}
local icon_time = 0
local icon_flash = false

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
    for number = 0, 9 do element:set_class("digit-" .. number, digit == tostring(number)) end
  end
end

local function refresh(document)
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
  document:get("player-level"):set_text(tostring(ui.setting("player.level") or ""))
  document:get("player-bits"):set_text(tostring(ui.setting("player.bits") or ""))
  local seconds = ui.setting("player.seconds")
  document:get("player-time"):set_text(type(seconds) == "number"
    and string.format("%d:%02d", math.floor(seconds / 3600), math.floor(seconds / 60) % 60) or "")
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
  document:get("prompt-cancel"):set_text(ui.prompt("cancel"))
end

local function show_page(document, name, focus)
  for _, page in ipairs(pages) do
    document:get("page-" .. page):set_class("hidden", page ~= name)
  end
  current = name
  document:get("title"):set_text(t("pause_menu.title." .. name))
  -- The original's menu box heads its icons with the date (game_menu.c: renderGameMenu); a skin
  -- shows it on the main page through the panel's class "on-main".
  document:get("panel"):set_class("on-main", name == "main")
  local first = name == "main" and ui.setting("ui_skin") == "original"
    and (fishing() ~= 0 and "fish" or "open-items") or first_entry[name]
  document:get(focus or first):focus()
end

function open_page(document, name)
  parents[name] = current
  show_page(document, name)
end

function back(document)
  if current == "main" then
    ui.hide(document_name)
  else
    local parent = parents[current] or "main"
    local focus = parent == "player" and "player-" .. current or opener[current]
    show_page(document, parent, focus)
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
  show_page(document, "main", entry)
  refresh_prompts(document)
end

function on_tick(document, seconds)
  if current ~= "main" or ui.setting("ui_skin") ~= "original" then return end
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
  if action == "cancel" then
    back(document)
    return true
  end
  if action == "menu" then
    ui.hide(document_name)
    return true
  end
  if current == "main" and ui.setting("ui_skin") == "original"
    and (action == "up" or action == "down" or action == "left" or action == "right") then
    local grid = { "open-items", "open-partner", "open-player", "praise", "scold", "sleep" }
    if fishing() ~= 0 then table.insert(grid, "fish") end
    for index, id in ipairs(grid) do
      if document:get(id):has_focus() then
        local next_index = index
        if action == "left" or action == "right" then
          next_index = (index - 1 + (action == "left" and -1 or 1)) % #grid + 1
        elseif action == "up" then
          next_index = index - 3
          if next_index < 1 then next_index = next_index + math.floor((#grid + 2) / 3) * 3 end
          if next_index > #grid then next_index = next_index - 3 end
        else
          next_index = index + 3
          if next_index > #grid then next_index = next_index - math.floor((#grid + 2) / 3) * 3 end
          if next_index < 1 then next_index = next_index + 3 end
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
