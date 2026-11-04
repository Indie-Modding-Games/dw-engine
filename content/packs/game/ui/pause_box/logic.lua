-- The pause box (utils2.c: handlePause): the box itself is the document's. Under it the engine
-- names the key that opens the game's options from the pause (docs/modulos/apps/game.md, «Opciones
-- en partida»), on the device in use. Only on a gamepad: `settings` has no keyboard key (the pad's
-- Select opens it), so the hint would name no key on the keyboard.

local function refresh_hint(document)
  local hint = document:get("hint")
  if ui.device() == "gamepad" then
    hint:set_prompt_text(t("pause_menu.settings_hint", { key = "{prompt:settings}" }))
  else
    hint:set_prompt_text("")
  end
end

function on_show(document)
  refresh_hint(document)
end

function on_device(document)
  refresh_hint(document)
end

function on_language(document)
  refresh_hint(document)
end
