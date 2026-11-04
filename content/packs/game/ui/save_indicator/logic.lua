-- The save indicator (apps/game/src/save_indicator.hpp): the game publishes how opaque it is
-- (save_indicator.opacity, 0 to 1), how far above its corner it goes to stay clear of the windows
-- there (save_indicator.lift, pixels), whether the figure's model loaded (save_indicator.figure;
-- without it the text shows alone) and the text key to show (save_indicator.text): "Saving", or
-- that an autosave was skipped for want of a free slot, which shows without the figure.

local SavingText = "save.saving"

local function refresh(document)
  local indicator = document:get("indicator")
  indicator:set_property("opacity", tostring(ui.setting("save_indicator.opacity") or 0))
  indicator:set_property("margin-bottom", tostring(ui.setting("save_indicator.lift") or 0) .. "px")
  local text = ui.setting("save_indicator.text") or SavingText
  document:get("label"):set_text(t(text))
  document
    :get("figure")
    :set_class("hidden", ui.setting("save_indicator.figure") ~= true or text ~= SavingText)
end

function on_show(document)
  refresh(document)
end

function on_setting(document, name)
  if string.sub(name, 1, 15) == "save_indicator." then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end
