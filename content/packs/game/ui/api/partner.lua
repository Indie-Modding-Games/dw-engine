-- The UI API's partner (docs/modding/ui-api.md, namespace partner): the partner's state as the
-- game publishes it, and the commands that ask the game to act on it. The game decides every rule
-- (a bar's mood, how urgent a need is); a command only asks, and the game answers with an event.
--
-- Every function returns a fresh table; changing it changes nothing in the game.

partner = {}

local function whole(name)
  local value = ui.setting(name)
  if type(value) ~= "number" then
    return 0
  end
  return math.floor(value)
end

local function key(name)
  local value = ui.setting(name)
  if type(value) ~= "string" then
    return ""
  end
  return value
end

-- The field's bars, needs and pools in the game's order (packages/screens: field_hud_view.hpp).
local meters = { "happiness", "discipline" }
local pools = { "hp", "mp" }
local needs = { "hunger", "sleep", "toilet", "tired", "injured", "sick" }
local areas = { [1] = "liked", [2] = "disliked" }

local function meter(index)
  local base = "hud.meter." .. (index - 1) .. "."
  return {
    id = meters[index],
    label_key = key(base .. "label"),
    value = whole(base .. "value"),
    min = whole(base .. "min"),
    max = whole(base .. "max"),
    two_tone = ui.setting(base .. "two_tone") == true,
    mood = ui.setting(base .. "mood") == "low" and "low" or "high",
  }
end

local function pool(index)
  local base = "hud.pool." .. (index - 1) .. "."
  return {
    id = pools[index],
    label_key = key(base .. "label"),
    value = whole(base .. "value"),
    max = whole(base .. "max"),
  }
end

-- The partner as the field shows it: its bars (meters: happiness and discipline, each with
-- label_key, value, min, max, two_tone and mood, "low" below the middle of its range), HP and MP
-- (pools: label_key, value, max) and how it feels about the map (area: "liked", "disliked" or nil).
function partner.status()
  local status = { meters = {}, pools = {}, area = areas[ui.setting("hud.partner.area")] }
  for index = 1, #meters do
    status.meters[index] = meter(index)
  end
  for index = 1, #pools do
    status.pools[index] = pool(index)
  end
  return status
end

-- The partner's needs in the game's order, each {id, label_key, active, urgency}: urgency ranks
-- the active ones, 1 the one that does its lasting harm first; 0 for a need it does not have.
function partner.needs()
  local list = {}
  for index, id in ipairs(needs) do
    local base = "hud.need." .. (index - 1) .. "."
    list[index] = {
      id = id,
      label_key = key(base .. "label"),
      active = ui.setting(base .. "active") == true,
      urgency = whole(base .. "urgency"),
    }
  end
  return list
end

-- Where the partner's head is on the window, for what a UI hangs over it: visible (false while the
-- field is stopped or the partner is off the window), x and y in percent of the window, and the
-- evolving bubble's frame (1 to 51) while the partner starts to evolve, else nil.
function partner.bubble()
  local x, y = ui.setting("hud.partner.x"), ui.setting("hud.partner.y")
  local evolving = ui.setting("hud.partner.evolving")
  return {
    visible = ui.setting("hud.partner.visible") == true,
    x = type(x) == "number" and x or 0,
    y = type(y) == "number" and y or 0,
    evolving = (type(evolving) == "number" and evolving >= 1) and math.floor(evolving) or nil,
  }
end

-- The partner's care counters the original never shows (care mistakes, battles, hours lived), or
-- nil while the player keeps the status extras off: a UI shows no more than the game's options
-- allow.
function partner.extras()
  if ui.setting("status_extras") == false then
    return nil
  end
  return {
    care_mistakes = whole("partner.care_mistakes"),
    battles = whole("partner.battles"),
    lifetime_hours = whole("partner.lifetime_hours"),
  }
end

-- Asks the game to praise or scold the partner. Returns true when the request is queued; the
-- game answers with partner.changed.
function partner.praise()
  ui.send("partner_praise")
  return true
end

function partner.scold()
  ui.send("partner_scold")
  return true
end

-- Asks the game to equip or take off the technique of the species' slot (0 to 255).
function partner.toggle_technique(slot)
  if type(slot) ~= "number" or slot ~= math.floor(slot) or slot < 0 or slot > 255 then
    return false, "slot must be a whole number from 0 to 255"
  end
  ui.send("partner_toggle_technique", slot)
  return true
end

-- partner.changed: something of the partner changed (the event's name says which setting);
-- partner.bubble_moved: its head moved on the window.
event_source("hud.revision", "partner.changed")
event_source("partner.revision", "partner.changed")
event_source("hud.meter.0.label", "partner.changed")
event_source("hud.meter.1.label", "partner.changed")
event_source("hud.pool.0.label", "partner.changed")
event_source("hud.pool.1.label", "partner.changed")
event_source("hud.need.0.label", "partner.changed")
event_source("hud.need.1.label", "partner.changed")
event_source("hud.need.2.label", "partner.changed")
event_source("hud.need.3.label", "partner.changed")
event_source("hud.need.4.label", "partner.changed")
event_source("hud.need.5.label", "partner.changed")
event_source("hud.partner.revision", "partner.bubble_moved")
