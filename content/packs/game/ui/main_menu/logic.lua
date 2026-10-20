-- The main menu, shown when the game starts: play, choose the language, quit. Like the pause menu it never changes a
-- setting itself: it sends the language command and shows the language the game reports back (ui.setting).

-- The language after the current one in the list the packs offer, step entries away, wrapping around.
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

local function refresh(document)
	document:get("language-value"):set_text(t("language.name"))
end

local function refresh_prompts(document)
	document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
end

function play()
	ui.hide(document_name)
end

function change_language(step)
	ui.send("language", next_language(step))
end

function on_show(document)
	document:get("play"):focus()
	refresh(document)
	refresh_prompts(document)
end

function on_setting(document, name)
	refresh(document)
end

function on_language(document)
	refresh(document)
	refresh_prompts(document)
end

function on_device(document)
	refresh_prompts(document)
end

-- Navigation steps arrive here first; returning true suppresses the default. Cancel and menu do not close the main
-- menu: the player leaves it with Play or Quit.
function on_action(document, action)
	if action == "cancel" or action == "menu" then
		return true
	end
	if (action == "left" or action == "right") and document:get("language"):has_focus() then
		change_language(action == "left" and -1 or 1)
		return true
	end
	return false
end
