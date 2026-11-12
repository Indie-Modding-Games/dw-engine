-- The pause box of the minimal UI: the game's word for the pause, the game clock and the key that
-- opens the options, all from the UI API (docs/modding/ui-api.md).

local function refresh(document)
  local now = clock.now()
  document:get("label"):set_text(t("pause_menu.title.main"))
  document:get("time"):set_text(string.format("%02d:%02d", now.hour, now.minute))
  document:get("hint"):set_prompt_text(t("pause_menu.settings_hint", { key = "{prompt:settings}" }))
end

function on_show(document)
  refresh(document)
end

function on_device(document)
  refresh(document)
end

function on_language(document)
  refresh(document)
end
