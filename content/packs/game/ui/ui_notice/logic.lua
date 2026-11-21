-- The notice that the game's own UI came back (docs/modding/ui-addons.md): the UI addon the
-- player picked could not go on (ui.setting("ui_addon.problem") says why, translated; the details
-- go to the log). The game shows it and hides it after a few seconds; it takes no input.

local function refresh(document)
  document:get("title"):set_text(t("ui_notice.addon_failed"))
  local problem = ui.setting("ui_addon.problem") or ""
  local reason = problem ~= "" and (problem .. " " .. t("common.details_logged")) or ""
  document:get("reason"):set_text(reason)
end

function on_show(document)
  refresh(document)
end

function on_language(document)
  refresh(document)
end
