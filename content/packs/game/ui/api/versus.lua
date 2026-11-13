-- The UI API's versus (docs/modding/ui-api.md, namespace versus): the online versus as the game
-- publishes it (apps/game: versus_flow.cpp, versus_arena.cpp), and the steps that ask the game to
-- move through it. Every rule is the game's: which view shows, where the cursor is, what each side
-- has, how the score screen moves. A step only asks; the game answers with versus.changed. The
-- arena's opening banner is the battle's (battle.intro()).
--
-- Every function returns a fresh table; changing it changes nothing in the game.

versus = {}

local function value(name, fallback)
  local found = ui.setting(name)
  if found == nil then
    return fallback
  end
  return found
end

local function whole(name)
  return math.floor(tonumber(ui.setting(name)) or 0)
end

local function text(name)
  local found = ui.setting(name)
  return found == nil and "" or tostring(found)
end

local function flag(name)
  return ui.setting(name) == true
end

local function entry(list, index, field)
  return list .. "." .. index .. "." .. field
end

-- The versus' state: view (menu, host_setup, hosting, join_entry, joining, save_pick, lineup,
-- waiting_lineup, match, intermission, result, error), cursor (the focused row of the view, or
-- the hovered command in a match), battles (the series' length), and in a match: round, clock
-- (its seconds), side (the player's side, 0 the host's and 1 the guest's), wins (each side's),
-- stalled (the rival has sent nothing for a while), leave_asked (one more cancel leaves), blink
-- (the hovered icon shows its second frame) and arena (what the arena shows: "banner", "fight",
-- "ending", "score", "" for nothing, or nil without an arena).
function versus.state()
  local view = text("versus.view")
  return {
    view = view ~= "" and view or "menu",
    cursor = whole("versus.cursor"),
    battles = whole("versus.battles"),
    round = whole("versus.round"),
    clock = whole("versus.clock"),
    side = whole("versus.local"),
    wins = { whole("versus.wins.0"), whole("versus.wins.1") },
    stalled = flag("versus.stalled"),
    leave_asked = flag("versus.leave_asked"),
    blink = flag("versus.blink"),
    arena = ui.setting("versus.show"),
  }
end

-- The host's setup: series (the chosen length's row) of series_count, arena (the chosen arena's
-- number, for its text key versus.arena.<n>), arena_row of arenas.
function versus.setup()
  return {
    series = whole("versus.choice"),
    series_count = whole("versus.choices"),
    arena = whole("versus.arena"),
    arena_row = whole("versus.arena_choice"),
    arenas = whole("versus.arenas"),
  }
end

-- The connection while hosting or joining: stage (creating, waiting, connecting) and the room's
-- code ("" until there is one).
function versus.connection()
  return { stage = text("versus.stage"), code = text("versus.code") }
end

-- The room code typed so far, and whether the last one was refused (bad).
function versus.entry()
  return { typed = text("versus.typed"), bad = flag("versus.bad_entry") }
end

-- The saves to fight with: player, nickname (the partner's), partners (how many it has) and
-- usable (it has enough for the series).
function versus.saves()
  local list = {}
  for index = 0, whole("versus.saves") - 1 do
    list[index + 1] = {
      player = text(entry("versus.save", index, "player")),
      nickname = text(entry("versus.save", index, "nickname")),
      partners = whole(entry("versus.save", index, "partners")),
      usable = flag(entry("versus.save", index, "usable")),
    }
  end
  return list
end

-- The lineup: partners (name, species_key, stats as one text, picked) and picks (the names picked
-- so far, in order).
function versus.lineup()
  local partners, picks = {}, {}
  for index = 0, whole("versus.partners") - 1 do
    partners[index + 1] = {
      name = text(entry("versus.partner", index, "name")),
      species_key = text(entry("versus.partner", index, "species")),
      stats = text(entry("versus.partner", index, "stats")),
      picked = flag(entry("versus.partner", index, "picked")),
    }
  end
  for index = 0, whole("versus.picks") - 1 do
    picks[index + 1] = text(entry("versus.pick", index, "name"))
  end
  return { partners = partners, picks = picks }
end

-- The two sides of a match, the host's first: hp, max_hp, mp, max_mp, segments (the finisher's
-- letters lit, 0 to 6), finisher_ready, hover (the hovered command's place on the strip: the
-- player's cursor, the rival's current command), icons (the strip's icons in its order, the
-- finisher's "") and command_key (the hovered command's text key, or "").
function versus.fighters()
  local list = {}
  for side = 0, 1 do
    local prefix = "versus.f." .. side .. "."
    local icons = {}
    for index = 0, whole(prefix .. "menu") - 1 do
      icons[index + 1] = text(prefix .. "menu." .. index .. ".icon")
    end
    list[side + 1] = {
      hp = whole(prefix .. "hp"),
      max_hp = whole(prefix .. "max_hp"),
      mp = whole(prefix .. "mp"),
      max_mp = whole(prefix .. "max_mp"),
      segments = whole(prefix .. "segments"),
      finisher_ready = flag(prefix .. "finisher_ready"),
      hover = whole(prefix .. "hover"),
      icons = icons,
      command_key = text(prefix .. "command_name"),
    }
  end
  return list
end

-- What happened in the match, the latest last: key (the line's text key), who, what_key (the
-- move's text key, or "") and amount.
function versus.log()
  local list = {}
  for index = 0, whole("versus.log") - 1 do
    list[index + 1] = {
      key = text(entry("versus.log", index, "key")),
      who = text(entry("versus.log", index, "who")),
      what_key = text(entry("versus.log", index, "what")),
      amount = whole(entry("versus.log", index, "amount")),
    }
  end
  return list
end

-- The series so far: rounds (each played battle's outcome for the player: won, lost or draw) and
-- winner (you, rival or draw once the series is over, else "").
function versus.results()
  local rounds = {}
  for index = 0, whole("versus.results") - 1 do
    rounds[index + 1] = text(entry("versus.result", index, "outcome"))
  end
  return { rounds = rounds, winner = text("versus.winner") }
end

-- The score screen between battles: round (the row that slides), offset (how far, in the
-- original's pixels), flash (the round's flash shows), black (the fade's opacity, 0 to 1), results
-- (each played battle's winner: host, guest or draw) and sides (host, guest: wins, losses and
-- faces, each battle's fighter face number).
function versus.score()
  local results, sides = {}, {}
  for index = 0, whole("versus.score.results") - 1 do
    results[index + 1] = text("versus.score.result." .. index)
  end
  for slot = 0, 1 do
    local faces = {}
    for row = 0, whole("versus.battles") - 1 do
      faces[row + 1] = whole("versus.score.face." .. slot .. "." .. row)
    end
    sides[slot + 1] = {
      wins = whole("versus.score.wins." .. slot),
      losses = whole("versus.score.losses." .. slot),
      faces = faces,
    }
  end
  return {
    round = whole("versus.score.round"),
    offset = whole("versus.score.offset"),
    flash = flag("versus.score.flash"),
    black = math.max(0, math.min(1, tonumber(value("versus.score.black", 0)) or 0)),
    results = results,
    sides = sides,
  }
end

-- Why the versus stopped: key (its text key) and detail (the error's own words, not translated).
function versus.error()
  return { key = text("versus.error_key"), detail = text("versus.error_detail") }
end

-- The steps the versus takes, as the game's navigation names them; "finisher" calls the
-- finisher in a match once its gauge is full.
local steps = {
  up = true,
  down = true,
  left = true,
  right = true,
  confirm = true,
  cancel = true,
  menu = true,
  tab_previous = true,
  tab_next = true,
  finisher = true,
}

local function open()
  return ui.setting("versus.view") ~= nil
end

local not_now = "the versus is not open"

-- One step in the versus' view: up, down, left, right, confirm, cancel, menu, tab_previous,
-- tab_next or finisher.
function versus.step(step)
  if type(step) ~= "string" or not steps[step] then
    return false, "step must be a navigation step or finisher"
  end
  if not open() then
    return false, not_now
  end
  ui.send("versus_action", step)
  return true
end

-- Picks a row of the view (its menu entry, save, partner, or a command of the player's strip by
-- its place): the cursor goes there and confirms.
function versus.pick(row)
  if type(row) ~= "number" or row ~= math.floor(row) or row < 0 or row > 255 then
    return false, "row must be a whole number from 0 to 255"
  end
  if not open() then
    return false, not_now
  end
  ui.send("versus_action", "pick:" .. row)
  return true
end

-- The versus' event (hooks.on).
event_source("versus.revision", "versus.changed")
