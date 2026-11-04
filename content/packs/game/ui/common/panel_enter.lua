-- A panel that fades in when its document shows. on_show calls panel_enter.start, which puts the
-- class "panel-enter" on the element #panel; on_tick calls panel_enter.tick, which takes it off two
-- frames later, once the panel has been laid out without it showing. The skin decides what the
-- class does (the modern look fades the panel in from transparent,
-- ui/skins/modern/pause_menu/style.rcss); a skin that does not style it sees nothing. Only the look
-- changes: the panel takes input and focus while it fades.

panel_enter = {}

local frames_left = 0

function panel_enter.start(document)
  document:get("panel"):set_class("panel-enter", true)
  frames_left = 2
end

function panel_enter.tick(document)
  if frames_left <= 0 then
    return
  end
  frames_left = frames_left - 1
  if frames_left == 0 then
    document:get("panel"):set_class("panel-enter", false)
  end
end
