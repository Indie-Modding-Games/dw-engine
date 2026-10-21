-- The battle screen. The game publishes the battle's view as settings (apps/screens: battle_view.hpp) and bumps
-- battle.revision once per change; this script only draws them and sends the player's choices back with ui.send.
-- No rule of the battle lives here: which commands exist, which are available and what they do is the game's.

local max_fighters = 4
local max_commands = 11
local finisher_segments = 6
local max_end_lines = 10
local stat_count = 6

-- The original's labels (main.stat_labels: HP, MP, offense, defense, speed, brain; main.battle_ui_bits_labels).
local stat_label_keys = {}
for index = 0, stat_count - 1 do
	stat_label_keys[index] = string.format("main.stat_labels.%04d", index)
end
local bits_label_key = "main.battle_ui_bits_labels.0000"

local phase = "fight"

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

local function entry(list, index, field)
	return list .. "." .. index .. "." .. field
end

-- A key's text, or nothing for an empty key.
local function text(key, arguments)
	if key == nil or key == "" then
		return ""
	end
	return t(key, arguments)
end

local function percent(value, maximum)
	if maximum <= 0 then
		return 0
	end
	return math.max(0, math.min(100, value * 100 / maximum))
end

local function draw_fighter(document, index)
	local panel = document:get("fighter-" .. index)
	local count = number("battle.fighter.count")
	panel:set_class("hidden", index >= count)
	if index >= count then
		return
	end
	local prefix = "fighter-" .. index
	local hp, max_hp = number(entry("battle.fighter", index, "hp")), number(entry("battle.fighter", index, "max_hp"))
	local mp, max_mp = number(entry("battle.fighter", index, "mp")), number(entry("battle.fighter", index, "max_mp"))
	document:get(prefix .. "-name"):set_text(text(setting(entry("battle.fighter", index, "name"), "")))
	document:get(prefix .. "-status"):set_text(text(setting(entry("battle.fighter", index, "status"), "")))
	document:get(prefix .. "-hp-label"):set_text(t(stat_label_keys[0]))
	document:get(prefix .. "-mp-label"):set_text(t(stat_label_keys[1]))
	document:get(prefix .. "-hp"):set_text(tostring(hp))
	document:get(prefix .. "-mp"):set_text(tostring(mp))
	document:get(prefix .. "-hp-fill"):set_property("width", percent(hp, max_hp) .. "%")
	document:get(prefix .. "-mp-fill"):set_property("width", percent(mp, max_mp) .. "%")
	panel:set_class("fainted", setting(entry("battle.fighter", index, "fainted"), false))
end

local function command_available(index)
	return setting(entry("battle.menu", index, "available"), false)
end

local function draw_commands(document)
	local count = number("battle.menu.count")
	for index = 0, max_commands - 1 do
		local button = document:get("command-" .. index)
		button:set_class("hidden", index >= count)
		if index < count then
			button:set_text(text(setting(entry("battle.menu", index, "label"), "")))
			button:set_class("unavailable", not command_available(index))
			button:set_class("current", setting(entry("battle.menu", index, "current"), false))
		end
	end
end

-- The name of the focused command under the row, as the original names the hovered icon.
local function draw_command_name(document)
	local name = ""
	for index = 0, number("battle.menu.count") - 1 do
		if document:get("command-" .. index):has_focus() then
			name = text(setting(entry("battle.menu", index, "label"), ""))
		end
	end
	document:get("command-name"):set_text(name)
end

local function focus_command(document)
	local count = number("battle.menu.count")
	for index = 0, count - 1 do
		if document:get("command-" .. index):has_focus() then
			return
		end
	end
	for index = 0, count - 1 do
		if setting(entry("battle.menu", index, "current"), false) then
			document:get("command-" .. index):focus()
			return
		end
	end
	if count > 0 then
		document:get("command-0"):focus()
	end
end

local function draw_finisher(document)
	local progress, goal = number("battle.finisher.progress"), number("battle.finisher.goal")
	local filled = goal > 0 and math.floor(progress * finisher_segments / goal) or 0
	for index = 0, finisher_segments - 1 do
		document:get("finisher-" .. index):set_class("filled", index < filled)
	end
	document:get("finisher"):set_class("ready", setting("battle.finisher.ready", false))
	local charging = setting("battle.finisher.charging", false)
	document:get("charge"):set_class("hidden", not charging)
	document:get("charge-fill"):set_property("width", math.min(100, number("battle.finisher.charge")) .. "%")
end

local function draw_revive(document)
	local seconds = number("battle.revive_seconds")
	document:get("revive"):set_class("hidden", seconds < 0)
	if seconds >= 0 then
		document:get("revive"):set_text(t("battle.revive_countdown", { seconds = seconds }))
	end
end

local function draw_end(document)
	document:get("end-title"):set_text(text(setting("battle.end.outcome", "")))
	local count = number("battle.end.count")
	for index = 0, max_end_lines - 1 do
		local line = document:get("end-line-" .. index)
		line:set_class("hidden", index >= count)
		if index < count then
			line:set_text(text(setting(entry("battle.end", index, "key"), ""), {
				name = text(setting(entry("battle.end", index, "name"), "")),
				amount = number(entry("battle.end", index, "amount")),
			}))
		end
	end
	for index = 0, stat_count - 1 do
		document:get("gain-" .. index .. "-label"):set_text(t(stat_label_keys[index]))
		document:get("gain-" .. index):set_text("+" .. number(entry("battle.end.gain", index, "value")))
	end
	document:get("bits-label"):set_text(t(bits_label_key))
	document:get("bits-value"):set_text(tostring(number("battle.end.bits")))
end

local function draw_prompts(document)
	document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
	document:get("prompt-charge"):set_text(ui.prompt("confirm"))
end

local function draw(document)
	phase = setting("battle.phase", "fight")
	document:get("fight"):set_class("hidden", phase ~= "fight")
	document:get("end"):set_class("hidden", phase ~= "end")
	if phase == "end" then
		draw_end(document)
		document:get("end-continue"):focus()
		return
	end
	for index = 0, max_fighters - 1 do
		draw_fighter(document, index)
	end
	draw_commands(document)
	focus_command(document)
	draw_command_name(document)
	draw_finisher(document)
	draw_revive(document)
end

function choose(index)
	if command_available(index) then
		ui.send("battle_command", number(entry("battle.menu", index, "command")))
	end
end

function charge()
	ui.send("battle_charge")
end

function close_end()
	ui.send("battle_close")
end

function on_show(document)
	draw(document)
	draw_prompts(document)
end

function on_setting(document, name)
	if name == "battle.revision" or name == "battle.phase" then
		draw(document)
	end
end

function on_tick(document)
	if phase == "fight" then
		draw_command_name(document)
	end
end

function on_device(document)
	draw_prompts(document)
end

function on_language(document)
	draw(document)
end

-- The battle cannot be left with cancel or the pause button; confirm charges the finisher while its aura lasts.
function on_action(document, action)
	if phase == "end" then
		if action == "cancel" or action == "menu" then
			close_end()
			return true
		end
		return action == "up" or action == "down"
	end
	if action == "confirm" and setting("battle.finisher.charging", false) then
		charge()
		return true
	end
	return action == "cancel" or action == "menu" or action == "up" or action == "down"
end
