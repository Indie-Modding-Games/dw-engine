-- The notice that the game's own UI came back (docs/modding/interfaces.md): the UI addon the
-- player picked could not go on (ui.setting("ui_addon.problem") says why). The game shows it and
-- hides it after a few seconds; it takes no input.

local function refresh(document)
  document:get("title"):set_text(t("ui_notice.addon_failed"))
  document:get("reason"):set_text(ui.setting("ui_addon.problem") or "")
end

function on_show(document)
  refresh(document)
end

function on_language(document)
  refresh(document)
end
