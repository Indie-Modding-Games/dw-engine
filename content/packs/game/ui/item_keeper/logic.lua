-- The item keeper the story opens (shop/script_menu.c: tickItemKeeper). The game drives it
-- (apps/game/src/item_keeper_screen.cpp) and publishes it as settings, bumping
-- item_keeper.revision once per change: item_keeper.phase (ask, choice, windows, description,
-- farewell), .speaker (the keeper's name, a text key), .line_key (what the keeper says),
-- .choices_key (the yes-or-no line) and .choice, .side (0 the keeper's window, 1 the bag's),
-- .description_key (with .description_species_key, the name its {species} shows), and for each
-- list (item_keeper.kept, item_keeper.bag) .total, .top, .cursor and
-- <list>.<row>.name/count/movable for the five rows shown. Every button goes to the game as
-- item_keeper_action.

local Rows = 5
local ThumbTrack = 240
local Lists = { kept = "item_keeper.kept", bag = "item_keeper.bag" }

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

-- A description's text; its {species} placeholder shows the species whose name key comes with it.
local function described(name, species_name)
  local species_key = ui.setting(species_name) or ""
  if species_key == "" then
    return text(name)
  end
  local key = ui.setting(name) or ""
  return key == "" and "" or t(key, { species = t(species_key) })
end

local function lines_of(value)
  local lines = {}
  for line in string.gmatch(value, "[^\n]+") do
    lines[#lines + 1] = line
  end
  return lines
end

local function draw_list(document, side)
  local list = Lists[side]
  local total = number(list .. ".total")
  local top = number(list .. ".top")
  for row = 0, Rows - 1 do
    local prefix = list .. "." .. row .. "."
    local name = ui.setting(prefix .. "name") or ""
    local shown = name ~= ""
    document:get(side .. "-name-" .. row):set_text(shown and t(name) or "")
    local count = shown and tostring(number(prefix .. "count")) or ""
    document:get(side .. "-count-" .. row):set_text(count)
    local stuck = shown and not ui.setting(prefix .. "movable")
    document:get(side .. "-row-" .. row):set_class("stuck", stuck)
  end
  local cursor = document:get(side .. "-cursor")
  cursor:set_class("hidden", total == 0)
  cursor:set_property("top", string.format("%.2fdp", 102 + 57.75 * number(list .. ".cursor")))
  local height = ThumbTrack
  local offset = 0
  if total > Rows then
    height = ThumbTrack * Rows / total
    offset = ThumbTrack * top / total
  end
  local thumb = document:get(side .. "-thumb")
  thumb:set_property("height", string.format("%.2fdp", height))
  thumb:set_property("top", string.format("%.2fdp", offset))
end

local function draw_talk(document, phase)
  local speaker = text("item_keeper.speaker")
  document:get("speaker"):set_text(speaker)
  document:get("speaker"):set_class("hidden", speaker == "")
  local windows = phase == "windows" or phase == "description"
  document:get("help"):set_class("hidden", not windows)
  local rows = {}
  local choice = nil
  if phase == "choice" then
    rows = lines_of(text("item_keeper.choices_key"))
    choice = number("item_keeper.choice")
  elseif not windows then
    rows = lines_of(text("item_keeper.line_key"))
  end
  for index = 0, 2 do
    local value = rows[index + 1]
    local row = document:get("talk-" .. index)
    row:set_class("hidden", value == nil)
    row:set_class("selected", choice == index)
    document:get("talk-run-" .. index):set_text(value or "")
  end
end

local function refresh(document)
  local phase = ui.setting("item_keeper.phase") or "closed"
  -- The talk only waits for the player to go on here (common/talk_wait.lua).
  talk_wait.draw(document, phase == "ask" or phase == "farewell")
  local frame = document:get("frame")
  for _, name in ipairs({ "ask", "choice", "windows", "description", "farewell" }) do
    frame:set_class("phase-" .. name, phase == name)
  end
  local side = number("item_keeper.side")
  document:get("kept-window"):set_class("active", side == 0)
  document:get("bag-window"):set_class("active", side == 1)
  draw_list(document, "kept")
  draw_list(document, "bag")
  draw_talk(document, phase)
  document
    :get("description-text")
    :set_text(described("item_keeper.description_key", "item_keeper.description_species_key"))
end

-- The help names the keys of the device in use: confirm moves one, alt ten and extra the stack.
local function refresh_prompts(document)
  document:get("key-one"):set_prompt("confirm")
  document:get("key-ten"):set_prompt("alt")
  document:get("key-all"):set_prompt("extra")
end

function on_show(document)
  refresh(document)
  refresh_prompts(document)
end

function on_setting(document, name)
  if name == "item_keeper.revision" then
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

-- Every button is the game's: the document neither moves a focus nor closes by itself.
function on_action(_document, action)
  ui.send("item_keeper_action", action)
  return true
end
