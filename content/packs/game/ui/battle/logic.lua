-- The battle screen. The game publishes the battle's view as settings (apps/screens:
-- battle_view.hpp) and bumps battle.revision once per change; this script only draws them and sends
-- the player's choices back with ui.send. No rule of the battle lives here: which commands exist,
-- which are available and what they do is the game's.

local max_fighters = 4
local max_commands = 11
local finisher_segments = 6
local finisher_marks = 8
local max_anger = 3
local banner_pieces = 155
-- The hovered command's icon swaps to its second frame every 8 of the original's battle frames
-- (command_menu.c: BTL_renderCommandMenu).
local blink_seconds = 0.4
local max_end_lines = 10
local stat_count = 6
local max_bag_slots = 30

-- The original's labels (main.stat_labels: HP, MP, offense, defense, speed, brain;
-- main.battle_ui_bits_labels).
local stat_label_keys = {}
for index = 0, stat_count - 1 do
  stat_label_keys[index] = string.format("main.stat_labels.%04d", index)
end
local bits_label_key = "main.battle_ui_bits_labels.0000"

local phase = "fight"
local bag_open = false
local intro_step = "done"
local blink_clock = 0
local blink_on = false
local icon_classes = {}
local frame_classes = {}
local shown_pieces = {}

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
  local hp, max_hp =
    number(entry("battle.fighter", index, "hp")), number(entry("battle.fighter", index, "max_hp"))
  local mp, max_mp =
    number(entry("battle.fighter", index, "mp")), number(entry("battle.fighter", index, "max_mp"))
  document
    :get(prefix .. "-name")
    :set_text(text(setting(entry("battle.fighter", index, "name"), "")))
  document
    :get(prefix .. "-status")
    :set_text(text(setting(entry("battle.fighter", index, "status"), "")))
  document:get(prefix .. "-hp-label"):set_text(t(stat_label_keys[0]))
  document:get(prefix .. "-hp"):set_text(tostring(hp))
  document:get(prefix .. "-hp-fill"):set_property("width", percent(hp, max_hp) .. "%")
  if index == 0 then
    -- The partner's HUD shows HP and MP all the time (battle_hud.c: BTL_renderPartnerStatusBars).
    document:get(prefix .. "-mp-label"):set_text(t(stat_label_keys[1]))
    document:get(prefix .. "-mp"):set_text(tostring(mp))
    document:get(prefix .. "-mp-fill"):set_property("width", percent(mp, max_mp) .. "%")
  else
    -- An enemy's HP bar only shows for a while after it takes damage, and it has no MP bar
    -- (battle_main.c: BTL_renderEnemyHPBars).
    document
      :get(prefix .. "-hp-row")
      :set_class("hidden", not setting(entry("battle.fighter", index, "hp_bar"), false))
  end
  panel:set_class("fainted", setting(entry("battle.fighter", index, "fainted"), false))
end

local function command_available(index)
  return setting(entry("battle.menu", index, "available"), false)
end

-- Swaps an element's class from one set to another: icons, places and frame kinds.
local function swap_class(element, classes, id, class)
  if classes[id] ~= nil and classes[id] ~= class then
    element:set_class(classes[id], false)
  end
  if class ~= nil then
    element:set_class(class, true)
  end
  classes[id] = class
end

-- A command's place in the original's strip (command_menu.c: BTL_renderCommandMenu): the last
-- command is the first icon from the left, each one 14 pixels on, every other one lower. The first
-- command, the finisher, has no icon there.
local function command_place(index, count)
  local place = count - 1 - index
  return "place-" .. place, place % 2 == 0
end

-- The strip's frame behind the icons, one piece per command: the left end, the middle pieces in
-- turn, and the right end that matches the last middle one.
local function frame_kind(index, count)
  if index == 0 then
    return "frame-left"
  end
  if index == count - 1 then
    return count % 2 == 1 and "frame-right-odd" or "frame-right-even"
  end
  return index % 2 == 1 and "frame-odd" or "frame-even"
end

local function draw_commands(document)
  local count = number("battle.menu.count")
  for index = 0, max_commands - 1 do
    local button = document:get("command-" .. index)
    local id = "command-" .. index
    local frame = document:get("command-frame-" .. index)
    button:set_class("hidden", index >= count)
    frame:set_class("hidden", index >= count)
    if index < count then
      button:set_text(text(setting(entry("battle.menu", index, "label"), "")))
      button:set_class("unavailable", not command_available(index))
      button:set_class("current", setting(entry("battle.menu", index, "current"), false))
      local icon = setting(entry("battle.menu", index, "icon"), "")
      button:set_class("no-icon", icon == "")
      swap_class(button, icon_classes, id, icon ~= "" and ("icon-" .. icon) or nil)
      local place, up = command_place(index, count)
      swap_class(button, frame_classes, id, icon ~= "" and place or nil)
      button:set_class("up", icon ~= "" and up)
      button:set_class("down", icon ~= "" and not up)
      swap_class(frame, frame_classes, "frame-" .. index, frame_kind(index, count))
    end
  end
  -- The bag's entry follows the strip, where a skin that draws it wants it.
  swap_class(document:get("open-bag"), frame_classes, "open-bag", "place-" .. count)
end

-- The cursor round the hovered icon, and its blink; nothing while the battle opens.
local function draw_cursor(document)
  local cursor = document:get("command-cursor")
  local count = number("battle.menu.count")
  local hovered = nil
  for index = 0, count - 1 do
    local button = document:get("command-" .. index)
    local focused = button:has_focus() and setting(entry("battle.menu", index, "icon"), "") ~= ""
    button:set_class("blink", focused and blink_on)
    if focused then
      hovered = index
    end
  end
  cursor:set_class("hidden", hovered == nil or phase ~= "fight")
  if hovered ~= nil then
    local place, up = command_place(hovered, count)
    swap_class(cursor, frame_classes, "cursor", place)
    cursor:set_class("up", up)
    cursor:set_class("down", not up)
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
  if document:get("open-bag"):has_focus() then
    return
  end
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
  local ready = setting("battle.finisher.ready", false)
  -- The gauge's word has two marks more than its steps; they show once it is full.
  if ready then
    filled = finisher_marks
  end
  for index = 0, finisher_marks - 1 do
    document:get("finisher-" .. index):set_class("filled", index < filled)
  end
  document:get("finisher"):set_class("ready", ready)
  local charging = setting("battle.finisher.charging", false)
  document:get("charge"):set_class("hidden", not charging)
  document
    :get("charge-fill")
    :set_property("width", math.min(100, number("battle.finisher.charge")) .. "%")
end

local function draw_revive(document)
  local seconds = number("battle.revive_seconds")
  document:get("revive"):set_class("hidden", seconds < 0)
  if seconds >= 0 then
    document:get("revive"):set_text(t("battle.revive_countdown", { seconds = seconds }))
  end
end

local function slot_setting(index, field)
  return entry("bag.slot", index, field)
end

local function slot_filled(index)
  return index < number("bag.slot.count") and not setting(slot_setting(index, "empty"), true)
end

-- Only the items of a battle colour can be thrown (inventory.c: selectOptionMenu); the others show
-- greyed out.
local function slot_usable(index)
  return slot_filled(index) and setting(slot_setting(index, "usable"), false)
end

-- The tamer's bag: open with its own button while the battle goes on (battle_tick.c:
-- tamerTickBattle); the command row waits meanwhile (handleCommands returns while it is open).
local function draw_bag(document)
  document:get("commands"):set_class("hidden", bag_open)
  document:get("bag"):set_class("hidden", not bag_open)
  document:get("open-bag"):set_class("unavailable", not setting("battle.bag.available", false))
  document:get("item-in-flight"):set_class("hidden", not setting("battle.item_in_flight", false))
  if not bag_open then
    return
  end
  local count = number("bag.slot.count")
  for index = 0, max_bag_slots - 1 do
    local slot = document:get("bag-slot-" .. index)
    slot:set_class("hidden", index >= count)
    if index < count then
      local filled = slot_filled(index)
      document
        :get("bag-slot-" .. index .. "-name")
        :set_text(
          filled and text(setting(slot_setting(index, "name"), "")) or t("battle.bag.empty")
        )
      document
        :get("bag-slot-" .. index .. "-count")
        :set_text(filled and tostring(number(slot_setting(index, "count"))) or "")
      slot:set_class("unusable", not slot_usable(index))
    end
  end
end

-- The description of the focused item under the bag.
local function draw_bag_description(document)
  local description = ""
  for index = 0, number("bag.slot.count") - 1 do
    if document:get("bag-slot-" .. index):has_focus() and slot_filled(index) then
      description = text(setting(slot_setting(index, "description"), ""))
    end
  end
  document:get("bag-description"):set_text(description)
end

local function focus_bag(document)
  local count = math.min(number("bag.slot.count"), max_bag_slots)
  for index = 0, count - 1 do
    if document:get("bag-slot-" .. index):has_focus() then
      return
    end
  end
  if count > 0 then
    document:get("bag-slot-0"):focus()
  end
end

-- The end boxes (battle_ui.c: handleBattleEndBox). The stats box counts each raise up a point a
-- frame after battle.end.count_delay_frames (renderPostBattleStatsBox, tickPostBattleStatsBox);
-- then the final balance box opens and the bits move from the bits box into it, one a frame
-- (tickBitBox). Meanwhile the lines are typed a character a frame into two rows that scroll, and
-- each waits battle.end.wait_frames (BTL_drawBattleEndText, BTL_tickBattleEndText). A press skips
-- what runs: the count, the bits, the wait or the typing. The boxes close by themselves once all is
-- done; there is no button to close them. Each point and bit counted asks for the count's sound,
-- and the bits all in for the sound that ends it (tickBitBox, renderPostBattleStatsBox).
local end_rows_shown = 2
local end_segments = 5
local ending = nil

-- A line's rows: its text with the line breaks as rows and each row cut at the placeholders, so the
-- names show in their own colour: plain, name, plain, name, plain.
local function rows_of(template, values)
  local rows = {}
  for row in (template .. "\n"):gmatch("(.-)\n") do
    local segments = {}
    local rest = row
    while #segments < end_segments - 1 do
      local first, last, name = rest:find("{(%w+)}")
      if first == nil then
        break
      end
      table.insert(segments, rest:sub(1, first - 1))
      table.insert(segments, values[name] or "")
      rest = rest:sub(last + 1)
    end
    table.insert(segments, rest)
    table.insert(rows, segments)
  end
  return rows
end

local function row_length(row)
  local length = 0
  for _, segment in ipairs(row) do
    length = length + #segment
  end
  return length
end

-- Every row of the end box's lines; the last row of each line waits.
local function end_rows()
  local rows = {}
  for index = 0, math.min(number("battle.end.count"), max_end_lines) - 1 do
    local values = {
      name = text(setting(entry("battle.end", index, "name"), "")),
      subject = text(setting(entry("battle.end", index, "subject"), "")),
      amount = tostring(number(entry("battle.end", index, "amount"))),
    }
    local line = rows_of(text(setting(entry("battle.end", index, "key"), "")), values)
    for row_index, row in ipairs(line) do
      table.insert(rows, { segments = row, waits = row_index == #line })
    end
  end
  return rows
end

local function start_end()
  local stats, gains, maxima = {}, {}, {}
  for index = 0, stat_count - 1 do
    stats[index] = number(entry("battle.end.stat", index, "value"))
    gains[index] = number(entry("battle.end.gain", index, "value"))
    maxima[index] = number(entry("battle.end.stat", index, "max"))
  end
  local bits = number("battle.end.bits")
  ending = {
    frame_seconds = setting("battle.frame_seconds", 0.05),
    seconds = 0,
    delay = number("battle.end.count_delay_frames"),
    wait_frames = number("battle.end.wait_frames"),
    stats = stats,
    gains = gains,
    maxima = maxima,
    bits = bits,
    balance = number("battle.end.money") - bits,
    balance_open = false,
    rows = end_rows(),
    row = 1,
    typed = 0,
    wait = 0,
    closed = false,
  }
  local raised = false
  for index = 0, stat_count - 1 do
    raised = raised or gains[index] > 0
  end
  -- Without raises nothing counts; with nothing to count or type, the outcome stays for a wait.
  if not raised then
    ending.delay = 0
    if bits <= 0 and #ending.rows == 0 then
      ending.wait = ending.wait_frames
    end
  end
end

local function counting()
  for index = 0, stat_count - 1 do
    if ending.gains[index] > 0 then
      return index
    end
  end
  return nil
end

local function text_done()
  return ending.row > #ending.rows and ending.wait <= 0
end

-- One frame of the end boxes.
local function step_end()
  if ending.delay > 0 then
    ending.delay = ending.delay - 1
  else
    local stat = counting()
    if stat ~= nil then
      ending.stats[stat] = ending.stats[stat] + 1
      ending.gains[stat] = ending.gains[stat] - 1
      ui.send("battle_end_count")
    elseif ending.bits > 0 then
      ending.balance_open = true
      ending.bits = ending.bits - 1
      ending.balance = ending.balance + 1
      ui.send("battle_end_count")
      if ending.bits == 0 then
        ui.send("battle_end_counted")
      end
    else
      ending.balance_open = true
    end
  end
  if ending.wait > 0 then
    ending.wait = ending.wait - 1
  elseif ending.row <= #ending.rows then
    local row = ending.rows[ending.row]
    ending.typed = ending.typed + 1
    if ending.typed >= row_length(row.segments) then
      if row.waits then
        ending.wait = ending.wait_frames
      end
      ending.row = ending.row + 1
      ending.typed = 0
    end
  end
end

-- A press skips what runs first: the count, the bits, the wait or the rest of the row.
function press_end()
  if ending == nil then
    return
  end
  if counting() ~= nil then
    for index = 0, stat_count - 1 do
      ending.stats[index] =
        math.min(ending.stats[index] + ending.gains[index], ending.maxima[index])
      ending.gains[index] = 0
    end
    ending.delay = 0
  elseif ending.bits > 0 then
    ending.balance_open = true
    ending.balance = ending.balance + ending.bits
    ending.bits = 0
    ui.send("battle_end_counted")
  elseif ending.wait > 0 then
    ending.wait = 0
  elseif ending.row <= #ending.rows then
    ending.typed = row_length(ending.rows[ending.row].segments) - 1
  end
end

-- The rows on show: the two last begun, the one being typed cut where the typing is.
local function draw_end_rows(document)
  local last = math.min(ending.row, #ending.rows)
  local first = math.max(1, last - end_rows_shown + 1)
  for slot = 0, end_rows_shown - 1 do
    local index = first + slot
    local row = ending.rows[index]
    local left = 0
    if row ~= nil then
      left = index < ending.row and row_length(row.segments) or ending.typed
    end
    for segment = 1, end_segments do
      local piece = row ~= nil and row.segments[segment] or ""
      local shown = piece:sub(1, math.max(0, left))
      left = left - #piece
      document:get("end-row-" .. slot .. "-" .. (segment - 1)):set_text(shown)
    end
  end
end

local function draw_end(document)
  document:get("end-title"):set_text(text(setting("battle.end.outcome", "")))
  for index = 0, stat_count - 1 do
    local gain = ending.gains[index]
    document:get("gain-" .. index .. "-label"):set_text(t(stat_label_keys[index]))
    document:get("stat-" .. index):set_text(tostring(ending.stats[index]))
    document
      :get("stat-" .. index .. "-fill")
      :set_property("width", percent(ending.stats[index], ending.maxima[index]) .. "%")
    document
      :get("stat-" .. index .. "-bar")
      :set_class("raised", number(entry("battle.end.gain", index, "value")) > 0)
    document:get("gain-" .. index .. "-arrow"):set_class("hidden", gain <= 0)
    document:get("gain-" .. index):set_text(gain > 0 and tostring(gain) or "")
  end
  document:get("bits-label"):set_text(t(bits_label_key))
  document:get("bits-value"):set_text(tostring(ending.bits))
  document:get("balance"):set_class("hidden", not ending.balance_open)
  document:get("balance-label"):set_text(t(bits_label_key))
  document:get("balance-value"):set_text(tostring(ending.balance))
  document:get("end-boxes"):set_class("with-text", #ending.rows > 0)
  draw_end_rows(document)
end

local function tick_end(document, seconds)
  ending.seconds = ending.seconds + seconds
  while ending.seconds >= ending.frame_seconds and not ending.closed do
    ending.seconds = ending.seconds - ending.frame_seconds
    step_end()
    if counting() == nil and ending.bits <= 0 and text_done() then
      ending.closed = true
      close_end()
    end
  end
  draw_end(document)
end

local function draw_prompts(document)
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
  document:get("prompt-charge"):set_text(ui.prompt("confirm"))
  document:get("prompt-cancel"):set_text(ui.prompt("cancel"))
end

-- The battle's opening (apps/screens: battle_intro.hpp): the anger marks over the enemies, then the
-- "BATTLE" banner flying together, flashing and bursting over the battle's HUD.
local function draw_anger(document)
  local scale = number("battle.intro.anger.scale") / 100
  for index = 0, max_anger - 1 do
    local mark = document:get("anger-" .. index)
    local shown = intro_step == "anger"
      and setting(entry("battle.intro.anger", index, "shown"), false)
    mark:set_class("hidden", not shown)
    if shown then
      mark:set_property("left", setting(entry("battle.intro.anger", index, "x"), 0) .. "%")
      mark:set_property("top", setting(entry("battle.intro.anger", index, "y"), 0) .. "%")
      mark:set_property("transform", string.format("scale(%.2f)", scale))
    end
  end
end

-- The banner's pieces come as seven numbers each, in the original's pixels (3dp each): x, y,
-- width, height, texture, trail and 1 for a trail on the left.
local function banner_numbers()
  local numbers = {}
  for value in string.gmatch(setting("battle.intro.pieces", ""), "%S+") do
    numbers[#numbers + 1] = tonumber(value)
  end
  return numbers
end

local function draw_piece(document, index, piece)
  local key = table.concat(piece, " ")
  if shown_pieces[index] == key then
    return
  end
  shown_pieces[index] = key
  local block = document:get("bp-" .. index)
  local shadow = document:get("bs-" .. index)
  local trail = document:get("bt-" .. index)
  block:set_class("hidden", piece[1] == nil)
  shadow:set_class("hidden", piece[1] == nil)
  if piece[1] == nil then
    return
  end
  local x, y = piece[1] * 3, piece[2] * 3
  local width, height = piece[3] * 3, piece[4] * 3
  for _, element in ipairs({ block, shadow }) do
    element:set_property("width", width .. "dp")
    element:set_property("height", height .. "dp")
  end
  block:set_property("left", x .. "dp")
  block:set_property("top", y .. "dp")
  -- The shadow lies 2 pixels down and right (battle_hud.c: BTL_renderBattleStartText).
  shadow:set_property("left", (x + 6) .. "dp")
  shadow:set_property("top", (y + 6) .. "dp")
  for texture = 0, 2 do
    block:set_class("texture-" .. texture, piece[5] == texture)
  end
  local length = piece[6] * 3
  trail:set_class("hidden", length <= 0)
  trail:set_class("left", piece[7] == 1)
  if length > 0 then
    trail:set_property("width", length .. "dp")
    trail:set_property("left", (piece[7] == 1 and -length or width) .. "dp")
  end
end

local function draw_banner(document)
  local numbers = banner_numbers()
  local count = math.floor(#numbers / 7)
  for index = 0, banner_pieces - 1 do
    local piece = {}
    if index < count then
      for field = 1, 7 do
        piece[field] = numbers[index * 7 + field]
      end
    end
    draw_piece(document, index, piece)
  end
  local banner = document:get("banner")
  for flash = 0, 2 do
    banner:set_class("flash-" .. flash, number("battle.intro.flash") == flash)
  end
end

local function draw_intro(document)
  intro_step = phase == "intro" and setting("battle.intro.step", "done") or "done"
  document:get("intro"):set_class("hidden", phase ~= "intro")
  -- The HUD is already there under the banner, without the cursor or the hovered command's name.
  document:get("fight"):set_class("opening", phase == "intro")
  if phase ~= "intro" then
    return
  end
  draw_anger(document)
  draw_banner(document)
  -- Veiled, not hidden: the commands keep their boxes, so the focus and the keyboard and gamepad
  -- navigation work on them from the first frame of the fight.
  document:get("fight"):set_class("veiled", intro_step == "anger")
end

local function draw(document)
  phase = setting("battle.phase", "fight")
  document:get("fight"):set_class("hidden", phase ~= "fight" and phase ~= "intro")
  document:get("end"):set_class("hidden", phase ~= "end")
  draw_intro(document)
  if phase == "end" then
    if ending == nil then
      start_end()
    end
    draw_end(document)
    return
  end
  ending = nil
  for index = 0, max_fighters - 1 do
    draw_fighter(document, index)
  end
  bag_open = setting("battle.bag.open", false)
  draw_bag(document)
  if bag_open then
    focus_bag(document)
    draw_bag_description(document)
  else
    draw_commands(document)
    focus_command(document)
    draw_command_name(document)
    draw_cursor(document)
  end
  draw_finisher(document)
  draw_revive(document)
end

function choose(index)
  if command_available(index) then
    ui.send("battle_command", number(entry("battle.menu", index, "command")))
  end
end

function open_bag()
  if setting("battle.bag.available", false) then
    ui.send("battle_bag_open")
  end
end

function close_bag()
  ui.send("battle_bag_close")
end

function throw_slot(index)
  if slot_usable(index) then
    ui.send("battle_throw", index)
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
  if name == "battle.revision" or name == "battle.phase" or name == "bag.revision" then
    draw(document)
  elseif name == "battle.intro.revision" then
    draw_intro(document)
  end
end

function on_tick(document, seconds)
  if phase == "end" and ending ~= nil then
    tick_end(document, seconds)
  elseif phase == "fight" then
    blink_clock = blink_clock + (seconds or 0)
    if blink_clock >= blink_seconds then
      blink_clock = blink_clock - blink_seconds
      blink_on = not blink_on
    end
    if bag_open then
      draw_bag_description(document)
    else
      draw_command_name(document)
      draw_cursor(document)
    end
  end
end

function on_device(document)
  draw_prompts(document)
end

function on_language(document)
  draw(document)
end

-- The battle cannot be left with cancel or the pause button: cancel opens the tamer's bag (the
-- original's own button) and closes it again; up and down move through the open bag; confirm
-- charges the finisher while its aura lasts.
function on_action(_document, action)
  -- Nothing answers while the battle opens.
  if phase == "intro" then
    return true
  end
  if phase == "end" then
    if action == "confirm" or action == "cancel" then
      press_end()
    end
    return true
  end
  if action == "confirm" and setting("battle.finisher.charging", false) then
    charge()
    return true
  end
  if action == "cancel" then
    if bag_open then
      close_bag()
    else
      open_bag()
    end
    return true
  end
  if bag_open then
    return action == "menu"
  end
  return action == "menu" or action == "up" or action == "down"
end
