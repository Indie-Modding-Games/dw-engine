-- The gym's stats box after a course (trn_hud.c: TRN_createPostTrainingStatsBox): the six stats as
-- the game counts the gains into them. The game publishes gym.stat.<n>.label (a text key) and
-- gym.stat.<n>.value and bumps gym.revision; confirm and cancel reach the game as field buttons.

local stats = 6

local function refresh(document)
  for index = 0, stats - 1 do
    local base = "gym.stat." .. index .. "."
    local label = ui.setting(base .. "label") or ""
    document:get("stat-" .. index .. "-label"):set_text(label ~= "" and t(label) or "")
    document:get("stat-" .. index):set_text(tostring(math.floor(ui.setting(base .. "value") or 0)))
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
