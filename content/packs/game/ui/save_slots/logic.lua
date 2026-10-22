-- The title screen's slot list (docs/architecture/guardar-y-cargar.md, 3.4 and 3.5): the empty
-- slots for a new game, or the saves to continue. The game publishes the slots as settings
-- (save.slots.mode, "new" or "continue"; save.slots, the count; save.slot.<n>.kind, number, state,
-- species, species_key, map, map_key, play_seconds and updated, from 0) and shows this document; it
-- sends choose_slot with the entry, from 0, play_without_saving or slots_back. Pages of six
-- entries, turned with left and right or the page buttons. The page and the row chosen stay while a
-- dialog about it is open, and start over when the list is opened from the main menu.

local PageSize = 6
local page = 1
local chosen = nil

local function count()
  return math.floor(ui.setting("save.slots") or 0)
end

local function new_game()
  return ui.setting("save.slots.mode") == "new"
end

local function pages()
  return math.max(1, math.ceil(count() / PageSize))
end

-- The entry of a row of the current page, from 0, or nil when the row is empty.
local function entry_of(row)
  local entry = (page - 1) * PageSize + row - 1
  if entry < count() then
    return entry
  end
  return nil
end

local function field(entry, name)
  return ui.setting("save.slot." .. entry .. "." .. name) or ""
end

-- A name the packs know by its key, else its ID.
local function named(entry, name)
  local key = field(entry, name .. "_key")
  return key ~= "" and t(key) or field(entry, name)
end

local function played(entry)
  local seconds = math.floor(tonumber(field(entry, "play_seconds")) or 0)
  return string.format("%d:%02d", seconds // 3600, (seconds // 60) % 60)
end

local function label_of(entry)
  local number = math.floor(tonumber(field(entry, "number")) or 0)
  local kind = field(entry, "kind") == "autosave" and "save.slots.autosave" or "save.slots.slot"
  local parts = { t(kind, { number = number }) }
  local state = field(entry, "state")
  if state == "saved" then
    for _, part in ipairs({
      named(entry, "species"),
      named(entry, "map"),
      played(entry),
      field(entry, "updated"),
    }) do
      if part ~= "" then
        table.insert(parts, part)
      end
    end
  else
    table.insert(parts, t("save.slots." .. state))
  end
  return table.concat(parts, " · ")
end

local function refresh(document)
  page = math.min(page, pages())
  document
    :get("title")
    :set_text(t(new_game() and "save.slots.title_new" or "save.slots.title_continue"))
  document:get("full"):set_class("hidden", not (new_game() and count() == 0))
  document:get("no-save"):set_class("hidden", not new_game())
  for row = 1, PageSize do
    local entry = entry_of(row)
    local button = document:get("slot-" .. row)
    button:set_class("hidden", entry == nil)
    if entry ~= nil then
      button:set_text(label_of(entry))
    end
  end
  local several = pages() > 1
  document:get("previous"):set_class("hidden", not several)
  document:get("next"):set_class("hidden", not several)
  document:get("page"):set_class("hidden", not several)
  document:get("page"):set_text(t("exploration.page", { page = page, pages = pages() }))
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
  document:get("prompt-cancel"):set_text(ui.prompt("cancel"))
end

local function focus_first(document)
  document:get(count() > 0 and "slot-1" or (new_game() and "no-save" or "back")):focus()
end

local function focus_chosen(document)
  if chosen ~= nil and entry_of(chosen) ~= nil then
    document:get("slot-" .. chosen):focus()
  else
    focus_first(document)
  end
end

function choose(row)
  local entry = entry_of(row)
  if entry ~= nil then
    chosen = row
    ui.send("choose_slot", entry)
  end
end

function play_without_saving()
  chosen = nil
  ui.send("play_without_saving", true)
end

function back()
  page = 1
  chosen = nil
  ui.send("slots_back", true)
end

function turn(document, step)
  page = ((page - 1 + step) % pages()) + 1
  refresh(document)
  document:get("slot-1"):focus()
end

function on_show(document)
  refresh(document)
  refresh_prompts(document)
  focus_chosen(document)
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
  refresh_prompts(document)
end

function on_device(document)
  refresh_prompts(document)
end

-- Cancel and menu go back to the main menu; left and right turn the page. Down from back wraps to
-- the first entry shown, which is not slot-1 when no slot is listed.
function on_action(document, action)
  if action == "cancel" or action == "menu" then
    back()
    return true
  end
  if action == "down" and count() == 0 and document:get("back"):has_focus() then
    focus_first(document)
    return true
  end
  if (action == "left" or action == "right") and pages() > 1 then
    turn(document, action == "left" and -1 or 1)
    return true
  end
  return false
end
