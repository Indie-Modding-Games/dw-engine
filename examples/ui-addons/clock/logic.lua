-- A clock at the top: the game clock from the UI API (clock.now()), shown while the game time
-- runs. It reads the game and changes nothing, so it needs no command.

local shown = nil

local function refresh(document)
  local now = clock.now()
  local box = document:get("clock")
  box:set_class("hidden", not now.running)
  box:set_class("night", now.night)
  local text = string.format("%02d:%02d", now.hour, now.minute)
  if text ~= shown then
    shown = text
    document:get("time"):set_text(text)
    document:get("day"):set_text(t("hud.minimap.day", { day = now.day }))
  end
end

function on_show(document)
  shown = nil
  refresh(document)
end

function on_tick(document, _seconds)
  refresh(document)
end
