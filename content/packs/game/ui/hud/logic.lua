-- The HUD follows the minimap settings the game reports: shown or hidden, and its size.

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

function on_show(document)
	refresh(document)
end

function on_setting(document, name)
	refresh(document)
end
