-- The online versus (apps/game/src/versus_flow.cpp), read only through the UI API's versus
-- namespace (docs/modding/ui-api.md): the game drives every view and fires versus.changed once per
-- change; versus.state() says which view shows and each view has its own query. Every button and
-- navigation step goes to the game as versus.step(); a click on a row is versus.pick(row). The
-- arena's opening banner is the battle's (battle.intro()).

local Views = {
  "menu",
  "host_setup",
  "hosting",
  "join_entry",
  "joining",
  "save_pick",
  "lineup",
  "waiting_lineup",
  "match",
  "intermission",
  "result",
  "error",
}
local SaveRows = 8
local PartnerRows = 10
local CommandRows = 8
local LogRows = 4
-- The arena's HUD (vs_hud.c): each side's command strip has room for the finisher, the seven
-- commands and the three techniques.
local StripSlots = 11
local FinisherMarks = 8
local BannerPieces = 200
local ScoreRows = 5
-- The room code's characters, one cell each (net/signal.hpp: RoomCodeLength).
local CodeCells = 4

-- A text key's text with its {name} placeholders filled; a missing key shows nothing.
local function say(key, values)
  if key == nil or key == "" then
    return ""
  end
  return (
    string.gsub(t(key), "{([%w_]+)}", function(name)
      local value = values and values[name]
      return value ~= nil and tostring(value) or ""
    end)
  )
end

-- Elements looked up once, and the properties and classes last set on each: the arena's HUD and
-- the banner's pieces change every frame, and a lookup walks the whole document.
local elements = {}
local applied = {}
local shown_pieces, piece_size, piece_texture, piece_trail = {}, {}, {}, {}

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

local function set_text(document, id, value)
  local key = id .. " text"
  if applied[key] ~= value then
    applied[key] = value
    lookup(document, id):set_text(value)
  end
end

-- Puts one class of a set on an element and takes the set's others off.
local function swap_class(document, id, set, class)
  local key = id .. " set " .. set
  local old = applied[key]
  if old == class then
    return
  end
  if old ~= nil then
    lookup(document, id):set_class(old, false)
  end
  if class ~= nil then
    lookup(document, id):set_class(class, true)
  end
  applied[key] = class
end

local function show(document, id, visible)
  document:get(id):set_class("hidden", not visible)
end

local function focus(document, id)
  local element = document:get(id)
  if element ~= nil and not element:has_class("hidden") then
    element:focus()
  end
end

local function percent(value, total)
  if total <= 0 then
    return "0%"
  end
  return string.format("%.1f%%", math.max(0, math.min(100, 100 * value / total)))
end

local function score(state)
  return say("versus.score", { host = state.wins[1], guest = state.wins[2] })
end

-- The host's series length and arena, each a value with its arrows beside it (vs_select.c:
-- VS__tickSelectMap goes round the stages).
local function draw_host_setup(document, state)
  local count = state.battles
  local setup = versus.setup()
  document:get("series"):set_text(
    say(count == 1 and "versus.host_setup.battle" or "versus.host_setup.battles", { count = count })
  )
  document:get("arena"):set_text(t("versus.arena." .. setup.arena))
  show(document, "setup-1", setup.arenas > 1)
  focus(document, "setup-" .. state.cursor)
end

-- The host's stage (creating the room, waiting for the guest, connecting) and its room code.
local function draw_hosting(document)
  local connection = versus.connection()
  local code = connection.code
  local stage = connection.stage
  document
    :get("hosting-stage")
    :set_text(t("versus.hosting." .. (stage ~= "" and stage or "creating")))
  show(document, "hosting-code-label", code ~= "")
  show(document, "hosting-code", code ~= "")
  document:get("hosting-code"):set_text(code)
end

local function draw_join(document)
  local entry = versus.entry()
  local typed = entry.typed
  document:get("typed"):set_text(typed .. "_")
  -- The same entry one character per cell, the next cell to fill marked, for skins that box it.
  for cell = 0, CodeCells - 1 do
    local element = document:get("typed-" .. cell)
    element:set_text(typed:sub(cell + 1, cell + 1))
    element:set_class("next", cell == #typed)
  end
  show(document, "bad-entry", entry.bad)
end

local function draw_saves(document, state)
  local saves = versus.saves()
  local count = #saves
  for row = 0, SaveRows - 1 do
    local id = "save-" .. row
    local save = saves[row + 1]
    show(document, id, save ~= nil)
    if save ~= nil then
      document:get(id):set_text(say("versus.save_pick.row", {
        player = save.player,
        partner = save.nickname,
        count = save.partners,
      }))
      document:get(id):set_class("dimmed", not save.usable)
    end
  end
  show(document, "no-saves", count == 0)
  focus(document, "save-" .. state.cursor)
end

local function draw_lineup(document, state)
  local lineup = versus.lineup()
  local picks = #lineup.picks
  document
    :get("lineup-title")
    :set_text(say("versus.lineup.title", { number = picks + 1, battles = state.battles }))
  for row = 0, PartnerRows - 1 do
    local id = "partner-" .. row
    local partner = lineup.partners[row + 1]
    show(document, id, partner ~= nil)
    if partner ~= nil then
      document:get(id):set_text(say("versus.lineup.row", {
        name = partner.name,
        species = t(partner.species_key),
        stats = partner.stats,
      }))
      document:get(id):set_class("dimmed", partner.picked)
    end
  end
  document:get("picks"):set_text(
    picks > 0 and say("versus.lineup.picked", { names = table.concat(lineup.picks, ", ") }) or ""
  )
  focus(document, "partner-" .. state.cursor)
end

-- The text panel's match view shows only without an arena (the arena's HUD covers it): the bars,
-- the numbers and the log. The game names neither the fighters nor the commands for it, so their
-- rows stay empty.
local function draw_fighter(document, side, fighter, state)
  local you = state.side == side
  document
    :get("f" .. side .. "-name")
    :set_text(say(you and "versus.match.you" or "versus.match.rival", { name = "", species = "" }))
  document:get("f" .. side .. "-hp"):set_property("width", percent(fighter.hp, fighter.max_hp))
  document:get("f" .. side .. "-mp"):set_property("width", percent(fighter.mp, fighter.max_mp))
  document
    :get("f" .. side .. "-finisher")
    :set_property("width", percent(fighter.finisher_ready and 6 or fighter.segments, 6))
  document:get("f" .. side .. "-numbers"):set_text(say("versus.match.numbers", {
    hp = fighter.hp,
    max_hp = fighter.max_hp,
    mp = fighter.mp,
    max_mp = fighter.max_mp,
    command = you and fighter.command_key ~= "" and t(fighter.command_key) or "",
  }))
end

local function draw_match(document, state)
  document:get("match-top"):set_text(say("versus.match.top", {
    round = state.round,
    battles = state.battles,
    clock = state.clock,
    score = score(state),
  }))
  local fighters = versus.fighters()
  draw_fighter(document, 0, fighters[1], state)
  draw_fighter(document, 1, fighters[2], state)
  for row = 0, CommandRows - 1 do
    show(document, "command-" .. row, false)
  end
  local log = versus.log()
  for row = 0, LogRows - 1 do
    local line = log[row + 1]
    document:get("log-" .. row):set_text(line ~= nil and say(line.key, {
      who = line.who,
      move = line.what_key ~= "" and t(line.what_key) or "",
      amount = line.amount,
    }) or "")
  end
  show(document, "stalled", state.stalled)
  show(document, "leave-asked", state.leave_asked)
  focus(document, "command-" .. state.cursor)
end

local function rounds_text(results)
  local marks = {}
  for _, outcome in ipairs(results.rounds) do
    marks[#marks + 1] = t("versus.outcome." .. outcome)
  end
  return table.concat(marks, " · ")
end

local function draw_intermission(document, state)
  local rounds = versus.results().rounds
  local outcome = rounds[#rounds] or "draw"
  document:get("round-result"):set_text(say("versus.intermission." .. outcome, {
    round = state.round,
  }))
  document:get("intermission-score"):set_text(score(state))
end

local function draw_result(document, state)
  local results = versus.results()
  document:get("winner"):set_text(t("versus.result." .. results.winner))
  document:get("result-score"):set_text(score(state))
  document:get("result-rounds"):set_text(rounds_text(results))
end

local function draw_error(document)
  local stopped = versus.error()
  document:get("error"):set_text(t(stopped.key))
  document:get("error-detail"):set_text(stopped.detail)
end

-- One side's bars (vs_hud.c: VS_renderFighterStatusBars): the fill is 50 pixels at most, 3dp each.
local function draw_bar(document, side, fighter, name)
  local value, total = fighter[name], fighter["max_" .. name]
  local pixels = total > 0 and math.floor(math.max(0, math.min(value, total)) * 50 / total) or 0
  set_property(document, "f" .. side .. "-" .. name .. "-fill", "width", (pixels * 3) .. "dp")
  set_text(document, "f" .. side .. "-" .. name .. "-value", tostring(value))
  hud_digits.draw(document, "f" .. side .. "-" .. name .. "-d-", value, set_class)
end

-- The finisher's letters (vs_hud.c: VS_renderHPBarDigits): "FINISH" a letter per sixth of the
-- gauge, "!!" once it is full, and the "PUSH" box. In another language the word shows in the
-- original's font, as many of its letters as the gauge lights.
local function draw_finisher(document, side, fighter)
  local ready = fighter.finisher_ready
  local lit = ready and FinisherMarks or fighter.segments
  for mark = 0, FinisherMarks - 1 do
    set_class(document, "f" .. side .. "-seg-" .. mark, "filled", mark < lit)
  end
  local word = t("battle.finisher_word")
  local letters = utf8.len(word) or #word
  local shown = math.floor(letters * lit / FinisherMarks)
  local stop = utf8.offset(word, shown + 1) or (#word + 1)
  set_text(document, "f" .. side .. "-word", string.sub(word, 1, stop - 1))
  set_class(document, "f" .. side .. "-push", "hidden", not ready)
end

-- A command's place on the strip (vs_hud.c: VS_renderCommandMenu): the last command is the first
-- icon from the left, each one 14 pixels on, every other one lower; the finisher has no icon.
local function draw_strip(document, side, fighter, fighting, blink)
  local count = math.min(#fighter.icons, StripSlots)
  local hovered = fighter.hover
  for index = 0, StripSlots - 1 do
    local frame = "f" .. side .. "-frame-" .. index
    local icon_id = "f" .. side .. "-cmd-" .. index
    set_class(document, frame, "hidden", index >= count)
    local icon = index < count and fighter.icons[index + 1] or ""
    set_class(document, icon_id, "hidden", icon == "")
    if index < count then
      local kind = "frame-even"
      if index == 0 then
        kind = "frame-left"
      elseif index == count - 1 then
        kind = count % 2 == 1 and "frame-right-odd" or "frame-right-even"
      elseif index % 2 == 1 then
        kind = "frame-odd"
      end
      swap_class(document, frame, "kind", kind)
      swap_class(document, frame, "frame-place", "piece-" .. index)
    end
    if icon ~= "" then
      local place = count - 1 - index
      swap_class(document, icon_id, "icon", "icon-" .. icon)
      swap_class(document, icon_id, "place", "place-" .. place)
      set_class(document, icon_id, "down", place % 2 == 1)
      set_class(document, icon_id, "blink", fighting and index == hovered and blink)
    end
  end
  local cursor = "f" .. side .. "-cursor"
  local shown = fighting and hovered >= 1 and hovered < count
  set_class(document, cursor, "hidden", not shown)
  if shown then
    local place = count - 1 - hovered
    swap_class(document, cursor, "place", "place-" .. place)
    set_class(document, cursor, "down", place % 2 == 1)
  end
  local name = fighter.command_key
  set_text(document, "f" .. side .. "-command", fighting and name ~= "" and t(name) or "")
end

-- The clock between the bars (vs_hud.c: the STD clock's panel, digits and the 1P and 2P pieces):
-- the tens left out under 10.
local function draw_clock(document, state)
  local seconds = math.max(0, math.min(99, state.clock))
  local digits = { math.floor(seconds / 10), seconds % 10 }
  for place = 0, 1 do
    local id = "vs-clock-d-" .. place
    local digit = digits[place + 1]
    local visible = place == 1 or digit ~= 0
    set_class(document, id, "hidden", not visible)
    swap_class(document, id, "digit", visible and ("d-" .. digit) or nil)
    set_text(document, id, visible and tostring(digit) or "")
  end
end

-- The finisher's key in the local side's "PUSH" box; the rival's box shows the word alone. Set
-- again when the local side or the input device changes.
local function draw_push_keys(document, state)
  local own = state.side
  if applied["push keys"] == own then
    return
  end
  applied["push keys"] = own
  for side = 0, 1 do
    local key = lookup(document, "f" .. side .. "-push-key")
    key:set_class("hidden", side ~= own)
    if side == own then
      key:set_prompt("extra")
    end
  end
end

local function draw_hud(document, state, show_name)
  draw_push_keys(document, state)
  local fighting = show_name == "fight"
  local fighters = versus.fighters()
  for side = 0, 1 do
    local fighter = fighters[side + 1]
    draw_bar(document, side, fighter, "hp")
    draw_bar(document, side, fighter, "mp")
    draw_finisher(document, side, fighter)
    draw_strip(document, side, fighter, fighting, state.blink)
  end
  draw_clock(document, state)
  set_class(document, "hud-stalled", "hidden", not state.stalled)
  set_class(document, "hud-leave", "hidden", not state.leave_asked)
end

-- The banner's pieces (battle.intro().pieces, as the story battle's banner) are in the original's
-- pixels, 3dp each: x, y, width, height, texture, trail and 1 for a trail on the left.

local function hide_piece(document, index)
  if shown_pieces[index] == false then
    return
  end
  shown_pieces[index] = false
  set_class(document, "bp-" .. index, "hidden", true)
  set_class(document, "bs-" .. index, "hidden", true)
end

-- A piece moves by its transform: over a hundred pieces fly in each frame and a transform leaves
-- the document's layout as it was.
local function draw_piece(document, index, x, y, width, height, texture, trail, left)
  local block, shadow = lookup(document, "bp-" .. index), lookup(document, "bs-" .. index)
  if not shown_pieces[index] then
    set_class(document, "bp-" .. index, "hidden", false)
    set_class(document, "bs-" .. index, "hidden", false)
  end
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

local function draw_banner(document)
  local intro = battle.intro()
  local index = 0
  for _, piece in ipairs(intro.pieces) do
    if index >= BannerPieces then
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
  for rest = index, BannerPieces - 1 do
    hide_piece(document, rest)
  end
  for flash = 0, 2 do
    set_class(document, "banner", "flash-" .. flash, intro.flash == flash)
  end
end

-- The score screen (vs_scene.c: VS_renderVersusIntro): a row per battle and side with the
-- fighter's face and, once played, a won or lost pip; the round's row slides out and back with a
-- flash between; each side's wins and losses; the fade from and to black over it all.
local function draw_score(document, state)
  local board = versus.score()
  local battles = math.min(state.battles, ScoreRows)
  local played = #board.results
  local round = board.round
  local offset = board.offset
  for slot = 0, 1 do
    for row = 0, ScoreRows - 1 do
      local id = "score-row-" .. slot .. "-" .. row
      set_class(document, id, "hidden", row >= battles)
      set_class(document, id, "done", row < played)
      -- VS_renderVersusBanner: the round's row slides out to its side and back.
      local shift = row == round and offset * 3 * (slot == 0 and -1 or 1) or 0
      set_property(document, id, "transform", "translateX(" .. shift .. "dp)")
      local face = board.sides[slot + 1].faces[row + 1] or 0
      swap_class(document, "score-face-" .. slot .. "-" .. row, "face", "face-" .. face)
      local pip = "score-pip-" .. slot .. "-" .. row
      set_class(document, pip, "hidden", row >= played)
      if row < played then
        local result = board.results[row + 1]
        local won = (result == "host" and slot == 0) or (result == "guest" and slot == 1)
        swap_class(document, pip, "pip", won and "won" or "lost")
        set_text(document, pip, won and "O" or "X")
      end
    end
    set_text(document, "score-wins-" .. slot, tostring(board.sides[slot + 1].wins))
    set_text(document, "score-losses-" .. slot, tostring(board.sides[slot + 1].losses))
  end
  set_class(document, "score-flash", "hidden", not board.flash)
  swap_class(document, "score-flash", "row", "row-" .. round)
  set_property(document, "score-black", "opacity", string.format("%.3f", board.black))
end

-- The arena's layers: the HUD from the banner to the end of the fight, the banner while it shows,
-- the score between battles. The panel steps aside while the arena shows.
local function draw_arena(document, state)
  local view = state.view
  local arena = state.arena ~= nil
  local show_name = arena and tostring(state.arena) or ""
  local on_arena = arena and (view == "match" or view == "intermission")
  local scoring = on_arena and show_name == "score"
  local hud = on_arena and (show_name == "banner" or show_name == "fight" or show_name == "ending")
  set_class(document, "hud", "hidden", not hud)
  set_class(document, "hud", "opening", show_name == "banner")
  set_class(document, "intro", "hidden", not (on_arena and show_name == "banner"))
  set_class(document, "score", "hidden", not scoring)
  set_class(document, "scrim", "hidden", on_arena)
  if hud then
    draw_hud(document, state, show_name)
  end
  if on_arena and show_name == "banner" then
    draw_banner(document)
  end
  if scoring then
    draw_score(document, state)
  end
end

local function refresh(document)
  local state = versus.state()
  local view = state.view
  for _, name in ipairs(Views) do
    show(document, "view-" .. name, name == view)
  end
  draw_arena(document, state)
  if view == "menu" then
    focus(document, "menu-" .. state.cursor)
  elseif view == "host_setup" then
    draw_host_setup(document, state)
  elseif view == "hosting" then
    draw_hosting(document)
  elseif view == "join_entry" then
    draw_join(document)
  elseif view == "save_pick" then
    draw_saves(document, state)
  elseif view == "lineup" then
    draw_lineup(document, state)
  elseif view == "match" then
    draw_match(document, state)
  elseif view == "intermission" then
    draw_intermission(document, state)
  elseif view == "result" then
    draw_result(document, state)
  elseif view == "error" then
    draw_error(document)
  end
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
  applied["push keys"] = nil
end

function act(action)
  versus.step(action)
end

function pick(row)
  versus.pick(row)
end

-- An arrow beside a setup value: the cursor goes to its row, then the value steps.
function step(row, direction)
  local cursor = versus.state().cursor
  while cursor ~= row do
    versus.step(cursor < row and "down" or "up")
    cursor = cursor + (cursor < row and 1 or -1)
  end
  versus.step(direction)
end

-- A click on one of this side's command icons picks it; the rival's strip takes no click.
function pick_command(side, index)
  if versus.state().side == side then
    versus.pick(index)
  end
end

function on_show(document)
  refresh_prompts(document)
  refresh(document)
end

-- Each change of the versus redraws it, while the screen shows (on_show draws it all again).
hooks.on("versus.changed", function(_, document)
  if ui.is_visible(document_name) then
    refresh(document)
  end
end)

function on_language(document)
  refresh_prompts(document)
  refresh(document)
end

function on_device(document)
  refresh_prompts(document)
end

-- Every step is the game's: the document neither moves a focus nor closes by itself.
function on_action(_document, action)
  versus.step(action)
  return true
end
