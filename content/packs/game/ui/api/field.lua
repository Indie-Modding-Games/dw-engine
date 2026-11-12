-- The UI API's field (docs/modding/ui-api.md, namespace field): what the field shows around the
-- partner and the player, as the game publishes it.
--
-- Every function returns a fresh table; changing it changes nothing in the game.

field = {}

local function whole(name)
  local value = ui.setting(name)
  if type(value) ~= "number" then
    return 0
  end
  return math.floor(value)
end

-- How dark the map change's fade is now, from 0 (none) to 1 (black).
function field.fade()
  local level = ui.setting("screen_fade")
  if type(level) ~= "number" then
    return 0
  end
  return math.min(math.max(level, 0), 1)
end

-- The text key of the map's name the game shows on arrival, or nil.
function field.map_title()
  local key = ui.setting("map_title_key")
  if type(key) ~= "string" or key == "" then
    return nil
  end
  return key
end

local minimap_sizes = { small = true, medium = true, large = true }

-- The round minimap: whether it may show (the player's option on, the field the player's to walk,
-- no battle), whether a window covers it, and its size ("small", "medium" or "large").
function field.minimap()
  local option = ui.setting("minimap")
  local size = ui.setting("minimap_size")
  return {
    shown = option ~= "off"
      and option ~= false
      and ui.setting("battle.active") ~= true
      and ui.setting("hud.minimap_allowed") ~= false,
    covered = ui.setting("hud.minimap_covered") == true,
    size = minimap_sizes[size] and size or "medium",
  }
end

-- Where the player stands on the window, in percent of its width and height.
function field.player()
  local x, y = ui.setting("field.player_x"), ui.setting("field.player_y")
  return { x = type(x) == "number" and x or 50, y = type(y) == "number" and y or 50 }
end

-- The shop job's tally while the canyon shop's job runs (shop_job_bar, an improvement), or nil:
-- the tally, the tally the shopkeeper joins from (goal), the one the better pay starts at, and the
-- customers served of all.
function field.shop_job()
  if ui.setting("shop_job.shown") ~= true then
    return nil
  end
  return {
    tally = whole("shop_job.tally"),
    goal = whole("shop_job.goal"),
    better_pay = whole("shop_job.better_pay"),
    customer = whole("shop_job.customer"),
    customers = whole("shop_job.customers"),
  }
end

-- The field's events (hooks.on).
event_source("screen_fade", "field.fade")
event_source("map_title_key", "field.map_title")
event_source("hud.minimap_allowed", "field.minimap")
event_source("hud.minimap_covered", "field.minimap")
event_source("battle.active", "field.minimap")
event_source("shop_job.revision", "field.shop_job")
