-- The developer menu (docs/modulos/apps/game.md, «Herramientas de desarrollo»): F2 opens it over a
-- game while the developer tools option is on. Each entry runs console lines with
-- ui.send("dev_menu_run", line) (the game runs only the commands this menu offers), opens a page,
-- or sends the game menu's quick_save. The game publishes the lists it picks from:
-- dev_menu.<list>.count and dev_menu.<list>.<n>.id/.name (from 0) for the lists items, species
-- and maps (the maps the adventure visited). dev_menu_close closes it.

local PageSize = 8
local Amounts = { 1, 10, 99 }

local page_name = "main"
local list_page = 1
local amount = 1

local function run(lines)
  for _, line in ipairs(lines) do
    ui.send("dev_menu_run", line)
  end
end

local function stats(value, life)
  local lines = {}
  for _, stat in ipairs({ "offense", "defense", "speed", "brains" }) do
    lines[#lines + 1] = string.format('partner_set("%s %d")', stat, value)
  end
  lines[#lines + 1] = string.format('partner_set("hp %d")', life)
  lines[#lines + 1] = string.format('partner_set("mp %d")', life)
  return lines
end

-- The fixed pages: each entry has its label (a text key and its arguments) and what it does.
local Pages = {
  main = {
    title = "dev_menu.title",
    entries = {
      { label = "dev_menu.items", page = "items" },
      { label = "dev_menu.money", page = "money" },
      { label = "dev_menu.clock", page = "clock" },
      { label = "dev_menu.stats", page = "stats" },
      { label = "dev_menu.care", page = "care" },
      { label = "dev_menu.evolve", page = "species" },
      { label = "dev_menu.warp", page = "maps" },
      { label = "dev_menu.save", send = "quick_save" },
    },
  },
  money = {
    title = "dev_menu.money",
    entries = {
      {
        label = "dev_menu.money_set",
        args = { bits = 1000 },
        lines = { 'story_set("money 1000")' },
      },
      {
        label = "dev_menu.money_set",
        args = { bits = 10000 },
        lines = { 'story_set("money 10000")' },
      },
      {
        label = "dev_menu.money_set",
        args = { bits = 99999 },
        lines = { 'story_set("money 99999")' },
      },
    },
  },
  clock = {
    title = "dev_menu.clock",
    entries = {
      { label = "dev_menu.clock_speed", args = { speed = 1 }, lines = { 'clock_speed("1")' } },
      { label = "dev_menu.clock_speed", args = { speed = 2 }, lines = { 'clock_speed("2")' } },
      { label = "dev_menu.clock_speed", args = { speed = 4 }, lines = { 'clock_speed("4")' } },
      { label = "dev_menu.clock_speed", args = { speed = 8 }, lines = { 'clock_speed("8")' } },
      { label = "dev_menu.clock_speed", args = { speed = 16 }, lines = { 'clock_speed("16")' } },
    },
  },
  stats = {
    title = "dev_menu.stats",
    entries = {
      {
        label = "dev_menu.stats_set",
        args = { value = 100, life = 1000 },
        lines = stats(100, 1000),
      },
      {
        label = "dev_menu.stats_set",
        args = { value = 500, life = 5000 },
        lines = stats(500, 5000),
      },
      {
        label = "dev_menu.stats_set",
        args = { value = 999, life = 9999 },
        lines = stats(999, 9999),
      },
    },
  },
  care = {
    title = "dev_menu.care",
    entries = {
      { label = "dev_menu.happiness_full", lines = { 'partner_set("happiness 100")' } },
      { label = "dev_menu.discipline_full", lines = { 'partner_set("discipline 100")' } },
      { label = "dev_menu.rested", lines = { 'partner_set("tiredness 0")' } },
      { label = "dev_menu.no_mistakes", lines = { 'partner_set("care 0")' } },
      { label = "dev_menu.sleep", lines = { 'sleep("")' } },
      { label = "dev_menu.toilet", lines = { 'toilet("")' } },
    },
  },
}

-- The list pages: the list the game publishes and the line an entry runs with its id.
local Lists = {
  items = {
    title = "dev_menu.items",
    line = function(id)
      return string.format('give_item("%s %d")', id, amount)
    end,
  },
  species = {
    title = "dev_menu.evolve",
    line = function(id)
      return string.format('evolve("%s")', id)
    end,
  },
  maps = {
    title = "dev_menu.warp",
    line = function(id)
      return string.format('warp("%s")', id)
    end,
  },
}

local function list_count()
  return math.floor(ui.setting("dev_menu." .. page_name .. ".count") or 0)
end

local function pages()
  return math.max(1, math.ceil(list_count() / PageSize))
end

-- The entry in a slot of the shown page, nil for an empty slot.
local function entry_of(slot)
  local list = Lists[page_name]
  if list == nil then
    return Pages[page_name].entries[slot]
  end
  local index = (list_page - 1) * PageSize + slot - 1
  if index >= list_count() then
    return nil
  end
  local prefix = "dev_menu." .. page_name .. "." .. index .. "."
  local id = ui.setting(prefix .. "id") or ""
  local key = ui.setting(prefix .. "name") or ""
  return { name = key ~= "" and t(key) or id, id = id, line = list.line(id) }
end

local function label_of(entry)
  if entry.name ~= nil then
    return entry.name
  end
  return t(entry.label, entry.args or {})
end

local function refresh(document)
  local list = Lists[page_name]
  local title = list ~= nil and list.title or Pages[page_name].title
  document:get("title"):set_text(t(title))
  for slot = 1, PageSize do
    local entry = entry_of(slot)
    local element = document:get("entry-" .. slot)
    element:set_class("hidden", entry == nil)
    if entry ~= nil then
      element:set_text(label_of(entry))
    end
  end
  local paged = list ~= nil and pages() > 1
  document:get("page"):set_class("hidden", not paged)
  document:get("page"):set_text(t("shop.page", { page = list_page, pages = pages() }))
  document:get("previous"):set_class("hidden", not paged)
  document:get("next-page"):set_class("hidden", not paged)
  document:get("amount"):set_class("hidden", page_name ~= "items")
  document:get("amount"):set_text(t("dev_menu.amount", { amount = amount }))
  document:get("back"):set_text(t(page_name == "main" and "dev_menu.close" or "common.back"))
end

local function show_page(document, name)
  page_name = name
  list_page = 1
  refresh(document)
  document:get(entry_of(1) ~= nil and "entry-1" or "back"):focus()
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

function choose(document, slot)
  local entry = entry_of(slot)
  if entry == nil then
    return
  end
  if entry.page ~= nil then
    show_page(document, entry.page)
    return
  end
  if entry.send ~= nil then
    ui.send("dev_menu_close", true)
    ui.send(entry.send, true)
    return
  end
  run(entry.line ~= nil and { entry.line } or entry.lines)
  document:get("done"):set_text(t("dev_menu.done", { what = label_of(entry) }))
end

function next_amount(document)
  local next = Amounts[1]
  for i, value in ipairs(Amounts) do
    if value == amount then
      next = Amounts[i % #Amounts + 1]
    end
  end
  amount = next
  refresh(document)
end

function turn(document, step)
  list_page = ((list_page - 1 + step) % pages()) + 1
  refresh(document)
  document:get("entry-1"):focus()
end

function back(document)
  if page_name == "main" then
    ui.send("dev_menu_close", true)
  else
    show_page(document, "main")
  end
end

function on_show(document)
  document:get("done"):set_text("")
  show_page(document, "main")
  refresh_prompts(document)
end

function on_language(document)
  refresh(document)
  refresh_prompts(document)
end

function on_device(document)
  refresh_prompts(document)
end

-- Cancel goes back a page (and closes from the first); left and right turn a list's pages.
function on_action(document, action)
  if action == "cancel" or action == "menu" then
    back(document)
    return true
  end
  if Lists[page_name] ~= nil and (action == "left" or action == "right") and pages() > 1 then
    turn(document, action == "left" and -1 or 1)
    return true
  end
  return false
end
