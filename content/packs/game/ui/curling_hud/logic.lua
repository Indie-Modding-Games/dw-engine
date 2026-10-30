-- The stones game's information. The game publishes curling_hud.player and curling_hud.opponent
-- (the scores), curling_hud.status (a text key, or empty), curling_hud.prompt (-1 closed, 0 play, 1
-- rules highlighted) and curling_hud.power (the gauge from 0 to 1, or -1 hidden), and shows this
-- document without taking the input.

local function refresh(document)
  document:get("score"):set_text(t("curling.score", {
    player = math.floor(ui.setting("curling_hud.player") or 0),
    opponent = math.floor(ui.setting("curling_hud.opponent") or 0),
  }))
  local status = ui.setting("curling_hud.status") or ""
  document:get("status"):set_text(status ~= "" and t(status) or "")
  local prompt = math.floor(ui.setting("curling_hud.prompt") or -1)
  document:get("prompt"):set_class("hidden", prompt < 0)
  document:get("play"):set_class("selected", prompt == 0)
  document:get("rules"):set_class("selected", prompt == 1)
  local power = ui.setting("curling_hud.power") or -1
  document:get("gauge"):set_class("hidden", power < 0)
  document
    :get("fill")
    :set_property("width", math.floor(math.max(0, math.min(1, power)) * 100 + 0.5) .. "%")
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

function on_device(_document) end

function on_action(_document, _action)
  return false
end
