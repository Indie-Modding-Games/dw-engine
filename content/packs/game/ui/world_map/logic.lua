-- The island map (M), a dwe improvement. The game opens it (apps/game/src/world_map_screen.cpp) and
-- publishes it as settings, bumping world_map.revision when they change: world_map.image (the
-- painted island) and .aspect, .here_key and .area_key (the player's place and area, text keys),
-- .marker with .marker_x and .marker_y (percent of the map), .regions and
-- world_map.region.<n>.name/x/y/known/here/exit, .exits and world_map.exit.<n>.name/area. The map
-- waits for nothing: M or cancel close it, through the game (world_map_action).

local Slots = 40
local ExitSlots = 8
-- The map's box in dp (structure.rcss #map): the island keeps its proportions inside it.
local MapWidth = 912
local MapHeight = 520

local function number(name)
  return tonumber(ui.setting(name)) or 0
end

local function text(name)
  local key = ui.setting(name) or ""
  if key == "" then
    return ""
  end
  -- The original's map names are centred with spaces on its banner.
  return (string.match(t(key), "^%s*(.-)%s*$"))
end

local function fit_map(document)
  local aspect = number("world_map.aspect")
  if aspect <= 0 then
    return
  end
  local width, height = MapWidth, MapWidth / aspect
  if height > MapHeight then
    width, height = MapHeight * aspect, MapHeight
  end
  local map = document:get("map")
  map:set_property("width", string.format("%ddp", math.floor(width + 0.5)))
  map:set_property("height", string.format("%ddp", math.floor(height + 0.5)))
  map:set_property("left", string.format("%ddp", math.floor(24 + (MapWidth - width) / 2 + 0.5)))
end

local function draw_regions(document)
  local total = math.floor(number("world_map.regions"))
  for slot = 0, Slots - 1 do
    local label = document:get("region-" .. slot)
    local prefix = "world_map.region." .. slot .. "."
    local shown = slot < total
    label:set_class("hidden", not shown)
    if shown then
      label:set_text(text(prefix .. "name"))
      label:set_property("left", string.format("%.2f%%", number(prefix .. "x")))
      label:set_property("top", string.format("%.2f%%", number(prefix .. "y")))
      label:set_class("unknown", not ui.setting(prefix .. "known"))
      label:set_class("exit", ui.setting(prefix .. "exit") == true)
      label:set_class("here", ui.setting(prefix .. "here") == true)
    end
  end
end

local function draw_marker(document)
  local marker = document:get("marker")
  local shown = ui.setting("world_map.marker") == true
  marker:set_class("hidden", not shown)
  if shown then
    marker:set_property("left", string.format("%.2f%%", number("world_map.marker_x")))
    marker:set_property("top", string.format("%.2f%%", number("world_map.marker_y")))
  end
end

local function draw_exits(document)
  local total = math.floor(number("world_map.exits"))
  for slot = 0, ExitSlots - 1 do
    local prefix = "world_map.exit." .. slot .. "."
    local shown = slot < total
    document:get("exit-" .. slot):set_class("hidden", not shown)
    document:get("exit-name-" .. slot):set_text(shown and text(prefix .. "name") or "")
    -- The area in brackets after a place of it: "Lava Cave (Drill Tunnel)".
    local area = shown and text(prefix .. "area") or ""
    document:get("exit-area-" .. slot):set_text(area ~= "" and "(" .. area .. ")" or "")
  end
  document:get("no-exits"):set_class("hidden", total > 0)
end

local function draw_here(document)
  local place = text("world_map.here_key")
  local area = text("world_map.area_key")
  if area ~= "" and area ~= place then
    place = place .. " (" .. area .. ")"
  end
  document:get("here-name"):set_text(" " .. place)
  document:get("here"):set_class("hidden", place == "")
end

local function refresh(document)
  document:get("map-image"):set_attribute("src", ui.setting("world_map.image") or "")
  fit_map(document)
  draw_regions(document)
  draw_marker(document)
  draw_exits(document)
  draw_here(document)
  document:get("close-key"):set_text(ui.prompt("world_map"))
end

function on_show(document)
  refresh(document)
end

-- The close key's glyph follows the device the player last used.
function on_device(document)
  document:get("close-key"):set_text(ui.prompt("world_map"))
end

function on_setting(document, name)
  if name == "world_map.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end

-- No entry takes the focus: cancel closes, the rest does nothing.
function on_action(_document, action)
  ui.send("world_map_action", action)
  return true
end
