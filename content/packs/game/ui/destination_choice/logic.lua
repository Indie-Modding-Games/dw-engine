-- Free exploration's chooser: the places the scripts of the cell the player stepped on lead to, whatever the story
-- (docs/architecture/warps-de-script.md). The game publishes them as settings (exploration_places, the count;
-- exploration_story, the place the story takes now, from 0, or -1; exploration_place_name.<n> and
-- exploration_place_map.<n>, from 0) and shows this document; it sends choose_destination with the place, from 0, or
-- stay_here. Pages of eight places, turned with left and right or the page buttons.

local PageSize = 8
local page = 1

local function count()
	return math.floor(ui.setting("exploration_places") or 0)
end

local function pages()
	return math.max(1, math.ceil(count() / PageSize))
end

-- The place of a slot of the current page, from 0, or nil when the slot is empty.
local function place_of(slot)
	local place = (page - 1) * PageSize + slot - 1
	if place < count() then
		return place
	end
	return nil
end

local function label_of(place)
	local key = ui.setting("exploration_place_name." .. place) or ""
	local map = ui.setting("exploration_place_map." .. place) or ""
	local name = key ~= "" and t(key) or map
	local text = t("exploration.place", { name = name, map = map })
	if place == math.floor(ui.setting("exploration_story") or -1) then
		text = text .. " · " .. t("exploration.story")
	end
	return text
end

local function refresh(document)
	for slot = 1, PageSize do
		local place = place_of(slot)
		local entry = document:get("place-" .. slot)
		entry:set_class("hidden", place == nil)
		if place ~= nil then
			entry:set_text(label_of(place))
		end
	end
	local several = pages() > 1
	document:get("previous"):set_class("hidden", not several)
	document:get("next"):set_class("hidden", not several)
	document:get("page"):set_class("hidden", not several)
	document:get("page"):set_text(t("exploration.page", { page = page, pages = pages() }))
end

local function refresh_prompts(document)
	document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
	document:get("prompt-cancel"):set_text(ui.prompt("cancel"))
end

function choose(slot)
	local place = place_of(slot)
	if place ~= nil then
		ui.send("choose_destination", place)
	end
end

function stay()
	ui.send("stay_here", true)
end

function turn(document, step)
	page = ((page - 1 + step) % pages()) + 1
	refresh(document)
	document:get("place-1"):focus()
end

function on_show(document)
	-- The page of the story's place, which the choice starts on; the first place when the story takes none.
	local story = math.floor(ui.setting("exploration_story") or -1)
	page = story >= 0 and math.floor(story / PageSize) + 1 or 1
	refresh(document)
	refresh_prompts(document)
	local slot = story >= 0 and (story % PageSize) + 1 or 1
	document:get("place-" .. slot):focus()
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

-- Cancel and menu stay on the cell; left and right turn the page.
function on_action(document, action)
	if action == "cancel" or action == "menu" then
		stay()
		return true
	end
	if (action == "left" or action == "right") and pages() > 1 then
		turn(document, action == "left" and -1 or 1)
		return true
	end
	return false
end
