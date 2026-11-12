-- The jukebox the story opens (shop/script_menu.c: openJukebox, tickJukeboxMenu). The game drives
-- it (apps/game/src/jukebox_screen.cpp) and publishes it as settings, bumping jukebox.revision once
-- per change: jukebox.phase (greeting, list, farewell), .speaker (the owner's name, a text key),
-- .line_key (what the owner says), .chosen_key (the name of the song that plays, for the line's
-- {bgm:...}), .total, .top and .cursor of the list, and jukebox.row.<n>.number/name/playing for
-- the six rows shown. Every button goes to the game as jukebox_action.

local Rows = 6
-- renderItemMenuScrollBar: the track is 90 original pixels (3dp each).
local ThumbTrack = 270

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

local function draw_list(document)
  local total = number("jukebox.total")
  local top = number("jukebox.top")
  for row = 0, Rows - 1 do
    local prefix = "jukebox.row." .. row .. "."
    local name = ui.setting(prefix .. "name") or ""
    local shown = name ~= ""
    -- calculateMusicMenuStrings: the number in two places, then the name; and the number with a
    -- leading zero (01), for a skin that shows that one instead (.number.padded).
    local song = number(prefix .. "number")
    document:get("number-" .. row):set_text(shown and string.format("%2d", song) or "")
    document:get("padded-" .. row):set_text(shown and string.format("%02d", song) or "")
    document:get("name-" .. row):set_text(shown and t(name) or "")
    -- The song that plays is in colour 6.
    document
      :get("row-" .. row)
      :set_class("playing", shown and not not ui.setting(prefix .. "playing"))
  end
  document
    :get("cursor")
    :set_property("top", string.format("%ddp", 51 + 54 * number("jukebox.cursor")))
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

local function draw_talk(document)
  local speaker = text("jukebox.speaker")
  document:get("speaker"):set_text(speaker)
  document:get("speaker"):set_class("hidden", speaker == "")
  -- showTextbox fills {bgm:pstat=249} with the chosen song's name.
  local song = text("jukebox.chosen_key")
  local line = string.gsub(text("jukebox.line_key"), "{bgm:pstat=%d+}", function()
    return song
  end)
  local rows = lines_of(line)
  for index = 0, 2 do
    local value = rows[index + 1]
    local row = document:get("talk-" .. index)
    row:set_class("hidden", value == nil)
    row:set_text(value or "")
  end
end

local function refresh(document)
  local phase = ui.setting("jukebox.phase") or "closed"
  -- The talk only waits for the player to go on here (common/talk_wait.lua).
  talk_wait.draw(document, phase == "greeting" or phase == "farewell")
  local frame = document:get("frame")
  for _, name in ipairs({ "greeting", "list", "farewell" }) do
    frame:set_class("phase-" .. name, phase == name)
  end
  draw_list(document)
  draw_talk(document)
end

function on_show(document)
  refresh(document)
end

function on_setting(document, name)
  if name == "jukebox.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end

-- Every button is the game's: the document neither moves a focus nor closes by itself.
function on_action(_document, action)
  ui.send("jukebox_action", action)
  return true
end
