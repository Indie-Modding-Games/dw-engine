-- The battle screen. It reads the battle through the UI API's battle namespace (ui/api/battle.lua,
-- docs/modding/ui-api.md) and redraws on its events; the player's choices go back as its actions.
-- No rule of the battle lives here: which commands exist, which are available and what they do is
-- the game's.

local max_fighters = 4
local max_commands = 11
local finisher_segments = 6
local finisher_marks = 8
local max_anger = 3
-- The marks over the NPCs not fighting, which run off (battle.intro.flee).
local max_flee = 8
-- Four fighters with up to 8 numbers each, of up to 4 digits (entity_text.c: ENTITY_TEXT_DATA).
local max_numbers = 32
local number_digits = 4
-- The banner's slots (screens::BannerPieceSlots): a word in another language may take more pieces
-- than the original's 155.
local banner_pieces = 200
-- The command strip's edges once it is in place, in the original's screen lines (0 at the middle;
-- command_menu.c: BTL_COMMAND_MENU_SLIDE_X and _Y), and the icons' height there.
local strip_top = -100
local strip_bottom = -68
-- std_main.c: STD_addCommandMenu: in a cup match the strip stands at the screen's foot.
local match_strip_top = 68
local match_strip_bottom = 100
local icon_height = 16
-- Whether the strip's pieces carry a transform from its slide.
local strip_moved = false
-- The partner's bars' left edge once in place, in the original's screen pixels from the middle
-- (battle_effect.c: BTL_STATUS_BAR_X[7]), and whether they carry a transform from their slide.
local bars_x = 46
local bars_moved = false
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
-- The fighters and the strip as the last draw read them (battle.fighters(), battle.commands()): the
-- game bumps the battle's revision on any change of them, and each frame's cursor reads them here.
local fighters = {}
local commands = {}
local intro_step = "done"
local icon_classes = {}
local frame_classes = {}
-- What each banner piece shows now, by piece index (draw_banner): its numbers as published (false
-- once hidden), its block's size and texture and its trail.
local shown_pieces = {}
local piece_size = {}
local piece_texture = {}
local piece_trail = {}

-- The strip's commands, no more than the document has (command-0..10).
local function command_count()
  return math.min(#commands, max_commands)
end

-- A command of the strip by its place from 0, or nil past its end.
local function command(index)
  return commands[index + 1]
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
      shown_pieces, piece_size, piece_texture, piece_trail = {}, {}, {}, {}
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
  local fighter = fighters[index + 1]
  panel:set_class("hidden", fighter == nil)
  if fighter == nil then
    return
  end
  local prefix = "fighter-" .. index
  local hp, max_hp = fighter.hp, fighter.max_hp
  local mp, max_mp = fighter.mp, fighter.max_mp
  document:get(prefix .. "-name"):set_text(text(fighter.name_key))
  document:get(prefix .. "-status"):set_text(text(fighter.status_key))
  lookup(document, prefix .. "-hp-label"):set_text(t(stat_label_keys[0]))
  lookup(document, prefix .. "-hp"):set_text(tostring(hp))
  lookup(document, prefix .. "-hp-fill"):set_property("width", percent(hp, max_hp) .. "%")
  if index == 0 then
    -- The partner's HP row gets the class low under a fifth of its HP, for a skin that warns.
    set_class(document, prefix .. "-hp-row", "low", max_hp > 0 and hp * 5 < max_hp)
    -- The partner's HUD shows HP and MP all the time (battle_hud.c: BTL_renderPartnerStatusBars),
    -- its values with BTL_renderNumber(0, 4, x0 + 0x49, y0 - 3, value, 0xa).
    hud_digits.draw(document, prefix .. "-hp-d-", hp, set_class)
    lookup(document, prefix .. "-mp-label"):set_text(t(stat_label_keys[1]))
    lookup(document, prefix .. "-mp"):set_text(tostring(mp))
    hud_digits.draw(document, prefix .. "-mp-d-", mp, set_class)
    lookup(document, prefix .. "-mp-fill"):set_property("width", percent(mp, max_mp) .. "%")
  else
    -- An enemy's HP bar only shows for a while after it takes damage, and it has no MP bar
    -- (battle_main.c: BTL_renderEnemyHPBars).
    document:get(prefix .. "-hp-row"):set_class("hidden", not fighter.hp_bar)
  end
  panel:set_class("fainted", fighter.fainted)
end

local function command_available(index)
  local found = command(index)
  return found ~= nil and found.available
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
  local count = command_count()
  for index = 0, max_commands - 1 do
    local button = lookup(document, "command-" .. index)
    local id = "command-" .. index
    local frame = lookup(document, "command-frame-" .. index)
    button:set_class("hidden", index >= count)
    frame:set_class("hidden", index >= count)
    if index < count then
      local entry = command(index)
      button:set_text(text(entry.label_key))
      button:set_class("unavailable", not entry.available)
      button:set_class("current", entry.current)
      local icon = entry.icon
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
-- when the hovered icon shows its second frame (packages/screens: command_menu.hpp).
local function draw_cursor(document)
  local cursor = lookup(document, "command-cursor")
  local count = command_count()
  local blink_on = battle.strip().blink
  local hovered = nil
  for index = 0, count - 1 do
    local button = lookup(document, "command-" .. index)
    local focused = button:has_focus() and command(index).icon ~= ""
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

-- The name of the focused command under the row, as the original names the hovered icon, and a
-- technique's MP cost after it (a skin that shows it styles #command-cost). The name gets the class
-- short while the command cannot be given now: the finisher before its gauge is full, a technique
-- the partner has not the MP for.
local function draw_command_name(document)
  local name, cost, short = "", 0, false
  for index = 0, command_count() - 1 do
    if lookup(document, "command-" .. index):has_focus() then
      local entry = command(index)
      name = text(entry.label_key)
      cost = entry.mp_cost
      local partner = fighters[1]
      short = not entry.available or (cost > 0 and (partner ~= nil and partner.mp or 0) < cost)
    end
  end
  lookup(document, "command-label"):set_text(name)
  local cost_text = cost > 0 and t("battle.mp_cost", { mp = cost }) or ""
  if applied["command-cost text"] ~= cost_text then
    applied["command-cost text"] = cost_text
    lookup(document, "command-cost"):set_text(cost_text)
  end
  set_class(document, "command-cost", "hidden", cost_text == "")
  set_class(document, "command-name", "short", short)
end

local function focus_command(document)
  local count = command_count()
  if lookup(document, "open-bag"):has_focus() then
    return
  end
  for index = 0, count - 1 do
    if lookup(document, "command-" .. index):has_focus() then
      return
    end
  end
  for index = 0, count - 1 do
    if command(index).current then
      lookup(document, "command-" .. index):focus()
      return
    end
  end
  if count > 0 then
    lookup(document, "command-0"):focus()
  end
end

local function draw_finisher(document)
  local finisher = battle.finisher()
  local progress, goal = finisher.progress, finisher.goal
  local filled = goal > 0 and math.floor(progress * finisher_segments / goal) or 0
  local ready = finisher.ready
  -- The gauge's word has two marks more than its steps; they show once it is full.
  if ready then
    filled = finisher_marks
  end
  for index = 0, finisher_marks - 1 do
    lookup(document, "finisher-" .. index):set_class("filled", index < filled)
  end
  -- The same word in the player's language, as many of its letters as the gauge has marks lit
  -- (the original look shows it where the original's English picture is not used).
  local word = t("battle.finisher_word")
  local letters = utf8.len(word) or #word
  local shown = math.floor(letters * filled / finisher_marks)
  local stop = utf8.offset(word, shown + 1) or (#word + 1)
  lookup(document, "finisher-word"):set_text(string.sub(word, 1, stop - 1))
  lookup(document, "finisher"):set_class("ready", ready)
  lookup(document, "charge"):set_class("hidden", not finisher.charging)
  document:get("charge-fill"):set_property("width", math.min(100, finisher.charge) .. "%")
end

local function draw_revive(document, state)
  local seconds = state.revive_seconds
  lookup(document, "revive"):set_class("hidden", seconds == nil)
  if seconds ~= nil then
    lookup(document, "revive"):set_text(t("battle.revive_countdown", { seconds = seconds }))
  end
end

-- A tournament match's clock (std_main.c: STD_renderCounterDigits): two digits, the tens hidden
-- under 10 (convertValueToDigits drops the leading zero), each a sprite class d-<digit> and the
-- digit as text for the skins that draw text.
local function draw_match_clock(document, state)
  local seconds = state.match_seconds
  lookup(document, "match-clock"):set_class("hidden", seconds == nil)
  if seconds == nil then
    return
  end
  local shown = math.min(99, math.floor(seconds))
  local digits = { math.floor(shown / 10), shown % 10 }
  for place = 0, 1 do
    local id = "mc-d-" .. place
    local digit = digits[place + 1]
    local visible = place == 1 or digit ~= 0
    set_class(document, id, "hidden", not visible)
    for glyph = 0, 9 do
      set_class(document, id, "d-" .. glyph, visible and digit == glyph)
    end
    if visible and applied[id .. " text"] ~= tostring(digit) then
      applied[id .. " text"] = tostring(digit)
      lookup(document, id):set_text(tostring(digit))
    end
  end
end

-- The player's bag opens with its own button while the battle goes on (battle_tick.c:
-- tamerTickBattle): the game shows the field's bag (ui/partner_menu, its items page) over the
-- fight, and the command row waits under it (handleCommands returns while it is open).
local function draw_bag(document, state)
  lookup(document, "open-bag"):set_class("unavailable", not state.bag.available)
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
local function line_text(line)
  local arguments = {
    name = text(line.name_key),
    subject = text(line.subject_key),
    amount = tostring(line.amount),
  }
  for mark, value in pairs(end_marks) do
    arguments[mark] = value
  end
  local joined = {}
  for _, part in ipairs(line.parts) do
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
local function end_rows(result)
  local rows = {}
  for index = 1, math.min(#result.lines, max_end_lines) do
    rows_of_line(line_text(result.lines[index]), rows)
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

local function start_end(state)
  local result = battle.result()
  local stats, gains, maxima = {}, {}, {}
  for index = 0, stat_count - 1 do
    local stat = result.stats[index + 1]
    stats[index] = stat.value
    gains[index] = stat.gain
    maxima[index] = stat.max
  end
  local bits = result.bits
  ending = {
    result = result,
    frame_seconds = state.frame_seconds,
    seconds = 0,
    delay = result.count_delay_frames,
    wait_frames = result.wait_frames,
    stats = stats,
    gains = gains,
    maxima = maxima,
    bits = bits,
    balance = result.money - bits,
    balance_open = false,
    rows = end_rows(result),
    -- The row being typed, the first row on show, the rows begun since it (BATTLE_END_ROWS_SHOWN),
    -- the characters typed of the row, whether a press set the typing on.
    row = 1,
    top = 1,
    shown = 0,
    typed = 0,
    typing = false,
    wait = result.wait_frames,
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
      battle.end_count()
    elseif ending.bits > 0 then
      ending.balance_open = true
      ending.bits = ending.bits - 1
      ending.balance = ending.balance + 1
      battle.end_count()
      if ending.bits == 0 then
        battle.end_counted()
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
    battle.end_counted()
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
  lookup(document, "end-title"):set_text(text(ending.result.outcome_key))
  for index = 0, stat_count - 1 do
    local gain = ending.gains[index]
    lookup(document, "gain-" .. index .. "-label"):set_text(t(stat_label_keys[index]))
    lookup(document, "stat-" .. index):set_text(tostring(ending.stats[index]))
    document
      :get("stat-" .. index .. "-fill")
      :set_property("width", percent(ending.stats[index], ending.maxima[index]) .. "%")
    document
      :get("stat-" .. index .. "-bar")
      :set_class("raised", ending.result.stats[index + 1].gain > 0)
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
  lookup(document, "prompt-confirm"):set_prompt("confirm")
  lookup(document, "prompt-charge"):set_prompt("confirm")
  lookup(document, "prompt-menu"):set_prompt("menu")
  -- The original's square button calls the finisher (battle_tick.c: handleCommands); here it is
  -- the `extra` action, so the gauge's box shows its key or button: the square itself on the
  -- original's pad. A key's name of more than two letters does not fit where the square was: a
  -- skin may move the box for it.
  lookup(document, "prompt-finisher"):set_prompt("extra")
  local long = ui.device() ~= "gamepad" and (utf8.len(ui.prompt("extra")) or 0) > 2
  lookup(document, "fight"):set_class("long-finisher-key", long)
end

-- The battle's opening (packages/screens: battle_intro.hpp): the anger marks over the enemies, then
-- the "BATTLE" banner flying together, flashing and bursting over the battle's HUD.
local function draw_marks(document, list, prefix, count, scale)
  for index = 0, count - 1 do
    local mark = lookup(document, prefix .. index)
    local found = list[index + 1]
    local shown = intro_step == "anger" and found ~= nil and found.shown
    mark:set_class("hidden", not shown)
    if shown then
      mark:set_property("left", found.x .. "%")
      mark:set_property("top", found.y .. "%")
      mark:set_property("transform", string.format("scale(%.2f)", scale))
    end
  end
end

local function draw_anger(document, intro)
  local scale = intro.anger_scale / 100
  draw_marks(document, intro.anger, "anger-", max_anger, scale)
  draw_marks(document, intro.flee, "flee-", max_flee, scale)
end

-- The numbers over the fighters (packages/screens: damage_numbers.hpp; battle.numbers()): the
-- anchor in percent of the window, the first digit's offset from it in the original's pixels, the
-- value, its kind and its icon. Each digit is 8 pixels square, 7 apart (utils2.c: drawEntityText);
-- the icon, when there is one, is 8 pixels square right before the first digit (entity_text.c:
-- renderEntityText, utils2.c: drawEntityTextIcon).
local number_icons = { "hp", "mp", "offense", "defense", "speed" }
local function draw_damage(document)
  local list = battle.numbers()
  local count = math.min(max_numbers, #list)
  for index = 0, max_numbers - 1 do
    local id = "dn-" .. index
    set_class(document, id, "hidden", index >= count)
    if index < count then
      local shown = list[index + 1]
      set_property(document, id, "left", shown.x .. "%")
      set_property(document, id, "top", shown.y .. "%")
      set_property(
        document,
        id,
        "transform",
        "translate(" .. (shown.dx * 3) .. "dp, " .. (shown.dy * 3) .. "dp)"
      )
      set_class(document, id, "poison", shown.kind == "poison")
      set_class(document, id, "gain", shown.kind == "gain")
      local icon = shown.icon
      local icon_id = "di-" .. index
      set_class(document, icon_id, "hidden", icon == nil)
      for _, name in ipairs(number_icons) do
        set_class(document, icon_id, "i-" .. name, icon == name)
      end
      if icon ~= nil and applied[icon_id .. " text"] ~= icon then
        applied[icon_id .. " text"] = icon
        lookup(document, icon_id):set_text(text("battle.number_icon." .. icon))
      end
      local digits = tostring(shown.value)
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

-- The enemies' HP bars beside them (packages/screens: enemy_hp_bars.hpp; battle.hp_bars()): the
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

-- The fill as gradients between where the channels saturate: the colour runs straight in each. The
-- sprite is as wide as the HP left and shows the ramp's first pixels (GsSPRITE w), so the gradients
-- stop at that width themselves: the fill's box does not clip what it holds.
local function draw_fill(document, enemy, width, red, green)
  local cuts = { 0 }
  for _, tint in ipairs({ red, green }) do
    local pixel = saturation(tint)
    if pixel ~= nil and pixel < width then
      cuts[#cuts + 1] = pixel
    end
  end
  table.sort(cuts)
  cuts[#cuts + 1] = width
  for segment = 0, 2 do
    local id = "hb-" .. enemy .. "-s" .. segment
    local from, to = cuts[segment + 1], cuts[segment + 2]
    if to ~= nil and to <= from then
      to = nil
    end
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
  local shown = battle.hp_bars()
  -- Enemy life bars (a dwe improvement): the modern look draws them too while the option is on.
  set_class(document, "hp-bars", "always", shown.always)
  local bars = {}
  for _, bar in ipairs(shown.bars) do
    bars[bar.enemy] = bar
  end
  for enemy = 1, max_enemies do
    local bar = bars[enemy]
    local id = "hb-" .. enemy
    set_class(document, id, "hidden", bar == nil)
    if bar ~= nil then
      local x, y = bar.left, bar.top
      set_property(document, id, "left", bar.x .. "%")
      set_property(document, id, "top", bar.y .. "%")
      set_property(document, id .. "-frame", "left", pixels(x))
      set_property(document, id .. "-frame", "top", pixels(y))
      set_property(document, id .. "-fill", "left", pixels(x + 2))
      set_property(document, id .. "-fill", "top", pixels(y + 2))
      set_property(document, id .. "-fill", "width", pixels(bar.fill))
      draw_fill(document, enemy, math.max(0, math.min(fill_pixels, bar.fill)), bar.red, bar.green)
      set_property(document, id .. "-value", "left", pixels(x + 33))
      set_property(document, id .. "-value", "top", pixels(y - 9))
      draw_hp_value(document, enemy, bar.hp)
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

-- The countdown over the fallen partner (packages/screens: death_countdown.hpp; command_menu.c:
-- BTL_tickDeathCountdown, BTL_renderDeathCountdown; battle.death_countdown()): the anchor in
-- percent of the window, the digit, the digit's and the badge's scale, their turn in degrees and
-- the badge's palette. The digit is drawn over the badge, both centred on the anchor.
local function draw_death_countdown(document)
  local countdown = battle.death_countdown()
  set_class(document, "death-countdown", "hidden", countdown == nil)
  if countdown == nil then
    return
  end
  set_property(document, "death-countdown", "left", countdown.x .. "%")
  set_property(document, "death-countdown", "top", countdown.y .. "%")
  for digit = 1, 5 do
    set_class(document, "dc-digit", "digit-" .. digit, countdown.digit == digit)
  end
  for palette = 0, 2 do
    set_class(document, "dc-badge", "palette-" .. palette, countdown.palette == palette)
  end
  local turn = string.format("rotate(%.1fdeg) ", countdown.turn)
  set_property(
    document,
    "dc-digit",
    "transform",
    turn .. string.format("scale(%.3f)", countdown.digit_scale)
  )
  set_property(
    document,
    "dc-badge",
    "transform",
    turn .. string.format("scale(%.3f)", countdown.badge_scale)
  )
end

-- The player's shout (command_shout.c: BTL_drawCommandShout, BTL_renderCommandShout): the game
-- gives the anchor and its line; the shout is as wide as 12 pixels a character (the original's,
-- strlen * 12) and keeps 140 pixels of the middle on each side, then hops and grows from its
-- centre.
local shout_glyph = 12
local shout_edge = 140
local function draw_shout(document)
  local shout = battle.shout()
  set_class(document, "shout", "hidden", shout == nil)
  if shout == nil then
    return
  end
  local line = text(shout.text_key)
  if applied["shout text"] ~= line then
    applied["shout text"] = line
    lookup(document, "shout-text"):set_text(line)
    lookup(document, "shout-shadow"):set_text(line)
  end
  local half = math.floor(((utf8.len(line) or #line) * shout_glyph) / 2)
  local node_x, node_y = shout.node_x, shout.node_y
  local x = node_x
  if x - half < -shout_edge then
    x = half - shout_edge
  end
  if x + half > shout_edge then
    x = shout_edge - half
  end
  local y = shout.line + shout.hop
  set_property(document, "shout", "left", shout.x .. "%")
  set_property(document, "shout", "top", shout.y .. "%")
  set_property(
    document,
    "shout",
    "transform",
    string.format(
      "translate(%ddp, %ddp) scale(%.2f)",
      (x - node_x) * 3,
      (y - node_y) * 3,
      shout.scale
    )
  )
end

-- The banner's pieces (battle.intro().pieces) are in the original's pixels, 3dp each: x, y, width,
-- height, texture, trail and 1 for a trail on the left.

local function hide_piece(document, index)
  if shown_pieces[index] == false then
    return
  end
  shown_pieces[index] = false
  set_class(document, "bp-" .. index, "hidden", true)
  set_class(document, "bs-" .. index, "hidden", true)
end

-- A piece moves by its transform, not by left and top: the banner's hundred and more pieces fly in
-- each frame and a transform leaves the document's layout as it was.
local function draw_piece(document, index, x, y, width, height, texture, trail, left)
  local block, shadow = lookup(document, "bp-" .. index), lookup(document, "bs-" .. index)
  if not shown_pieces[index] then
    set_class(document, "bp-" .. index, "hidden", false)
    set_class(document, "bs-" .. index, "hidden", false)
  end
  -- Only the transforms change while a piece flies: its size, texture and trail are set again only
  -- when they change (a property set on an element is parsed again even when it is the same).
  local size = width .. " " .. height
  if piece_size[index] ~= size then
    piece_size[index] = size
    for _, element in ipairs({ block, shadow }) do
      element:set_property("width", (width * 3) .. "dp")
      element:set_property("height", (height * 3) .. "dp")
    end
  end
  x, y = x * 3, y * 3
  block:set_property("transform", "translate(" .. x .. "dp, " .. y .. "dp)")
  -- The shadow lies 2 pixels down and right (battle_hud.c: BTL_renderBattleStartText).
  shadow:set_property("transform", "translate(" .. (x + 6) .. "dp, " .. (y + 6) .. "dp)")
  if piece_texture[index] ~= texture then
    piece_texture[index] = texture
    for shown = 0, 2 do
      block:set_class("texture-" .. shown, texture == shown)
    end
  end
  local trail_key = trail .. " " .. left .. " " .. width
  if piece_trail[index] ~= trail_key then
    piece_trail[index] = trail_key
    local trail_element = lookup(document, "bt-" .. index)
    local length = trail * 3
    trail_element:set_class("hidden", length <= 0)
    trail_element:set_class("left", left == 1)
    if length > 0 then
      trail_element:set_property("width", length .. "dp")
      trail_element:set_property("left", (left == 1 and -length or width * 3) .. "dp")
    end
  end
end

-- Only the pieces whose numbers changed since the last draw are touched: once gathered, the banner
-- stands still until it bursts.
local function draw_banner(document, intro)
  local index = 0
  for _, piece in ipairs(intro.pieces) do
    if index >= banner_pieces then
      break
    end
    if shown_pieces[index] ~= piece.key then
      draw_piece(
        document,
        index,
        piece.x,
        piece.y,
        piece.width,
        piece.height,
        piece.texture,
        piece.trail,
        piece.left
      )
      shown_pieces[index] = piece.key
    end
    index = index + 1
  end
  for rest = index, banner_pieces - 1 do
    hide_piece(document, rest)
  end
  local banner = lookup(document, "banner")
  for flash = 0, 2 do
    banner:set_class("flash-" .. flash, intro.flash == flash)
  end
end

-- The strip's slide in the original look (command_menu.c: BTL_renderCommandMenu): the frame's
-- pieces span the strip's top to bottom edge and each icon 3 lines under the top edge to 13 over
-- the bottom one (13 and 3 for the lower ones), stretched with them, so the strip drops in
-- stretched and settles. The game moves the edges (packages/screens: command_menu.hpp); the cursor
-- and the name stay where they are. Only the original look slides it.
local function draw_motion(document)
  if ui.layout("menu") ~= "original" then
    -- A skin switched live away from the original look drops a slide caught halfway.
    if strip_moved then
      strip_moved = false
      for index = 0, max_commands - 1 do
        set_property(document, "command-frame-" .. index, "transform", "none")
        set_property(document, "command-" .. index, "transform", "none")
      end
    end
    return
  end
  -- Before the banner and once it has gone back up the strip waits above the screen, at its first
  -- step.
  local strip = battle.strip()
  local top = strip.top
  local bottom = strip.bottom
  -- A cup match's strip (STD_renderCommandMenu) stands still at the foot, its name above it.
  local match = top == match_strip_top and bottom == match_strip_bottom
  set_class(document, "commands", "match", match)
  local settled = match or (top == strip_top and bottom == strip_bottom)
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
    local has_icon = command(index) ~= nil and command(index).icon ~= ""
    set_property(document, "command-" .. index, "transform", has_icon and icon_transform or "none")
  end
end

-- The partner's bars' slide in the original look (battle_hud.c: BTL_renderPartnerStatusBars draws
-- each bar from its own BTL_STATUS_BAR_X): they come in from the right edge with the strip and
-- leave as it goes back up. The game moves them (packages/screens: partner_bars.hpp).
local function draw_bars(document)
  if ui.layout("menu") ~= "original" then
    if bars_moved then
      bars_moved = false
      set_property(document, "fighter-0-hp-row", "transform", "none")
      set_property(document, "fighter-0-mp-row", "transform", "none")
    end
    return
  end
  local strip = battle.strip()
  local hp = math.floor(strip.hp_x or bars_x)
  local mp = math.floor(strip.mp_x or bars_x)
  local settled = hp == bars_x and mp == bars_x
  if settled and not bars_moved then
    return
  end
  bars_moved = not settled
  local function shift(x)
    if x == bars_x then
      return "none"
    end
    return string.format("translate(%ddp, 0dp)", (x - bars_x) * 3)
  end
  set_property(document, "fighter-0-hp-row", "transform", shift(hp))
  set_property(document, "fighter-0-mp-row", "transform", shift(mp))
end

local function draw_intro(document)
  local intro = phase == "intro" and battle.intro() or nil
  intro_step = intro ~= nil and intro.step or "done"
  lookup(document, "intro"):set_class("hidden", phase ~= "intro")
  -- The HUD is already there under the banner, without the cursor or the hovered command's name.
  lookup(document, "fight"):set_class("opening", phase == "intro")
  if intro == nil then
    return
  end
  draw_anger(document, intro)
  draw_banner(document, intro)
  -- Veiled, not hidden: the commands keep their boxes, so the focus and the keyboard and gamepad
  -- navigation work on them from the first frame of the fight.
  lookup(document, "fight"):set_class("veiled", intro_step == "anger")
end

local function draw(document)
  local state = battle.state()
  phase = state.phase
  -- In the original look the strip and the partner's bars stay through the victory and the end box,
  -- the strip without its cursor or name, and then go (battle_main.c:777); the rest of the fight's
  -- screen goes.
  local after = phase == "victory" or phase == "end" or phase == "closing"
  local strip_after = after and ui.layout("menu") == "original"
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
      start_end(state)
    end
    draw_end(document)
    return
  end
  ending = nil
  fighters = battle.fighters()
  commands = battle.commands()
  for index = 0, max_fighters - 1 do
    draw_fighter(document, index)
  end
  bag_open = state.bag.open
  draw_bag(document, state)
  draw_commands(document)
  -- Under the open bag the row keeps its cursor and the hovered command's name: the bag's
  -- document holds the focus meanwhile.
  if not bag_open then
    focus_command(document)
    draw_command_name(document)
    draw_cursor(document)
  end
  draw_finisher(document)
  draw_revive(document, state)
  draw_match_clock(document, state)
end

function choose(index)
  if command_available(index) then
    battle.command(command(index).command)
  end
end

function open_bag()
  battle.open_bag()
end

function charge()
  battle.charge()
end

function close_end()
  battle.close()
end

-- The marks over the fighters of a cup match between two of the same species (packages/screens:
-- enemy_hp_bars.hpp, std_main.c: STD_renderPlayerMarker; battle.markers()): the fighter (0 the
-- partner, 1 the rival) and the anchor in percent of the window; the mark's corner is 32 pixels
-- over it. The modern look writes «1P» and «COM» in it; the original look draws the bubbles.
local function draw_player_marks(document)
  local marks = {}
  for _, mark in ipairs(battle.markers()) do
    marks[mark.fighter] = mark
  end
  for fighter = 0, 1 do
    local mark = marks[fighter]
    local id = "pm-" .. fighter
    set_class(document, id, "hidden", mark == nil)
    if mark ~= nil then
      set_property(document, id, "left", mark.x .. "%")
      set_property(document, id, "top", mark.y .. "%")
      local label = text(fighter == 0 and "battle.mark.partner" or "battle.mark.rival")
      if applied[id .. " text"] ~= label then
        applied[id .. " text"] = label
        lookup(document, id):set_text(label)
      end
    end
  end
end

-- Quick battles (apps/game battle_speed.hpp, a dwe improvement): a small x2 plate while the fight
-- runs at twice the speed.
local function draw_speed(document)
  set_class(document, "speed-plate", "hidden", not battle.state().fast)
end

function on_show(document)
  draw_speed(document)
  draw(document)
  draw_motion(document)
  draw_bars(document)
  draw_damage(document)
  draw_hp_bars(document)
  draw_player_marks(document)
  draw_death_countdown(document)
  draw_shout(document)
  draw_prompts(document)
end

-- The battle's events (hooks.on): each redraws only what it changed, while the screen shows
-- (on_show draws everything again).
local function on(event, handler)
  hooks.on(event, function(_, document)
    if ui.is_visible(document_name) then
      handler(document)
    end
  end)
end

on("battle.changed", draw)
on("battle.phase", draw)
on("bag.changed", draw)
on("battle.strip_moved", function(document)
  draw_motion(document)
  draw_bars(document)
  if phase == "fight" and not bag_open then
    draw_cursor(document)
  end
end)
on("battle.intro", draw_intro)
on("battle.numbers", draw_damage)
on("battle.overlays", function(document)
  draw_hp_bars(document)
  draw_player_marks(document)
  draw_death_countdown(document)
end)
on("battle.shout", draw_shout)
on("battle.speed", draw_speed)

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

-- The battle cannot be left with cancel or the pause button. `menu` opens the tamer's bag (the same
-- button that opens the field's menu, whose items page this is), whose document then takes the
-- buttons until it closes; confirm charges the finisher while its aura lasts; `cancel` does nothing
-- on the fight itself (the UI's default for a modal's `cancel` would close the battle).
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
  if action == "confirm" and battle.finisher().charging then
    charge()
    return true
  end
  -- The bag's own document takes the buttons while it is open.
  if bag_open then
    return true
  end
  if action == "menu" then
    open_bag()
    return true
  end
  return action == "cancel" or action == "up" or action == "down"
end
