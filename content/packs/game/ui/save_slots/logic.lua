-- The title screen's slot list (docs/architecture/guardar-y-cargar.md, 3.4 and 3.5): the empty
-- slots for a new game, or the saves to continue. The game publishes the slots as settings
-- (save.slots.mode, "new" or "continue"; save.slots, the count; save.slot.<n>.kind, number, state,
-- nickname, species, species_key, map, map_key, play_seconds and updated, from 0) and shows this
-- document; it
-- sends choose_slot with the entry, from 0, play_without_saving or slots_back. Pages of six
-- entries, turned with left and right or the page buttons. The page and the row chosen stay while a
-- dialog about it is open, and start over when the list is opened from the main menu.
--
-- The original look (the skin marks #panel with slots-original) lists every slot instead, one row
-- each, as the original lists its memory card slots (main_menu.c: renderSelectCardSlot): "CONTINUE
-- SLOT 1", "ERASE AUTOSAVE 2"... (the game publishes save.slots.manual and save.slots.autosave, the
-- slots of each kind), greyed when the slot has nothing to choose. Eight rows show at a time and
-- the list scrolls, as the original's save list does (renderSelectSlot), with its cursor stopping
-- at the first and last row (tickMenuInput); left and right move a whole box, an addition. A box
-- below the rows sums up the focused save (partner, place, play time and date), an addition: the
-- original shows a save's details only after its card is chosen.

local PageSize = 6
local page = 1
local chosen = nil

local Visible = 8
local top = 0
local rows = {}
local chosen_item = nil
local detail_key = nil

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

-- The partner's own name and its species (main_menu.c: drawMainMenuStrings), or the species.
local function partner_of(entry)
  local species = named(entry, "species")
  local nickname = field(entry, "nickname")
  if nickname == "" or nickname == species then
    return species
  end
  return nickname .. " (" .. species .. ")"
end

local function played(entry)
  local seconds = math.floor(tonumber(field(entry, "play_seconds")) or 0)
  return string.format("%d:%02d", seconds // 3600, (seconds // 60) % 60)
end

local function original(document)
  return document:get("panel"):has_class("slots-original")
end

local function mode()
  return ui.setting("save.slots.mode") or "continue"
end

-- Every slot the original look lists, with its entry when the list has one.
local function build_rows()
  rows = {}
  local kinds = mode() == "new" and { "slot" } or { "slot", "autosave" }
  local counts = {
    slot = math.floor(ui.setting("save.slots.manual") or 0),
    autosave = math.floor(ui.setting("save.slots.autosave") or 0),
  }
  for _, kind in ipairs(kinds) do
    for number = 1, counts[kind] do
      local found = nil
      for entry = 0, count() - 1 do
        if
          field(entry, "kind") == kind
          and math.floor(tonumber(field(entry, "number")) or 0) == number
        then
          found = entry
        end
      end
      table.insert(rows, { kind = kind, number = number, entry = found })
    end
  end
end

-- A row with nothing to choose: no entry, or a newer game's save that cannot be continued.
local function dimmed(row)
  return row.entry == nil or (mode() == "continue" and field(row.entry, "state") == "newer")
end

local function row_label(row)
  local key = "save.slots.original.new_slot"
  if mode() ~= "new" then
    key = "save.slots.original."
      .. mode()
      .. "_"
      .. (row.kind == "autosave" and "autosave" or "slot")
  end
  return t(key, { number = row.number })
end

-- How many entries the cursor goes through: the rows, and play without saving for a new game.
local function items()
  return #rows + (mode() == "new" and 1 or 0)
end

local function label_of(entry)
  local number = math.floor(tonumber(field(entry, "number")) or 0)
  local kind = field(entry, "kind") == "autosave" and "save.slots.autosave" or "save.slots.slot"
  local parts = { t(kind, { number = number }) }
  local state = field(entry, "state")
  if state == "saved" then
    for _, part in ipairs({
      partner_of(entry),
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

local function refresh_original(document)
  build_rows()
  top = math.max(0, math.min(top, #rows - Visible))
  for _, id in ipairs({ "title", "full", "page", "previous", "next", "back" }) do
    document:get(id):set_class("hidden", true)
  end
  document:get("no-save"):set_class("hidden", mode() ~= "new")
  document:get("no-save"):set_text(t("save.slots.original.no_save"))
  for slot = 1, Visible do
    local row = rows[top + slot]
    local button = document:get("slot-" .. slot)
    button:set_class("hidden", row == nil)
    if row ~= nil then
      button:set_text(row_label(row))
      button:set_class("dimmed", dimmed(row))
    end
  end
  document:get("more-up"):set_class("hidden", top == 0)
  document:get("more-down"):set_class("hidden", top + Visible >= #rows)
  detail_key = nil
end

-- The item focused, from 1: a row (top + its button) or play without saving after the rows.
local function current_item(document)
  for slot = 1, Visible do
    if document:get("slot-" .. slot):has_focus() then
      return top + slot
    end
  end
  if document:get("no-save"):has_focus() then
    return #rows + 1
  end
  return nil
end

-- Focuses an item, scrolling the rows so that it shows.
local function focus_item(document, item)
  if item > #rows then
    document:get("no-save"):focus()
    return
  end
  if item <= top then
    top = item - 1
  elseif item > top + Visible then
    top = item - Visible
  end
  refresh_original(document)
  document:get("slot-" .. (item - top)):focus()
end

-- The first row with something to choose, else the first item.
local function first_item()
  for index, row in ipairs(rows) do
    if not dimmed(row) then
      return index
    end
  end
  return 1
end

-- The box below the rows: what the focused row holds.
local function refresh_detail(document)
  local item = current_item(document)
  local key = tostring(item) .. ":" .. top
  if key == detail_key then
    return
  end
  detail_key = key
  local row = item ~= nil and rows[item] or nil
  local lines = { "", "" }
  if row ~= nil and row.entry ~= nil then
    local state = field(row.entry, "state")
    if state == "saved" then
      local place = { named(row.entry, "map"), played(row.entry), field(row.entry, "updated") }
      local parts = {}
      for _, part in ipairs(place) do
        if part ~= "" then
          table.insert(parts, part)
        end
      end
      lines = { partner_of(row.entry), table.concat(parts, "  ") }
    elseif state == "empty" then
      lines[1] = t("save.slots.empty")
    else
      lines[1] = t("save.slots." .. state)
    end
  elseif row ~= nil then
    lines[1] = t(mode() == "new" and "save.slots.original.in_use" or "save.slots.empty")
  end
  document:get("detail"):set_class("hidden", row == nil)
  document:get("detail-line-1-text"):set_text(lines[1])
  -- A long name may make the line wider than the box (common/fit_text.lua).
  fit_text.fit(document:get("detail-line-1"), { document:get("detail-line-1-text") })
  document:get("detail-line-2"):set_text(lines[2])
  document:get("detail-line-2"):set_class("hidden", lines[2] == "")
end

local function refresh(document)
  if original(document) then
    refresh_original(document)
    return
  end
  for _, id in ipairs({ "title", "back", "slot-7", "slot-8", "more-up", "more-down", "detail" }) do
    document:get(id):set_class("hidden", id ~= "title" and id ~= "back")
  end
  document:get("no-save"):set_text(t("save.slots.no_save"))
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
  if original(document) then
    focus_item(document, chosen_item or first_item())
    return
  end
  if chosen ~= nil and entry_of(chosen) ~= nil then
    document:get("slot-" .. chosen):focus()
  else
    focus_first(document)
  end
end

function choose(document, row)
  if original(document) then
    local item = rows[top + row]
    if item ~= nil and not dimmed(item) then
      chosen_item = top + row
      ui.send("choose_slot", item.entry)
    end
    return
  end
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
  top = 0
  chosen_item = nil
  ui.send("slots_back", true)
end

function turn(document, step)
  page = ((page - 1 + step) % pages()) + 1
  refresh(document)
  document:get("slot-1"):focus()
end

-- A click on the marks above or below the rows: the rows a box further, the cursor on the same row
-- of the box.
function scroll_rows(document, step)
  local slot = (current_item(document) or (top + 1)) - top
  top = math.max(0, math.min(top + step * Visible, #rows - Visible))
  focus_item(document, math.min(#rows, top + math.max(1, math.min(slot, Visible))))
end

function on_tick(document, _seconds)
  if original(document) then
    refresh_detail(document)
  end
  fit_text.tick()
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
  if original(document) then
    local item = current_item(document)
    local step = ({ up = -1, down = 1, left = -Visible, right = Visible })[action]
    if step == nil or item == nil then
      return false
    end
    local next = math.max(1, math.min(items(), item + step))
    if next == item then
      -- The cursor stops at the ends: nothing moves, so nothing sounds.
      return false
    end
    focus_item(document, next)
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
