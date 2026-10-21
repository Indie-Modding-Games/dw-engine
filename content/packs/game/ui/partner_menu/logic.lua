-- The partner's menu. The game publishes the partner's status and the bag as settings (apps/screens:
-- partner_status_view.hpp, bag_view.hpp) and bumps partner.revision and bag.revision once per change; this script
-- draws them and sends the player's choices with ui.send. What an item does, whether the partner takes it, how the
-- bag sorts: all of it is the game's.

local pages = { "status", "items", "care" }
local first_entry = { status = "tab-status", items = "slot-0", care = "care-praise" }
local max_slots = 30
local pool_bars = 2
local stat_bars = 4
local meters = 3
local max_conditions = 7

-- The original's labels: the item options (main.item_menu_labels: use, move, sort, drop), the sort orders
-- (main.inventory_sort_labels), the drop's question (main.inventory_confirm_labels) and bits.
local option_names = { "use", "move", "sort", "drop" }
local sort_orders = 3
local bits_label_key = "main.battle_ui_bits_labels.0000"

local function original_label(block, slot)
	return t(string.format("main.%s.%04d", block, slot))
end

local current = "status"
-- The open box over the bag (options, sort-box, confirm-box) and its entries, or nil.
local box = nil
local box_entries = {
	options = { "opt-use", "opt-move", "opt-sort", "opt-drop" },
	["sort-box"] = { "sort-0", "sort-1", "sort-2" },
	["confirm-box"] = { "confirm-yes", "confirm-no" },
}
-- The slot the options are for, and the slot being moved (move picks its destination next).
local picked = nil
local moving = nil

local function setting(name, fallback)
	local value = ui.setting(name)
	if value == nil then
		return fallback
	end
	return value
end

local function number(name)
	return math.floor(setting(name, 0))
end

local function text(key, arguments)
	if key == nil or key == "" then
		return ""
	end
	return t(key, arguments)
end

local function percent(value, minimum, maximum)
	if maximum <= minimum then
		return 0
	end
	return math.max(0, math.min(100, (value - minimum) * 100 / (maximum - minimum)))
end

local function slot_setting(index, field)
	return "bag.slot." .. index .. "." .. field
end

local function slot_filled(index)
	return index < number("bag.slot.count") and not setting(slot_setting(index, "empty"), true)
end

local function draw_bar(document, prefix, list, index)
	local base = list .. "." .. index .. "."
	local value = number(base .. "value")
	document:get(prefix .. "-label"):set_text(text(setting(base .. "label", "")))
	document:get(prefix):set_text(tostring(value))
	document:get(prefix .. "-fill"):set_property("width",
		percent(value, number(base .. "min"), number(base .. "max")) .. "%")
end

-- A care meter. A two-tone one fills the bar from the minimum to the middle of its range, then fills it again in
-- its second colour from the middle to the maximum (game_menu.c: renderDigimonStatsView: happiness, discipline).
local function draw_meter(document, index)
	local prefix = "meter-" .. index
	local base = "partner.meter." .. index .. "."
	draw_bar(document, prefix, "partner.meter", index)
	local high = 0
	if setting(base .. "two_tone", false) then
		local value, low, top = number(base .. "value"), number(base .. "min"), number(base .. "max")
		local middle = (low + top) / 2
		document:get(prefix .. "-fill"):set_property("width", percent(value, low, middle) .. "%")
		high = percent(value, middle, top)
	end
	document:get(prefix .. "-high"):set_property("width", high .. "%")
end

local function draw_status(document)
	document:get("status-name"):set_text(text(setting("partner.name", "")))
	document:get("status-lives"):set_text(t("partner_menu.lives", { lives = number("partner.lives") }))
	document:get("status-age"):set_text(t("partner_menu.age.value", { days = number("partner.age") }))
	document:get("status-weight"):set_text(t("partner_menu.weight.value", { weight = number("partner.weight") }))
	-- HP and MP: the numbers, now and at most, and a bar of the maximum (game_menu.c: renderDigimonStatsView).
	local pool_now = { number("partner.hp"), number("partner.mp") }
	for index = 0, pool_bars - 1 do
		draw_bar(document, "pool-" .. index, "partner.pool", index)
		local maximum = number("partner.pool." .. index .. ".value")
		document:get("pool-" .. index):set_text(pool_now[index + 1] .. " / " .. maximum)
	end
	for index = 0, stat_bars - 1 do
		draw_bar(document, "stat-" .. index, "partner.stat", index)
	end
	for index = 0, meters - 1 do
		draw_meter(document, index)
	end
	local count = number("partner.condition.count")
	for index = 0, max_conditions - 1 do
		local condition = document:get("condition-" .. index)
		condition:set_class("hidden", index >= count)
		if index < count then
			condition:set_text(text(setting("partner.condition." .. index .. ".key", "")))
		end
	end
	document:get("no-condition"):set_class("hidden", count > 0)
end

local function draw_bag(document)
	local count = number("bag.slot.count")
	for index = 0, max_slots - 1 do
		local slot = document:get("slot-" .. index)
		slot:set_class("hidden", index >= count)
		if index < count then
			local filled = slot_filled(index)
			document:get("slot-" .. index .. "-name"):set_text(filled and text(setting(slot_setting(index, "name"), ""))
				or t("partner_menu.bag.empty"))
			document:get("slot-" .. index .. "-count"):set_text(filled and tostring(number(slot_setting(index, "count")))
				or "")
			slot:set_class("dimmed", filled and setting(slot_setting(index, "dimmed"), false))
			slot:set_class("moving", moving == index)
		end
	end
	document:get("bag-money"):set_text(t(bits_label_key) .. " " .. number("bag.money"))
	local result = setting("bag.result", "")
	document:get("bag-result"):set_text(text(result, { name = text(setting("bag.result_item", "")) }))
end

-- The description of the focused item, below the list.
local function draw_description(document)
	local description = ""
	if moving ~= nil then
		description = t("partner_menu.bag.move_prompt", { name = text(setting(slot_setting(moving, "name"), "")) })
	else
		for index = 0, number("bag.slot.count") - 1 do
			if document:get("slot-" .. index):has_focus() and slot_filled(index) then
				description = text(setting(slot_setting(index, "description"), ""))
			end
		end
	end
	document:get("bag-description"):set_text(description)
end

local function draw_care(document)
	document:get("care-result"):set_text(text(setting("partner.care_result", "")))
end

local function draw_boxes(document)
	for index, name in ipairs(option_names) do
		document:get("opt-" .. name):set_text(original_label("item_menu_labels", index - 1))
	end
	for index = 0, sort_orders - 1 do
		document:get("sort-" .. index):set_text(original_label("inventory_sort_labels", index))
	end
	document:get("confirm-text"):set_text(original_label("inventory_confirm_labels", 0))
	document:get("confirm-yes"):set_text(original_label("inventory_confirm_labels", 1))
	document:get("confirm-no"):set_text(original_label("inventory_confirm_labels", 2))
end

local function draw_prompts(document)
	document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
	document:get("prompt-cancel"):set_text(ui.prompt("cancel"))
end

local function draw(document)
	draw_status(document)
	draw_bag(document)
	draw_description(document)
	draw_care(document)
end

local function close_box(document, focus)
	if box ~= nil then
		document:get(box):set_class("hidden", true)
		box = nil
	end
	if focus ~= nil then
		document:get(focus):focus()
	end
end

local function open_box(document, name)
	close_box(document)
	box = name
	document:get(name):set_class("hidden", false)
	document:get(box_entries[name][1]):focus()
end

local function show_page(document, name, focus)
	close_box(document)
	moving = nil
	for _, page in ipairs(pages) do
		document:get("page-" .. page):set_class("hidden", page ~= name)
		document:get("tab-" .. page):set_class("selected", page == name)
	end
	current = name
	draw(document)
	document:get(focus or first_entry[name]):focus()
end

function open_page(document, name)
	show_page(document, name)
end

function pick_slot(document, index)
	if moving ~= nil then
		local from = moving
		moving = nil
		if from ~= index then
			ui.send("bag_move", from .. " " .. index)
		end
		draw_bag(document)
		return
	end
	if not slot_filled(index) then
		return
	end
	picked = index
	open_box(document, "options")
end

function choose_option(document, name)
	local slot = "slot-" .. picked
	if name == "use" then
		close_box(document, slot)
		ui.send("bag_use", picked)
	elseif name == "move" then
		moving = picked
		close_box(document, slot)
		draw_bag(document)
	elseif name == "sort" then
		open_box(document, "sort-box")
	elseif name == "drop" then
		open_box(document, "confirm-box")
		document:get("confirm-no"):focus()
	end
end

function choose_sort(document, mode)
	close_box(document, "slot-0")
	ui.send("bag_sort", mode)
end

function confirm_drop(document, yes)
	close_box(document, "slot-" .. picked)
	if yes then
		ui.send("bag_drop", picked)
	end
end

-- Back: out of a box, out of a move, from a page to its tab, from the tabs out of the menu.
local function back(document)
	if box ~= nil then
		close_box(document, "slot-" .. picked)
	elseif moving ~= nil then
		local slot = "slot-" .. moving
		moving = nil
		draw_bag(document)
		document:get(slot):focus()
	elseif not document:get("tab-" .. current):has_focus() then
		document:get("tab-" .. current):focus()
	else
		ui.hide(document_name)
	end
end

-- Up and down go through the entries of an open box only.
local function step_box(document, step)
	local entries = box_entries[box]
	local at = 1
	for index, id in ipairs(entries) do
		if document:get(id):has_focus() then
			at = index
		end
	end
	document:get(entries[((at - 1 + step) % #entries) + 1]):focus()
end

function on_show(document)
	box = nil
	moving = nil
	for name, _ in pairs(box_entries) do
		document:get(name):set_class("hidden", true)
	end
	draw_boxes(document)
	draw_prompts(document)
	show_page(document, "status")
end

function on_setting(document, name)
	if name == "partner.revision" or name == "bag.revision" or name == "bag.result" or name == "partner.care_result" then
		draw(document)
	end
end

function on_tick(document)
	if current == "items" then
		draw_description(document)
	end
end

function on_device(document)
	draw_prompts(document)
end

function on_language(document)
	draw_boxes(document)
	draw(document)
	draw_prompts(document)
end

function on_action(document, action)
	if action == "cancel" then
		back(document)
		return true
	end
	if action == "menu" then
		ui.hide(document_name)
		return true
	end
	if box ~= nil then
		if action == "up" or action == "down" then
			step_box(document, action == "up" and -1 or 1)
			return true
		end
		return action == "left" or action == "right"
	end
	return false
end
