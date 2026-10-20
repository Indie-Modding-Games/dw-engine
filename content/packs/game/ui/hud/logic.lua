-- The HUD follows the minimap settings the game reports: shown or hidden, and its size. Its menu button asks the game
-- to open the pause menu (the open_menu command) and shows the control that opens it with the last device used. On a
-- map change the game subtracts its fade from the scene under the HUD and reports how much it takes away
-- (screen_fade, 0 to 1): the panels fade out with it, as the original's fade covers its HUD. The name of the map it
-- goes to shows in front of the fade, in the player's language: the game reports its text key (map_title_key, empty
-- when none).

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

local panels = { "minimap", "menu-button" }

local function refresh_fade(document)
	local level = ui.setting("screen_fade")
	if type(level) ~= "number" then
		level = 0
	end
	local opacity = string.format("%.3f", 1 - math.min(math.max(level, 0), 1))
	for _, id in ipairs(panels) do
		document:get(id):set_property("opacity", opacity)
	end
end

local function refresh_title(document)
	local key = ui.setting("map_title_key")
	local shown = type(key) == "string" and key ~= ""
	local element = document:get("map-title")
	element:set_text(shown and t(key) or "")
	element:set_class("shown", shown)
end

function on_show(document)
	refresh(document)
	refresh_fade(document)
	refresh_title(document)
	refresh_prompts(document)
end

function on_device(document)
	refresh_prompts(document)
end

-- The language changed: the elements with a text-key are already in the new one; the prompts and the map's name are
-- written here.
function on_language(document)
	refresh_title(document)
	refresh_prompts(document)
end

function on_setting(document, name)
	if name == "screen_fade" then
		refresh_fade(document)
	elseif name == "map_title_key" then
		refresh_title(document)
	else
		refresh(document)
	end
end
