-- The pause menu: three pages (main, minimap, options). The menu never changes a setting itself: it sends a command to
-- the game with ui.send and shows what the game reports back through ui.setting, so the labels always tell the truth.
-- Every text the player sees comes from the texts in the player's language: t(key) here, text-key in the document.

local pages = { "main", "minimap", "options" }
local first_entry = { main = "continue", minimap = "minimap-visible", options = "display-mode" }
-- The entry of the main page that opened each page, focused again when going back.
local opener = { minimap = "open-minimap", options = "open-options" }

local current = "main"

local minimap_sizes = { "small", "medium", "large" }
local ui_scales = { 0.75, 0.9, 1.0, 1.15, 1.3, 1.5, 1.75, 2.0 }

local function on_off(value)
	return t(value and "common.yes" or "common.no")
end

local function index_of(list, value)
	for index, item in ipairs(list) do
		if item == value then
			return index
		end
	end
	return nil
end

-- The nearest UI scale step to a value, so a scale set elsewhere still cycles from the right place.
local function nearest_scale(value)
	local best = 3
	for index, scale in ipairs(ui_scales) do
		if math.abs(scale - value) < math.abs(ui_scales[best] - value) then
			best = index
		end
	end
	return best
end

local function cycle(list, index, step)
	return ((index - 1 + step) % #list) + 1
end

-- What each option entry does when confirmed or moved left and right.
local options = {
	["minimap-visible"] = function(step)
		ui.send("minimap_visible", not ui.setting("minimap_visible"))
	end,
	["minimap-size"] = function(step)
		local index = index_of(minimap_sizes, ui.setting("minimap_size")) or 2
		ui.send("minimap_size", minimap_sizes[cycle(minimap_sizes, index, step)])
	end,
	["display-mode"] = function(step)
		ui.send("fullscreen", not ui.setting("fullscreen"))
	end,
	["ui-scale"] = function(step)
		local index = nearest_scale(ui.setting("ui_scale") or 1.0)
		ui.send("ui_scale", ui_scales[math.max(1, math.min(#ui_scales, index + step))])
	end,
	["vsync"] = function(step)
		ui.send("vsync", not ui.setting("vsync"))
	end,
	["ui-skin"] = function(step)
		skin_choice.change(step)
	end,
}

local function refresh(document)
	document:get("minimap-visible-value"):set_text(on_off(ui.setting("minimap_visible")))
	local size = index_of(minimap_sizes, ui.setting("minimap_size")) and ui.setting("minimap_size") or "medium"
	document:get("minimap-size-value"):set_text(t("pause_menu.minimap_size." .. size))
	document:get("display-mode-value"):set_text(t(ui.setting("fullscreen") and "pause_menu.display_mode.fullscreen"
		or "pause_menu.display_mode.window"))
	local percent = math.floor((ui.setting("ui_scale") or 1.0) * 100 + 0.5)
	document:get("ui-scale-value"):set_text(t("pause_menu.ui_scale.value", { percent = percent }))
	document:get("vsync-value"):set_text(on_off(ui.setting("vsync")))
	document:get("ui-skin-value"):set_text(skin_choice.name())
end

local function refresh_prompts(document)
	document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
	document:get("prompt-cancel"):set_text(ui.prompt("cancel"))
end

local function show_page(document, name, focus)
	for _, page in ipairs(pages) do
		document:get("page-" .. page):set_class("hidden", page ~= name)
	end
	current = name
	document:get("title"):set_text(t("pause_menu.title." .. name))
	document:get(focus or first_entry[name]):focus()
end

function open_page(document, name)
	show_page(document, name)
end

function back(document)
	if current == "main" then
		ui.hide(document_name)
	else
		show_page(document, "main", opener[current])
	end
end

function resume()
	ui.hide(document_name)
end

function adjust(document, id, step)
	options[id](step)
end

function on_show(document)
	show_page(document, "main")
	refresh(document)
	refresh_prompts(document)
end

function on_setting(document, name)
	refresh(document)
end

function on_device(document)
	refresh_prompts(document)
end

-- The language changed: the elements with a text-key are already in the new one; the rest are written here.
function on_language(document)
	document:get("title"):set_text(t("pause_menu.title." .. current))
	refresh(document)
	refresh_prompts(document)
end

-- Navigation steps arrive here first. Returning true means the step was handled here and suppresses the default
-- (moving the focus, confirming, closing); returning false lets the default run.
function on_action(document, action)
	if action == "cancel" then
		back(document)
		return true
	end
	if action == "menu" then
		ui.hide(document_name)
		return true
	end
	if action == "left" or action == "right" then
		for id, change in pairs(options) do
			local entry = document:get(id)
			if entry ~= nil and entry:has_focus() then
				change(action == "left" and -1 or 1)
				return true
			end
		end
	end
	return false
end
