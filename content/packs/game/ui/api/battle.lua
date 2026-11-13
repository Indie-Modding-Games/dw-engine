-- The UI API's battle (docs/modding/ui-api.md, namespace battle): what a battle shows, as the game
-- publishes it (packages/screens: battle_view.hpp, battle_intro.hpp, command_menu.hpp,
-- damage_numbers.hpp, enemy_hp_bars.hpp, death_countdown.hpp, command_shout.hpp; apps/game:
-- tournament_match_screen.cpp), and the commands that ask the game to act in it. Every rule is the
-- game's: which commands exist and are available, what they cost, when the finisher is ready, how
-- the opening and the numbers move. A command only asks; the game answers with an event.
--
-- Every function returns a fresh table; changing it changes nothing in the game. Positions are the
-- original's pixels (3dp each) unless they say percent of the window.

battle = {}

local function value(name, fallback)
  local found = ui.setting(name)
  if found == nil then
    return fallback
  end
  return found
end

local function whole(name)
  return math.floor(value(name, 0))
end

local function entry(list, index, field)
  return list .. "." .. index .. "." .. field
end

-- The numbers of a setting that packs them as text, separated by spaces.
local function numbers(name)
  local list = {}
  for found in string.gmatch(value(name, ""), "%S+") do
    list[#list + 1] = tonumber(found)
  end
  return list
end

-- A number of seconds the game publishes as -1 while there is none: nil then.
local function seconds(name)
  local found = whole(name)
  if found < 0 then
    return nil
  end
  return found
end

local function phase()
  return value("battle.phase", "fight")
end

-- The battle's state: phase ("intro" the opening, "fight", "victory" the poses after a win, "end"
-- the end box, "closing" while the command strip goes back up), active (a battle is on screen,
-- from its opening to the end box's close), fast (the fight runs at twice the speed), frame_seconds
-- (a battle frame's length, for the end box's pacing), bag (available: the player's bag can open;
-- open; item_in_flight: a thrown item is on its way), revive_seconds (the fallen partner's
-- countdown, or nil), match_seconds (a cup match's clock, or nil) and cup (a cup match).
function battle.state()
  local match = seconds("battle.match_seconds")
  return {
    phase = phase(),
    active = ui.setting("battle.active") == true,
    fast = ui.setting("battle.fast") == true,
    frame_seconds = value("battle.frame_seconds", 0.05),
    bag = {
      available = value("battle.bag.available", false),
      open = value("battle.bag.open", false),
      item_in_flight = ui.setting("battle.item_in_flight") == true,
    },
    revive_seconds = seconds("battle.revive_seconds"),
    match_seconds = match,
    cup = match ~= nil,
  }
end

-- The fighters, the partner first, then the enemies: name_key, status_key (its status effect's
-- text key, or ""), hp, max_hp, mp, max_mp, fainted and hp_bar (whether its HP bar shows now: the
-- partner's always, an enemy's a while after it takes damage).
function battle.fighters()
  local list = {}
  for index = 0, whole("battle.fighter.count") - 1 do
    list[index + 1] = {
      name_key = value(entry("battle.fighter", index, "name"), ""),
      status_key = value(entry("battle.fighter", index, "status"), ""),
      hp = whole(entry("battle.fighter", index, "hp")),
      max_hp = whole(entry("battle.fighter", index, "max_hp")),
      mp = whole(entry("battle.fighter", index, "mp")),
      max_mp = whole(entry("battle.fighter", index, "max_mp")),
      fainted = value(entry("battle.fighter", index, "fainted"), false),
      hp_bar = value(entry("battle.fighter", index, "hp_bar"), false),
    }
  end
  return list
end

-- The command strip in the game's order, the finisher first: command (the number battle.command
-- takes), label_key, icon (its icon in the skins' sheets, "" for the finisher), available (it can
-- be given now), current (the partner follows it now) and mp_cost (a technique's MP now, else 0).
function battle.commands()
  local list = {}
  for index = 0, whole("battle.menu.count") - 1 do
    list[index + 1] = {
      command = whole(entry("battle.menu", index, "command")),
      label_key = value(entry("battle.menu", index, "label"), ""),
      icon = value(entry("battle.menu", index, "icon"), ""),
      available = value(entry("battle.menu", index, "available"), false),
      current = value(entry("battle.menu", index, "current"), false),
      mp_cost = whole(entry("battle.menu", index, "mp_cost")),
    }
  end
  return list
end

-- The finisher's gauge: progress of goal, ready (full), charging (its aura: each battle.charge()
-- counts) and charge (the aura's charge, in percent).
function battle.finisher()
  return {
    progress = whole("battle.finisher.progress"),
    goal = whole("battle.finisher.goal"),
    ready = value("battle.finisher.ready", false),
    charging = value("battle.finisher.charging", false),
    charge = whole("battle.finisher.charge"),
  }
end

-- Where the strip and the partner's bars are while they slide (the original look's): top and
-- bottom (the strip's edges, in lines from the screen's middle), blink (the hovered icon shows its
-- second frame) and hp_x, mp_x (each bar's left edge from the middle, or nil before the game moves
-- them).
function battle.strip()
  local hp_x, mp_x = ui.setting("battle.bars.hp_x"), ui.setting("battle.bars.mp_x")
  return {
    top = whole("battle.menu.top"),
    bottom = whole("battle.menu.bottom"),
    blink = value("battle.menu.blink", false),
    hp_x = hp_x,
    mp_x = mp_x,
  }
end

-- The opening's marks: one per slot, each {shown, x, y} in percent of the window.
local function marks(list, count)
  local found = {}
  for index = 0, count - 1 do
    found[index + 1] = {
      shown = value(entry(list, index, "shown"), false),
      x = value(entry(list, index, "x"), 0),
      y = value(entry(list, index, "y"), 0),
    }
  end
  return found
end

-- The banner's pieces come as seven numbers each (x, y, width, height, texture, trail, left), each
-- piece's group ending with a space.
local piece_pattern = "((%S+) (%S+) (%S+) (%S+) (%S+) (%S+) (%S+) )"

-- The opening: step ("anger", "banner", "burst" or "done"), anger (3 marks over the enemies),
-- flee (8 marks over the others, who run off), anger_scale (the marks' size in percent), flash
-- (the banner's flash, 0 to 2) and pieces (the "BATTLE" banner's blocks: x, y, width, height,
-- texture 0 to 2, trail and left, 1 for a trail on the left; key changes whenever one of them
-- does).
function battle.intro()
  local pieces = {}
  for key, x, y, width, height, texture, trail, left in
    string.gmatch(value("battle.intro.pieces", ""), piece_pattern)
  do
    pieces[#pieces + 1] = {
      key = key,
      x = tonumber(x),
      y = tonumber(y),
      width = tonumber(width),
      height = tonumber(height),
      texture = tonumber(texture),
      trail = tonumber(trail),
      left = tonumber(left),
    }
  end
  return {
    step = value("battle.intro.step", "done"),
    anger = marks("battle.intro.anger", 3),
    flee = marks("battle.intro.flee", 8),
    anger_scale = whole("battle.intro.anger.scale"),
    flash = whole("battle.intro.flash"),
    pieces = pieces,
  }
end

local number_icons = { "hp", "mp", "offense", "defense", "speed" }
local number_kinds = { [11] = "gain", [12] = "poison" }

-- The numbers over the fighters: x and y (the anchor, in percent of the window), dx and dy (the
-- first digit from it), value, kind ("damage", "gain" or "poison") and icon (the stat it is:
-- "hp", "mp", "offense", "defense", "speed", or nil).
function battle.numbers()
  local values = numbers("battle.damage.numbers")
  local list = {}
  for base = 0, #values - 7, 7 do
    list[#list + 1] = {
      x = values[base + 1],
      y = values[base + 2],
      dx = values[base + 3],
      dy = values[base + 4],
      value = math.floor(values[base + 5]),
      kind = number_kinds[values[base + 6]] or "damage",
      icon = number_icons[math.floor(values[base + 7])],
    }
  end
  return list
end

-- The enemies' HP bars beside them: always (the player wants them the whole fight) and bars, each
-- {enemy (1 to 3), x, y (the anchor, in percent), left, top (the bar's corner from it), fill (the
-- fill's width), red, green (its tint, 128 as it is, 255 twice as bright), hp}.
function battle.hp_bars()
  local values = numbers("battle.hp_bars")
  local bars = {}
  for base = 0, #values - 9, 9 do
    bars[#bars + 1] = {
      enemy = math.floor(values[base + 1]),
      x = values[base + 2],
      y = values[base + 3],
      left = values[base + 4],
      top = values[base + 5],
      fill = values[base + 6],
      red = values[base + 7],
      green = values[base + 8],
      hp = values[base + 9],
    }
  end
  return { always = ui.setting("battle.hp_bars.always") == true, bars = bars }
end

-- The marks over the fighters of a cup match between two of the same species: each {fighter (0 the
-- partner, 1 the rival), x, y (the anchor, in percent)}.
function battle.markers()
  local values = numbers("battle.markers")
  local list = {}
  for base = 0, #values - 3, 3 do
    list[#list + 1] = {
      fighter = math.floor(values[base + 1]),
      x = values[base + 2],
      y = values[base + 3],
    }
  end
  return list
end

-- The countdown over the fallen partner, or nil: x, y (the anchor, in percent), digit (1 to 5),
-- digit_scale, badge_scale, turn (degrees) and palette (0 to 2).
function battle.death_countdown()
  local values = numbers("battle.death_countdown")
  if #values ~= 7 then
    return nil
  end
  return {
    x = values[1],
    y = values[2],
    digit = values[3],
    digit_scale = values[4],
    badge_scale = values[5],
    turn = values[6],
    palette = values[7],
  }
end

-- The player's shout over the partner, or nil: text_key, x, y (the anchor, in percent), node_x,
-- node_y (the partner's point from the screen's middle), line (its line), hop and scale.
function battle.shout()
  if not value("battle.shout.shown", false) then
    return nil
  end
  return {
    text_key = value("battle.shout.text", ""),
    x = value("battle.shout.anchor_x", 50),
    y = value("battle.shout.anchor_y", 50),
    node_x = whole("battle.shout.node_x"),
    node_y = whole("battle.shout.node_y"),
    line = whole("battle.shout.y"),
    hop = whole("battle.shout.hop"),
    scale = value("battle.shout.scale", 1),
  }
end

-- The end box: outcome_key, lines (each {parts, name_key, subject_key, amount}: parts are text
-- keys, or marks in braces: {color:7}, {color:1}, {br}, {wait}, {name}, {subject}, {amount}),
-- stats (HP, MP, offense, defense, speed and brain: value before, max and gain), bits, money (with
-- the bits in), count_delay_frames and wait_frames (the boxes' pacing, in battle frames).
function battle.result()
  local lines = {}
  for index = 0, whole("battle.end.count") - 1 do
    local parts = {}
    for part in string.gmatch(value(entry("battle.end", index, "parts"), ""), "[^|]+") do
      parts[#parts + 1] = part
    end
    lines[index + 1] = {
      parts = parts,
      name_key = value(entry("battle.end", index, "name"), ""),
      subject_key = value(entry("battle.end", index, "subject"), ""),
      amount = whole(entry("battle.end", index, "amount")),
    }
  end
  local stats = {}
  for index = 0, 5 do
    stats[index + 1] = {
      value = whole(entry("battle.end.stat", index, "value")),
      max = whole(entry("battle.end.stat", index, "max")),
      gain = whole(entry("battle.end.gain", index, "value")),
    }
  end
  return {
    outcome_key = value("battle.end.outcome", ""),
    lines = lines,
    stats = stats,
    bits = whole("battle.end.bits"),
    money = whole("battle.end.money"),
    count_delay_frames = whole("battle.end.count_delay_frames"),
    wait_frames = whole("battle.end.wait_frames"),
  }
end

local function match_number(name, fallback)
  local found = ui.setting(name)
  if type(found) ~= "number" then
    return fallback
  end
  return found
end

local function match_text(name)
  local found = ui.setting(name)
  if type(found) ~= "string" then
    return ""
  end
  return found
end

local function match_flag(name)
  local found = ui.setting(name)
  return found == true or (type(found) == "number" and found ~= 0)
end

-- What a cup match shows over the arena (texts already in the player's language): shown, name and
-- name_y (the fighter's name as far as it came in, and its row), stats (the stats box shows),
-- counted (its numbers show), stat (6 rows: label, value, width of the bar), time_up and
-- time_up_label, box (the damage box shows), partner, rival, damage_label, dealt, taken, and wipe
-- (32 columns' heights, 0 to 240). Positions are from the screen's centre.
function battle.match()
  local stat = {}
  for index = 0, 5 do
    local prefix = "tournament_match.stat." .. index .. "."
    stat[index + 1] = {
      label = match_text(prefix .. "label"),
      value = match_number(prefix .. "value", 0),
      width = match_number(prefix .. "width", 1),
    }
  end
  local wipe = {}
  for index = 0, 31 do
    wipe[index + 1] = match_number("tournament_match.wipe." .. index, 0)
  end
  return {
    shown = match_flag("tournament_match.shown"),
    name = match_text("tournament_match.name"),
    name_y = match_number("tournament_match.name_y", 0),
    stats = match_flag("tournament_match.stats"),
    counted = match_flag("tournament_match.counted"),
    stat = stat,
    time_up = match_flag("tournament_match.time_up"),
    time_up_label = match_text("tournament_match.time_up_label"),
    box = match_flag("tournament_match.box"),
    partner = match_text("tournament_match.partner"),
    rival = match_text("tournament_match.rival"),
    damage_label = match_text("tournament_match.damage_label"),
    dealt = match_number("tournament_match.dealt", 0),
    taken = match_number("tournament_match.taken", 0),
    wipe = wipe,
  }
end

-- The windows the commands work in, as the game's: the fight's input (the opening, the poses and
-- the end box take no command), the bag open over it, the end box.
local not_now = "the battle takes no such command now"

local function whole_number(number)
  return type(number) == "number" and number == math.floor(number)
end

-- Asks the partner to follow a command of the strip (its command number, battle.commands()).
function battle.command(command)
  if not whole_number(command) or command < 0 or command > 255 then
    return false, "command must be a whole number from 0 to 255"
  end
  if phase() ~= "fight" then
    return false, not_now
  end
  ui.send("battle_command", command)
  return true
end

-- One press of the finisher's charge, while its aura lasts.
function battle.charge()
  if phase() ~= "fight" then
    return false, not_now
  end
  ui.send("battle_charge")
  return true
end

-- Asks to open the player's bag over the fight (the game shows the partner_menu screen on its items
-- page) when it can open.
function battle.open_bag()
  if phase() ~= "fight" or not value("battle.bag.available", false) then
    return false, not_now
  end
  ui.send("battle_bag_open")
  return true
end

-- Asks to close the player's bag and go back to the fight.
function battle.close_bag()
  if not value("battle.bag.open", false) then
    return false, not_now
  end
  ui.send("battle_bag_close")
  return true
end

-- The end box is done: the battle closes.
function battle.close()
  if phase() ~= "end" then
    return false, not_now
  end
  ui.send("battle_close")
  return true
end

-- A point or a bit of the end box counted (its sound), and all the bits in (the sound that ends
-- the count): the UI paces the count and the game plays its sounds.
function battle.end_count()
  if phase() ~= "end" then
    return false, not_now
  end
  ui.send("battle_end_count")
  return true
end

function battle.end_counted()
  if phase() ~= "end" then
    return false, not_now
  end
  ui.send("battle_end_counted")
  return true
end

-- The battle's events (hooks.on).
event_source("battle.active", "battle.started", true)
event_source("battle.active", "battle.ended", false)
event_source("battle.phase", "battle.phase")
event_source("battle.revision", "battle.changed")
event_source("battle.fast", "battle.speed")
event_source("battle.menu.motion", "battle.strip_moved")
event_source("battle.intro.revision", "battle.intro")
event_source("battle.damage.revision", "battle.numbers")
event_source("battle.hp_bars.revision", "battle.overlays")
event_source("battle.hp_bars.always", "battle.overlays")
event_source("battle.shout.revision", "battle.shout")
event_source("tournament_match.revision", "battle.match")
