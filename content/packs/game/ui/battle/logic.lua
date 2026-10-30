-- The battle screen. The game publishes the battle's view as settings (apps/screens:
-- battle_view.hpp) and bumps battle.revision once per change; this script only draws them and sends
-- the player's choices back with ui.send. No rule of the battle lives here: which commands exist,
-- which are available and what they do is the game's.

local max_fighters = 4
local max_commands = 11
local finisher_segments = 6
local finisher_marks = 8
local max_anger = 3
-- Four fighters with up to 8 numbers each, of up to 4 digits (entity_text.c: ENTITY_TEXT_DATA).
local max_numbers = 32
local number_digits = 4
local banner_pieces = 155
-- The command strip's edges once it is in place, in the original's screen lines (0 at the middle;
-- command_menu.c: BTL_COMMAND_MENU_SLIDE_X and _Y), and the icons' height there.
local strip_top = -100
local strip_bottom = -68
local icon_height = 16
-- Whether the strip's pieces carry a transform from its slide.
local strip_moved = false
local max_end_lines = 10
local stat_count = 6

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

-- Elements looked up once: a lookup walks the whole document, and the opening moves over a hundred
-- banner pieces each frame.
local elements = {}
-- The properties last set on each element, by "id property": setting one parses its value again
-- even when it is the same.
local applied = {}
local function lookup(document, id)
  local found = elements[id]
  if found == nil or not found:valid() then
    if found ~= nil then
      -- The document was loaded again (hot reload): none of what was set holds.
      applied = {}
    end
    found = document:get(id)
    elements[id] = found
  end
  return found
end

local function set_property(document, id, property, value)
  local key = id .. " " .. property
  if applied[key] ~= value then
    applied[key] = value
    lookup(document, id):set_property(property, value)
  end
end

local function set_class(document, id, class, on)
  local key = id .. " ." .. class
  if applied[key] ~= on then
    applied[key] = on
    lookup(document, id):set_class(class, on)
  end
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
  local panel = lookup(document, "fighter-" .. index)
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
  lookup(document, prefix .. "-hp-label"):set_text(t(stat_label_keys[0]))
  lookup(document, prefix .. "-hp"):set_text(tostring(hp))
  lookup(document, prefix .. "-hp-fill"):set_property("width", percent(hp, max_hp) .. "%")
  if index == 0 then
    -- The partner's HUD shows HP and MP all the time (battle_hud.c: BTL_renderPartnerStatusBars).
    lookup(document, prefix .. "-mp-label"):set_text(t(stat_label_keys[1]))
    lookup(document, prefix .. "-mp"):set_text(tostring(mp))
    lookup(document, prefix .. "-mp-fill"):set_property("width", percent(mp, max_mp) .. "%")
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
    local button = lookup(document, "command-" .. index)
    local id = "command-" .. index
    local frame = lookup(document, "command-frame-" .. index)
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
  swap_class(lookup(document, "open-bag"), frame_classes, "open-bag", "place-" .. count)
end

-- The cursor round the hovered icon, and its blink; nothing while the battle opens. The game says
-- when the hovered icon shows its second frame (apps/screens: command_menu.hpp).
local function draw_cursor(document)
  local cursor = lookup(document, "command-cursor")
  local count = number("battle.menu.count")
  local blink_on = setting("battle.menu.blink", false)
  local hovered = nil
  for index = 0, count - 1 do
    local button = lookup(document, "command-" .. index)
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
    if lookup(document, "command-" .. index):has_focus() then
      name = text(setting(entry("battle.menu", index, "label"), ""))
    end
  end
  lookup(document, "command-name"):set_text(name)
end

local function focus_command(document)
  local count = number("battle.menu.count")
  if lookup(document, "open-bag"):has_focus() then
    return
  end
  for index = 0, count - 1 do
    if lookup(document, "command-" .. index):has_focus() then
      return
    end
  end
  for index = 0, count - 1 do
    if setting(entry("battle.menu", index, "current"), false) then
      lookup(document, "command-" .. index):focus()
      return
    end
  end
  if count > 0 then
    lookup(document, "command-0"):focus()
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
    lookup(document, "finisher-" .. index):set_class("filled", index < filled)
  end
  lookup(document, "finisher"):set_class("ready", ready)
  local charging = setting("battle.finisher.charging", false)
  lookup(document, "charge"):set_class("hidden", not charging)
  document
    :get("charge-fill")
    :set_property("width", math.min(100, number("battle.finisher.charge")) .. "%")
end

local function draw_revive(document)
  local seconds = number("battle.revive_seconds")
  lookup(document, "revive"):set_class("hidden", seconds < 0)
  if seconds >= 0 then
    lookup(document, "revive"):set_text(t("battle.revive_countdown", { seconds = seconds }))
  end
end

-- The player's bag opens with its own button while the battle goes on (battle_tick.c:
-- tamerTickBattle): the game shows the field's bag (ui/partner_menu, its items page) over the
-- fight, and the command row waits under it (handleCommands returns while it is open).
local function draw_bag(document)
  lookup(document, "open-bag"):set_class("unavailable", not setting("battle.bag.available", false))
  lookup(document, "item-in-flight"):set_class(
    "hidden",
    not setting("battle.item_in_flight", false)
  )
end

-- The end boxes (battle_ui.c: handleBattleEndBox). The stats box counts each raise up a point a
-- frame after battle.end.count_delay_frames (renderPostBattleStatsBox, tickPostBattleStatsBox);
-- then the final balance box opens and the bits move from the bits box into it, one a frame
-- (tickBitBox). A press skips the count or the bits. The boxes close by themselves once all is
-- done and the text is typed and waited; there is no button to close them. Each point and bit
-- counted asks for the count's sound, and the bits all in for the sound that ends it (tickBitBox,
-- renderPostBattleStatsBox).
--
-- The text box (battle_hud.c: BTL_initializeBattleEndText, BTL_drawBattleEndText,
-- BTL_tickBattleEndText, BTL_scrollBattleEndText, BTL_isEndBoxTextFinished) types the lines' rows
-- into two rows that scroll. As the boxes open it types two characters and waits for a press:
-- each press (confirm or cancel) types a character a frame up to the end of the line, whose last
-- row waits battle.end.wait_frames (a press cuts the wait short) and stops the typing until the
-- next press. A row break takes a frame of its own; with both rows full, the next row scrolls in
-- once no wait runs. The box opens with that wait already running, so a press then only ends it.
local end_rows_shown = 2
local end_runs = 5
local ending = nil

-- The marks of the original's text (battle_hud.c: '#C' colour, '#R' row, '#W' row and wait) as
-- t() fills them in: the colours the box uses, a row and a row that waits.
local mark_colour = "\31"
local mark_row = "\30"
local mark_wait = "\29"
local end_marks = {
  ["color:1"] = mark_colour .. "1" .. mark_colour,
  ["color:7"] = mark_colour .. "7" .. mark_colour,
  br = mark_row,
  wait = mark_wait,
}
local end_white = 1
local end_yellow = 7

-- A line's text: its parts (battle_view.hpp: EndLine) joined, the keys looked up with the line's
-- values and the marks, a part that starts with a brace taken as marks itself.
local function line_text(index)
  local arguments = {
    name = text(setting(entry("battle.end", index, "name"), "")),
    subject = text(setting(entry("battle.end", index, "subject"), "")),
    amount = tostring(number(entry("battle.end", index, "amount"))),
  }
  for mark, value in pairs(end_marks) do
    arguments[mark] = value
  end
  local joined = {}
  for part in string.gmatch(setting(entry("battle.end", index, "parts"), ""), "[^|]+") do
    if string.sub(part, 1, 1) == "{" then
      table.insert(joined, (string.gsub(part, "{([^}]*)}", function(mark)
        return arguments[mark] or ""
      end)))
    else
      table.insert(joined, text(part, arguments))
    end
  end
  return table.concat(joined)
end

-- A line's rows: runs of one colour each, the colour going on from row to row; a line whose text
-- does not end in a wait waits after its last row.
local function rows_of_line(line, rows)
  local colour = end_white
  local row = { runs = {}, glyphs = 0 }
  local function add(piece)
    if piece == "" then
      return
    end
    local last = row.runs[#row.runs]
    if last ~= nil and last.colour == colour then
      last.text = last.text .. piece
    elseif #row.runs < end_runs then
      table.insert(row.runs, { text = piece, colour = colour })
    else
      last.text = last.text .. piece
    end
    row.glyphs = row.glyphs + (utf8.len(piece) or #piece)
  end
  local function close(waits)
    row.waits = waits
    table.insert(rows, row)
    row = { runs = {}, glyphs = 0 }
  end
  local at = 1
  local ended = false
  while at <= #line do
    local c = string.sub(line, at, at)
    if c == mark_colour then
      local stop = string.find(line, mark_colour, at + 1, true) or #line
      colour = tonumber(string.sub(line, at + 1, stop - 1)) or end_white
      at = stop + 1
    elseif c == mark_row or c == mark_wait then
      close(c == mark_wait)
      ended = c == mark_wait
      at = at + 1
    else
      local stop = at
      while
        stop <= #line
        and not string.find(
          mark_colour .. mark_row .. mark_wait,
          string.sub(line, stop, stop),
          1,
          true
        )
      do
        stop = stop + 1
      end
      add(string.sub(line, at, stop - 1))
      ended = false
      at = stop
    end
  end
  if not ended then
    close(true)
  end
end

-- Every row of the end box's lines.
local function end_rows()
  local rows = {}
  for index = 0, math.min(number("battle.end.count"), max_end_lines) - 1 do
    rows_of_line(line_text(index), rows)
  end
  return rows
end

-- BTL_drawBattleEndText: one character of the row being typed, or its break: the next row starts
-- and, at the end of a line, the wait.
local function type_glyph()
  local row = ending.rows[ending.row]
  if row == nil then
    return
  end
  if ending.typed < row.glyphs then
    ending.typed = ending.typed + 1
    return
  end
  if row.waits then
    ending.wait = ending.wait_frames
  end
  ending.row = ending.row + 1
  ending.typed = 0
  ending.shown = ending.shown + 1
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
    -- The row being typed, the first row on show, the rows begun since it (BATTLE_END_ROWS_SHOWN),
    -- the characters typed of the row, whether a press set the typing on.
    row = 1,
    top = 1,
    shown = 0,
    typed = 0,
    typing = false,
    wait = number("battle.end.wait_frames"),
    closed = false,
  }
  local raised = false
  for index = 0, stat_count - 1 do
    raised = raised or gains[index] > 0
  end
  -- Without raises nothing counts.
  if not raised then
    ending.delay = 0
  end
  -- handleBattleEndBox: as the boxes open, the first two characters.
  type_glyph()
  type_glyph()
end

local function counting()
  for index = 0, stat_count - 1 do
    if ending.gains[index] > 0 then
      return index
    end
  end
  return nil
end

-- BTL_isEndBoxTextFinished: every row typed and no wait left.
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
  -- BTL_tickBattleEndText.
  if ending.wait > 0 then
    ending.wait = ending.wait - 1
  end
  if #ending.rows == 0 then
    return
  end
  if ending.shown == end_rows_shown then
    if ending.wait == 0 then
      ending.top = ending.top + 1
      ending.shown = ending.shown - 1
    end
  elseif ending.typing then
    type_glyph()
    if ending.wait > 0 then
      ending.typing = false
    end
  end
end

-- A press skips the count or the bits, and the text box takes it too: it ends the wait, or with no
-- wait sets the typing on (BTL_tickBattleEndText).
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
  end
  if ending.wait > 0 then
    ending.wait = 0
  else
    ending.typing = true
  end
end

-- The two rows on show from the first: those typed whole, the one being typed cut where the typing
-- is, each run in its colour.
local function draw_end_rows(document)
  for slot = 0, end_rows_shown - 1 do
    local index = ending.top + slot
    local row = ending.rows[index]
    local left = 0
    if row ~= nil and index <= ending.row then
      left = index < ending.row and row.glyphs or ending.typed
    end
    for run = 1, end_runs do
      local id = "end-row-" .. slot .. "-" .. (run - 1)
      local piece = row ~= nil and row.runs[run] or nil
      local shown = ""
      if piece ~= nil and left > 0 then
        local length = utf8.len(piece.text) or #piece.text
        if left >= length then
          shown = piece.text
        else
          shown =
            string.sub(piece.text, 1, (utf8.offset(piece.text, left + 1) or (#piece.text + 1)) - 1)
        end
        left = left - length
      end
      set_class(document, id, "end-name", piece ~= nil and piece.colour == end_yellow)
      set_class(document, id, "end-plain", piece == nil or piece.colour ~= end_yellow)
      lookup(document, id):set_text(shown)
    end
  end
end

local function draw_end(document)
  lookup(document, "end-title"):set_text(text(setting("battle.end.outcome", "")))
  for index = 0, stat_count - 1 do
    local gain = ending.gains[index]
    lookup(document, "gain-" .. index .. "-label"):set_text(t(stat_label_keys[index]))
    lookup(document, "stat-" .. index):set_text(tostring(ending.stats[index]))
    document
      :get("stat-" .. index .. "-fill")
      :set_property("width", percent(ending.stats[index], ending.maxima[index]) .. "%")
    document
      :get("stat-" .. index .. "-bar")
      :set_class("raised", number(entry("battle.end.gain", index, "value")) > 0)
    lookup(document, "gain-" .. index .. "-arrow"):set_class("hidden", gain <= 0)
    lookup(document, "gain-" .. index):set_text(gain > 0 and tostring(gain) or "")
  end
  lookup(document, "bits-label"):set_text(t(bits_label_key))
  lookup(document, "bits-value"):set_text(tostring(ending.bits))
  lookup(document, "balance"):set_class("hidden", not ending.balance_open)
  lookup(document, "balance-label"):set_text(t(bits_label_key))
  lookup(document, "balance-value"):set_text(tostring(ending.balance))
  lookup(document, "end-boxes"):set_class("with-text", #ending.rows > 0)
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
  lookup(document, "prompt-confirm"):set_text(ui.prompt("confirm"))
  lookup(document, "prompt-charge"):set_text(ui.prompt("confirm"))
  lookup(document, "prompt-cancel"):set_text(ui.prompt("cancel"))
  -- The original's square button calls the finisher (battle_tick.c: handleCommands); here it is
  -- the `extra` action, so the gauge's box names its key or button instead of the square. A name
  -- of more than two letters does not fit where the square was: a skin may move the box for it.
  local finisher_key = ui.prompt("extra")
  lookup(document, "prompt-finisher"):set_text(finisher_key)
  lookup(document, "fight"):set_class("long-finisher-key", (utf8.len(finisher_key) or 0) > 2)
end

-- The battle's opening (apps/screens: battle_intro.hpp): the anger marks over the enemies, then the
-- "BATTLE" banner flying together, flashing and bursting over the battle's HUD.
local function draw_anger(document)
  local scale = number("battle.intro.anger.scale") / 100
  for index = 0, max_anger - 1 do
    local mark = lookup(document, "anger-" .. index)
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

-- The numbers over the fighters (apps/screens: damage_numbers.hpp) come as seven values each: the
-- anchor in percent of the window, the first digit's offset from it in the original's pixels, the
-- value, the colour and the icon. Each digit is 8 pixels square, 7 apart (utils2.c:
-- drawEntityText); the icon, when there is one, is 8 pixels square right before the first digit
-- (entity_text.c: renderEntityText, utils2.c: drawEntityTextIcon).
local number_values = 7
local number_icons = { "hp", "mp", "offense", "defense", "speed" }
local function draw_damage(document)
  local values = {}
  for value in string.gmatch(setting("battle.damage.numbers", ""), "%S+") do
    values[#values + 1] = tonumber(value)
  end
  local count = math.min(max_numbers, math.floor(#values / number_values))
  for index = 0, max_numbers - 1 do
    local id = "dn-" .. index
    set_class(document, id, "hidden", index >= count)
    if index < count then
      local base = index * number_values
      set_property(document, id, "left", values[base + 1] .. "%")
      set_property(document, id, "top", values[base + 2] .. "%")
      set_property(
        document,
        id,
        "transform",
        "translate(" .. (values[base + 3] * 3) .. "dp, " .. (values[base + 4] * 3) .. "dp)"
      )
      set_class(document, id, "poison", values[base + 6] == 12)
      set_class(document, id, "gain", values[base + 6] == 11)
      local icon = number_icons[math.floor(values[base + 7])]
      local icon_id = "di-" .. index
      set_class(document, icon_id, "hidden", icon == nil)
      for _, name in ipairs(number_icons) do
        set_class(document, icon_id, "i-" .. name, icon == name)
      end
      if icon ~= nil and applied[icon_id .. " text"] ~= icon then
        applied[icon_id .. " text"] = icon
        lookup(document, icon_id):set_text(text("battle.number_icon." .. icon))
      end
      local digits = tostring(math.floor(values[base + 5]))
      for place = 0, number_digits - 1 do
        local digit_id = "dd-" .. index .. "-" .. place
        local digit = string.sub(digits, place + 1, place + 1)
        set_class(document, digit_id, "hidden", digit == "")
        for glyph = 0, 9 do
          set_class(document, digit_id, "d-" .. glyph, digit == tostring(glyph))
        end
        if digit ~= "" and applied[digit_id .. " text"] ~= digit then
          applied[digit_id .. " text"] = digit
          lookup(document, digit_id):set_text(digit)
        end
      end
    end
  end
end

-- The enemies' HP bars beside them (apps/screens: enemy_hp_bars.hpp) come as nine values each: the
-- enemy (1 to 3), the anchor in percent of the window, the bar's top-left corner from it in the
-- original's pixels, the fill's width in pixels, its red and green tint (128 as it is, 255 twice as
-- bright) and the HP (battle_main.c: BTL_renderEnemyHPBars).
local max_enemies = 3
local fill_pixels = 50
-- The fill sprite is a grey ramp from its left end to its right (BTLCOMND, palette 3); the tint's
-- blue never changes.
local ramp_left = 0x63
local ramp_right = 0xce
local plain_tint = 128

local function pixels(value)
  return string.format("%.2fdp", value * 3)
end

-- A tinted channel: 128 keeps the grey, 255 doubles it, up to white.
local function tinted(grey, tint)
  return math.min(255, math.floor(grey * tint / plain_tint))
end

local function fill_colour(pixel, red, green)
  local grey = ramp_left + (ramp_right - ramp_left) * pixel / fill_pixels
  return string.format(
    "#%02x%02x%02x",
    tinted(grey, red),
    tinted(grey, green),
    tinted(grey, plain_tint)
  )
end

-- Where along the fill a channel's tint reaches white, when it does inside it.
local function saturation(tint)
  if tint <= plain_tint then
    return nil
  end
  local pixel = (255 * plain_tint / tint - ramp_left) * fill_pixels / (ramp_right - ramp_left)
  if pixel > 0 and pixel < fill_pixels then
    return pixel
  end
  return nil
end

-- The fill as gradients between where the channels saturate: the colour runs straight in each.
local function draw_fill(document, enemy, red, green)
  local cuts = { 0 }
  for _, tint in ipairs({ red, green }) do
    local pixel = saturation(tint)
    if pixel ~= nil then
      cuts[#cuts + 1] = pixel
    end
  end
  table.sort(cuts)
  cuts[#cuts + 1] = fill_pixels
  for segment = 0, 2 do
    local id = "hb-" .. enemy .. "-s" .. segment
    local from, to = cuts[segment + 1], cuts[segment + 2]
    set_class(document, id, "hidden", to == nil)
    if to ~= nil then
      set_property(document, id, "left", pixels(from))
      set_property(document, id, "width", pixels(to - from))
      set_property(
        document,
        id,
        "decorator",
        "horizontal-gradient("
          .. fill_colour(from, red, green)
          .. " "
          .. fill_colour(to, red, green)
          .. ")"
      )
    end
  end
end

-- The HP right-aligned in four places (utils2.c: drawEntityText(0, 4, ...)).
local function draw_hp_value(document, enemy, hp)
  local digits = string.sub(tostring(math.floor(hp)), -number_digits)
  local first = number_digits - #digits
  for place = 0, number_digits - 1 do
    local id = "hd-" .. enemy .. "-" .. place
    local digit = place >= first and string.sub(digits, place - first + 1, place - first + 1) or ""
    set_class(document, id, "hidden", digit == "")
    for glyph = 0, 9 do
      set_class(document, id, "d-" .. glyph, digit == tostring(glyph))
    end
    if digit ~= "" and applied[id .. " text"] ~= digit then
      applied[id .. " text"] = digit
      lookup(document, id):set_text(digit)
    end
  end
end

local function draw_hp_bars(document)
  local values = {}
  for value in string.gmatch(setting("battle.hp_bars", ""), "%S+") do
    values[#values + 1] = tonumber(value)
  end
  local bars = {}
  for base = 0, #values - 9, 9 do
    bars[math.floor(values[base + 1])] = base
  end
  for enemy = 1, max_enemies do
    local base = bars[enemy]
    local id = "hb-" .. enemy
    set_class(document, id, "hidden", base == nil)
    if base ~= nil then
      local x, y = values[base + 4], values[base + 5]
      set_property(document, id, "left", values[base + 2] .. "%")
      set_property(document, id, "top", values[base + 3] .. "%")
      set_property(document, id .. "-frame", "left", pixels(x))
      set_property(document, id .. "-frame", "top", pixels(y))
      set_property(document, id .. "-fill", "left", pixels(x + 2))
      set_property(document, id .. "-fill", "top", pixels(y + 2))
      set_property(document, id .. "-fill", "width", pixels(values[base + 6]))
      draw_fill(document, enemy, values[base + 7], values[base + 8])
      set_property(document, id .. "-value", "left", pixels(x + 33))
      set_property(document, id .. "-value", "top", pixels(y - 9))
      draw_hp_value(document, enemy, values[base + 9])
      -- The line from the outline's bottom-left corner to the node, both ends drawn.
      local dx, dy = -x, -(y + 6)
      set_property(document, id .. "-line", "left", pixels(x))
      set_property(document, id .. "-line", "top", pixels(y + 6))
      set_property(document, id .. "-line", "width", pixels(math.sqrt(dx * dx + dy * dy) + 1))
      set_property(
        document,
        id .. "-line",
        "transform",
        string.format("rotate(%.2fdeg)", math.deg(math.atan(dy, dx)))
      )
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

-- A piece moves by its transform, not by left and top: the banner's hundred and more pieces fly in
-- each frame and a transform leaves the document's layout as it was.
local function draw_piece(document, index, piece)
  local key = table.concat(piece, " ")
  if shown_pieces[index] == key then
    return
  end
  shown_pieces[index] = key
  local block, shadow, trail = "bp-" .. index, "bs-" .. index, "bt-" .. index
  set_class(document, block, "hidden", piece[1] == nil)
  set_class(document, shadow, "hidden", piece[1] == nil)
  if piece[1] == nil then
    return
  end
  local x, y = piece[1] * 3, piece[2] * 3
  local width, height = piece[3] * 3, piece[4] * 3
  for _, id in ipairs({ block, shadow }) do
    set_property(document, id, "width", width .. "dp")
    set_property(document, id, "height", height .. "dp")
  end
  set_property(document, block, "transform", "translate(" .. x .. "dp, " .. y .. "dp)")
  -- The shadow lies 2 pixels down and right (battle_hud.c: BTL_renderBattleStartText).
  set_property(document, shadow, "transform", "translate(" .. (x + 6) .. "dp, " .. (y + 6) .. "dp)")
  for texture = 0, 2 do
    set_class(document, block, "texture-" .. texture, piece[5] == texture)
  end
  local length = piece[6] * 3
  set_class(document, trail, "hidden", length <= 0)
  set_class(document, trail, "left", piece[7] == 1)
  if length > 0 then
    set_property(document, trail, "width", length .. "dp")
    set_property(document, trail, "left", (piece[7] == 1 and -length or width) .. "dp")
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
  local banner = lookup(document, "banner")
  for flash = 0, 2 do
    banner:set_class("flash-" .. flash, number("battle.intro.flash") == flash)
  end
end

-- The strip's slide in the original look (command_menu.c: BTL_renderCommandMenu): the frame's
-- pieces span the strip's top to bottom edge and each icon 3 lines under the top edge to 13 over
-- the bottom one (13 and 3 for the lower ones), stretched with them, so the strip drops in
-- stretched and settles. The game moves the edges (apps/screens: command_menu.hpp); the cursor and
-- the name stay where they are. Only the original look slides it.
local function draw_motion(document)
  if ui.setting("ui_skin") ~= "original" then
    return
  end
  -- Before the banner and once it has gone back up the strip waits above the screen, at its first
  -- step.
  local top = number("battle.menu.top")
  local bottom = number("battle.menu.bottom")
  local settled = top == strip_top and bottom == strip_bottom
  local frame_transform, icon_transform = "none", "none"
  if not settled then
    local shift = string.format("translate(0dp, %ddp)", (top - strip_top) * 3)
    frame_transform =
      string.format("%s scale(1, %.4f)", shift, (bottom - top) / (strip_bottom - strip_top))
    icon_transform = string.format(
      "%s scale(1, %.4f)",
      shift,
      math.max(0, bottom - top - icon_height) / icon_height
    )
  end
  -- A strip that never moved keeps no transform at all.
  if settled and not strip_moved then
    return
  end
  strip_moved = not settled
  for index = 0, max_commands - 1 do
    set_property(document, "command-frame-" .. index, "transform", frame_transform)
    -- The finisher has no icon on the strip: its entry is the gauge's "PUSH" box, which stays.
    local has_icon = setting(entry("battle.menu", index, "icon"), "") ~= ""
    set_property(document, "command-" .. index, "transform", has_icon and icon_transform or "none")
  end
end

local function draw_intro(document)
  intro_step = phase == "intro" and setting("battle.intro.step", "done") or "done"
  lookup(document, "intro"):set_class("hidden", phase ~= "intro")
  -- The HUD is already there under the banner, without the cursor or the hovered command's name.
  lookup(document, "fight"):set_class("opening", phase == "intro")
  if phase ~= "intro" then
    return
  end
  draw_anger(document)
  draw_banner(document)
  -- Veiled, not hidden: the commands keep their boxes, so the focus and the keyboard and gamepad
  -- navigation work on them from the first frame of the fight.
  lookup(document, "fight"):set_class("veiled", intro_step == "anger")
end

local function draw(document)
  phase = setting("battle.phase", "fight")
  -- In the original look the strip stays through the victory and the end box, without its cursor
  -- or name, and then goes back up (battle_main.c:777); the rest of the fight's screen goes.
  local after = phase == "victory" or phase == "end" or phase == "closing"
  local strip_after = after and ui.setting("ui_skin") == "original"
  lookup(document, "fight"):set_class(
    "hidden",
    phase ~= "fight" and phase ~= "intro" and not strip_after
  )
  lookup(document, "fight"):set_class("after", strip_after)
  lookup(document, "end"):set_class("hidden", phase ~= "end")
  draw_intro(document)
  -- The victory poses before the end box: nothing of the battle's own is drawn.
  if phase == "victory" then
    ending = nil
    return
  end
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
  draw_commands(document)
  -- Under the open bag the row keeps its cursor and the hovered command's name: the bag's
  -- document holds the focus meanwhile.
  if not bag_open then
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

function charge()
  ui.send("battle_charge")
end

function close_end()
  ui.send("battle_close")
end

function on_show(document)
  draw(document)
  draw_motion(document)
  draw_damage(document)
  draw_hp_bars(document)
  draw_prompts(document)
end

function on_setting(document, name)
  if name == "battle.revision" or name == "battle.phase" or name == "bag.revision" then
    draw(document)
  elseif name == "battle.menu.motion" then
    draw_motion(document)
    if phase == "fight" and not bag_open then
      draw_cursor(document)
    end
  elseif name == "battle.intro.revision" then
    draw_intro(document)
  elseif name == "battle.damage.revision" then
    draw_damage(document)
  elseif name == "battle.hp_bars.revision" then
    draw_hp_bars(document)
  end
end

function on_tick(document, seconds)
  if phase == "end" and ending ~= nil then
    tick_end(document, seconds)
  elseif phase == "fight" then
    if not bag_open then
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
-- original's own button), whose document then takes the buttons until it closes; confirm charges
-- the finisher while its aura lasts.
function on_action(_document, action)
  -- Nothing answers while the battle opens, nor during the victory poses before the end box
  -- (BTL_handleVictorySequence reads no buttons until handleBattleEndBox), nor while the command
  -- strip goes back up after it.
  if phase == "intro" or phase == "victory" or phase == "closing" then
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
  -- The bag's own document takes the buttons while it is open.
  if bag_open then
    return true
  end
  if action == "cancel" then
    open_bag()
    return true
  end
  return action == "menu" or action == "up" or action == "down"
end
