-- The UI API's clock (docs/modding/ui-api.md, namespace clock): the game time as the game publishes
-- it with the field's state, rules included (the night and the part of the day are the game's).
--
-- Every function returns a fresh table; changing it changes nothing in the game.

clock = {}

local function whole(name)
  local value = ui.setting(name)
  if type(value) ~= "number" then
    return 0
  end
  return math.floor(value)
end

-- The time now: year and day (from 1), hour (0-23) and minute, whether it is night and the part of
-- the day ("morning", "day", "late" or "night"), and whether the game time runs (false in a menu,
-- a battle or before a game starts). Until the game publishes it the clock reads 0:00, which is
-- night.
function clock.now()
  local phase = ui.setting("hud.clock.phase")
  return {
    year = whole("hud.clock.year"),
    day = whole("hud.clock.day"),
    hour = whole("hud.clock.hour"),
    minute = whole("hud.clock.minute"),
    night = ui.setting("hud.clock.night") ~= false,
    phase = type(phase) == "string" and phase or "night",
    running = ui.setting("hud.shown") == true,
  }
end

-- clock.changed: the field's state changed, the time included.
event_source("hud.revision", "clock.changed")
