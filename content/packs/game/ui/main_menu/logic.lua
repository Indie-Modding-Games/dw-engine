-- The title screen's menu, with no game behind it: play (a new game), the language, the text
-- font, the skin, quit. Like the pause menu it never changes a setting itself: it sends the
-- language, text_font and ui_skin commands and shows what the game reports back (ui.setting). The
-- skin entry is common/ui_skin_choice.lua, shared with the pause menu.

-- The language after the current one in the list the packs offer, step entries away, wrapping
-- around.
local function next_language(step)
  local languages = ui.languages()
  local current = 1
  for index, language in ipairs(languages) do
    if language == ui.setting("language") then
      current = index
    end
  end
  return languages[((current - 1 + step) % #languages) + 1]
end

-- The fonts "original look" text can draw with (text_font): two, so left and right both switch to
-- the other one.
local function other_text_font()
  return ui.setting("text_font") == "original" and "modern" or "original"
end

local function refresh(document)
  document:get("language-value"):set_text(t("language.name"))
  local font = ui.setting("text_font") == "original" and "original" or "modern"
  document:get("text-font-value"):set_text(t("main_menu.text_font." .. font))
  document:get("ui-skin-value"):set_text(skin_choice.name())
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
end

function play()
  ui.hide(document_name)
  ui.send("play")
end

function change_language(step)
  ui.send("language", next_language(step))
end

function change_text_font()
  ui.send("text_font", other_text_font())
end

function on_show(document)
  document:get("play"):focus()
  refresh(document)
  refresh_prompts(document)
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
  refresh_prompts(document)
end

function on_device(document)
  refresh_prompts(document)
end

-- Navigation steps arrive here first; returning true suppresses the default. Cancel and menu do not
-- close the main menu: the player leaves it with Play or Quit.
function on_action(document, action)
  if action == "cancel" or action == "menu" then
    return true
  end
  if (action == "left" or action == "right") and document:get("language"):has_focus() then
    change_language(action == "left" and -1 or 1)
    return true
  end
  if (action == "left" or action == "right") and document:get("text-font"):has_focus() then
    change_text_font()
    return true
  end
  if (action == "left" or action == "right") and document:get("ui-skin"):has_focus() then
    skin_choice.change(action == "left" and -1 or 1)
    return true
  end
  return false
end
