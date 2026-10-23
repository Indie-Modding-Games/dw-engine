-- The gym's stats box after a course (trn_hud.c: TRN_createPostTrainingStatsBox): the six stats as
-- the game counts the gains into them, and the brains course's notes (new orders, a move learned).
-- The game publishes gym.stat.<n>.label (a text key) and gym.stat.<n>.value, gym.notice.count and
-- gym.notice.<n>.text and .name (text keys), and bumps gym.revision; confirm and cancel reach the
-- game as field buttons.

local stats = 6
local notices = 4

local function text(key, arguments)
  if key == nil or key == "" then
    return ""
  end
  return t(key, arguments)
end

local function refresh(document)
  for index = 0, stats - 1 do
    local base = "gym.stat." .. index .. "."
    document:get("stat-" .. index .. "-label"):set_text(text(ui.setting(base .. "label")))
    document:get("stat-" .. index):set_text(tostring(math.floor(ui.setting(base .. "value") or 0)))
  end
  local count = math.floor(ui.setting("gym.notice.count") or 0)
  for index = 0, notices - 1 do
    local line = document:get("notice-" .. index)
    if index < count then
      local base = "gym.notice." .. index .. "."
      line:set_text(text(ui.setting(base .. "text"), { name = text(ui.setting(base .. "name")) }))
    else
      line:set_text("")
    end
    line:set_class("hidden", index >= count)
  end
end

function on_show(document)
  refresh(document)
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
end
