-- The interface skin entry the main menu and the pause menu share. Like every option it never
-- changes the setting itself: it sends the ui_skin command and shows what the game reports back
-- (ui.setting("ui_skin")). The skins are the ones the mounted packs offer (ui.skins()), so a mod's
-- skin shows up here without touching this file.

skin_choice = {}

-- The skin after the current one in the list, step entries away, wrapping around.
function skin_choice.next(step)
  local skins = ui.skins()
  if #skins == 0 then
    return nil
  end
  local current = 1
  for index, skin in ipairs(skins) do
    if skin == ui.setting("ui_skin") then
      current = index
    end
  end
  return skins[((current - 1 + step) % #skins) + 1]
end

-- The current skin's name in the player's language.
function skin_choice.name()
  local key = ui.skin_name_key(ui.setting("ui_skin") or "")
  return key ~= "" and t(key) or ""
end

function skin_choice.change(step)
  local skin = skin_choice.next(step)
  if skin ~= nil then
    ui.send("ui_skin", skin)
  end
end
