-- The online versus (apps/game/src/versus_flow.cpp). The game drives every view and publishes it
-- as settings, bumping versus.revision once per change: versus.view (menu, host_setup, hosting,
-- join_entry, joining, save_pick, lineup, waiting_lineup, match, intermission, result, error),
-- versus.cursor, versus.battles, and each view's own values (see VersusFlow::settings). Every
-- button and navigation step goes to the game as versus_action; a click on a row sends
-- "pick:<row>".

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

local function setting(name)
  return ui.setting(name)
end

local function number(name)
  return math.floor(tonumber(ui.setting(name)) or 0)
end

local function text(name)
  local value = ui.setting(name)
  return value == nil and "" or tostring(value)
end

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

local function score()
  return say("versus.score", { host = number("versus.wins.0"), guest = number("versus.wins.1") })
end

local function draw_host_setup(document)
  document
    :get("series")
    :set_text(say("versus.host_setup.battles", { count = number("versus.battles") }))
  focus(document, "series")
end

local function draw_hosting(document)
  local codes = text("versus.codes")
  local addresses = text("versus.addresses")
  document:get("hosting-codes"):set_text(codes ~= "" and codes or t("versus.hosting.no_address"))
  document:get("hosting-addresses"):set_text(
    addresses ~= "" and addresses or say("versus.hosting.port", { port = number("versus.port") })
  )
end

local function draw_join(document)
  document:get("typed"):set_text(text("versus.typed") .. "_")
  show(document, "bad-entry", setting("versus.bad_entry") == true)
end

local function draw_saves(document)
  local count = number("versus.saves")
  for row = 0, SaveRows - 1 do
    local id = "save-" .. row
    local shown = row < count
    show(document, id, shown)
    if shown then
      local prefix = "versus.save." .. row .. "."
      document:get(id):set_text(say("versus.save_pick.row", {
        player = text(prefix .. "player"),
        partner = text(prefix .. "nickname"),
        count = number(prefix .. "partners"),
      }))
      document:get(id):set_class("dimmed", setting(prefix .. "usable") ~= true)
    end
  end
  show(document, "no-saves", count == 0)
  focus(document, "save-" .. number("versus.cursor"))
end

local function draw_lineup(document)
  local count = number("versus.partners")
  local picks = number("versus.picks")
  document
    :get("lineup-title")
    :set_text(
      say("versus.lineup.title", { number = picks + 1, battles = number("versus.battles") })
    )
  for row = 0, PartnerRows - 1 do
    local id = "partner-" .. row
    local shown = row < count
    show(document, id, shown)
    if shown then
      local prefix = "versus.partner." .. row .. "."
      document:get(id):set_text(say("versus.lineup.row", {
        name = text(prefix .. "name"),
        species = t(text(prefix .. "species")),
        stats = text(prefix .. "stats"),
      }))
      document:get(id):set_class("dimmed", setting(prefix .. "picked") == true)
    end
  end
  local names = {}
  for index = 0, picks - 1 do
    names[#names + 1] = text("versus.pick." .. index .. ".name")
  end
  document
    :get("picks")
    :set_text(
      picks > 0 and say("versus.lineup.picked", { names = table.concat(names, ", ") }) or ""
    )
  focus(document, "partner-" .. number("versus.cursor"))
end

local function draw_fighter(document, side)
  local prefix = "versus.f." .. side .. "."
  local you = number("versus.local") == side
  document
    :get("f" .. side .. "-name")
    :set_text(say(you and "versus.match.you" or "versus.match.rival", {
      name = text(prefix .. "name"),
      species = t(text(prefix .. "species")),
    }))
  document
    :get("f" .. side .. "-hp")
    :set_property("width", percent(number(prefix .. "hp"), number(prefix .. "max_hp")))
  document
    :get("f" .. side .. "-mp")
    :set_property("width", percent(number(prefix .. "mp"), number(prefix .. "max_mp")))
  document
    :get("f" .. side .. "-finisher")
    :set_property("width", string.format("%d%%", number(prefix .. "finisher")))
  local command = text(prefix .. "command")
  document:get("f" .. side .. "-numbers"):set_text(say("versus.match.numbers", {
    hp = number(prefix .. "hp"),
    max_hp = number(prefix .. "max_hp"),
    mp = number(prefix .. "mp"),
    max_mp = number(prefix .. "max_mp"),
    command = command ~= "" and t(command) or "",
  }))
end

local function draw_match(document)
  document:get("match-top"):set_text(say("versus.match.top", {
    round = number("versus.round"),
    battles = number("versus.battles"),
    clock = number("versus.clock"),
    score = score(),
  }))
  draw_fighter(document, 0)
  draw_fighter(document, 1)
  local count = number("versus.menu")
  for row = 0, CommandRows - 1 do
    local id = "command-" .. row
    local shown = row < count
    show(document, id, shown)
    if shown then
      local key = text("versus.menu." .. row .. ".key")
      document:get(id):set_text(key ~= "" and t(key) or "?")
    end
  end
  for row = 0, LogRows - 1 do
    local prefix = "versus.log." .. row .. "."
    local line = row < number("versus.log")
        and say(text(prefix .. "key"), {
          who = text(prefix .. "who"),
          move = text(prefix .. "what") ~= "" and t(text(prefix .. "what")) or "",
          amount = number(prefix .. "amount"),
        })
      or ""
    document:get("log-" .. row):set_text(line)
  end
  show(document, "stalled", setting("versus.stalled") == true)
  show(document, "leave-asked", setting("versus.leave_asked") == true)
  focus(document, "command-" .. number("versus.cursor"))
end

local function rounds_text()
  local marks = {}
  for index = 0, number("versus.results") - 1 do
    marks[#marks + 1] = t("versus.outcome." .. text("versus.result." .. index .. ".outcome"))
  end
  return table.concat(marks, " · ")
end

local function draw_intermission(document)
  local last = number("versus.results") - 1
  local outcome = last >= 0 and text("versus.result." .. last .. ".outcome") or "draw"
  document:get("round-result"):set_text(say("versus.intermission." .. outcome, {
    round = number("versus.round"),
  }))
  document:get("intermission-score"):set_text(score())
end

local function draw_result(document)
  document:get("winner"):set_text(t("versus.result." .. text("versus.winner")))
  document:get("result-score"):set_text(score())
  document:get("result-rounds"):set_text(rounds_text())
end

local function draw_error(document)
  document:get("error"):set_text(t(text("versus.error_key")))
  document:get("error-detail"):set_text(text("versus.error_detail"))
end

local function refresh(document)
  local view = text("versus.view")
  if view == "" then
    view = "menu"
  end
  for _, name in ipairs(Views) do
    show(document, "view-" .. name, name == view)
  end
  if view == "menu" then
    focus(document, "menu-" .. number("versus.cursor"))
  elseif view == "host_setup" then
    draw_host_setup(document)
  elseif view == "hosting" then
    draw_hosting(document)
  elseif view == "join_entry" then
    draw_join(document)
  elseif view == "save_pick" then
    draw_saves(document)
  elseif view == "lineup" then
    draw_lineup(document)
  elseif view == "match" then
    draw_match(document)
  elseif view == "intermission" then
    draw_intermission(document)
  elseif view == "result" then
    draw_result(document)
  elseif view == "error" then
    draw_error(document)
  end
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

function act(action)
  ui.send("versus_action", action)
end

function pick(row)
  ui.send("versus_action", "pick:" .. row)
end

function on_show(document)
  refresh_prompts(document)
  refresh(document)
end

function on_setting(document, name)
  if name == "versus.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh_prompts(document)
  refresh(document)
end

function on_device(document)
  refresh_prompts(document)
end

-- Every step is the game's: the document neither moves a focus nor closes by itself.
function on_action(_document, action)
  ui.send("versus_action", action)
  return true
end
