-- The tension gauge of the fishing. The game publishes fishing_hud.tension, from 0 (slack) to 1
-- (the line's limit), and shows this document without taking the input.

-- From this much on the bar warns that the line may snap.
local Danger = 0.85

local function refresh(document)
  local tension = math.max(0, math.min(1, ui.setting("fishing_hud.tension") or 0))
  local fill = document:get("fill")
  fill:set_property("width", math.floor(tension * 100 + 0.5) .. "%")
  fill:set_class("danger", tension >= Danger)
end

function on_show(document)
  refresh(document)
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(_document) end

function on_device(_document) end

function on_action(_document, _action)
  return false
end
