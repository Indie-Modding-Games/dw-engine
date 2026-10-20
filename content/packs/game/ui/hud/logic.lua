-- The HUD follows the minimap settings the game reports: shown or hidden, and its size. Its menu button asks the game
-- to open the pause menu (the open_menu command) and shows the control that opens it with the last device used. On a
-- map change the game reports how dark the screen is (screen_fade, 0 to 1) and the HUD covers everything with black.

local sizes = { small = true, medium = true, large = true }

local function refresh(document)
	local minimap = document:get("minimap")
	minimap:set_class("hidden", ui.setting("minimap_visible") == false)
	local size = ui.setting("minimap_size")
	if not sizes[size] then
		size = "medium"
	end
	for name in pairs(sizes) do
		minimap:set_class("size-" .. name, name == size)
	end
end

local function refresh_prompts(document)
	document:get("prompt-menu"):set_text(ui.prompt("menu"))
end

local function refresh_fade(document)
	local level = ui.setting("screen_fade")
	if type(level) ~= "number" then
		level = 0
	end
	local fade = document:get("screen-fade")
	fade:set_class("shown", level > 0)
	fade:set_property("opacity", string.format("%.3f", level))
end

function on_show(document)
	refresh(document)
	refresh_fade(document)
	refresh_prompts(document)
end

function on_device(document)
	refresh_prompts(document)
end

function on_setting(document, name)
	if name == "screen_fade" then
		refresh_fade(document)
	else
		refresh(document)
	end
end
