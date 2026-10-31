-- The save indicator (apps/game/src/save_indicator.hpp): the game publishes how opaque it is
-- (save_indicator.opacity, 0 to 1), how far above its corner it goes to stay clear of the windows
-- there (save_indicator.lift, pixels) and whether the figure's model loaded (save_indicator.figure;
-- without it the text shows alone).

local function refresh(document)
  local indicator = document:get("indicator")
  indicator:set_property("opacity", tostring(ui.setting("save_indicator.opacity") or 0))
  indicator:set_property("margin-bottom", tostring(ui.setting("save_indicator.lift") or 0) .. "px")
  document:get("figure"):set_class("hidden", ui.setting("save_indicator.figure") ~= true)
end

function on_show(document)
  refresh(document)
end

function on_setting(document, name)
  if string.sub(name, 1, 15) == "save_indicator." then
    refresh(document)
  end
end
