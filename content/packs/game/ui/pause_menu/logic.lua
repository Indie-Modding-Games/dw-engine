-- The pause menu: main, player, minimap and options. The menu never changes a setting itself: it
-- sends a command to the game with ui.send and shows what the game reports back through ui.setting,
-- so the labels always tell the truth. Every text the player sees comes from the texts in the
-- player's language: t(key) here, text-key in the document.

local pages = { "main", "minimap", "options", "player" }
local first_entry = {
  main = "continue",
  minimap = "minimap-mode",
  options = "display-mode",
  player = "player-city",
}
-- The entry of the main page that opened each page, focused again when going back.
local opener = { minimap = "open-minimap", options = "open-options", player = "open-player" }

local current = "main"
-- Whether F10 opened the menu on its options page, which then goes back straight to the game.
local options_direct = false
local parents = {}
local icon_time = 0
local icon_flash = false
local player_tools_open = false
local player_tab = "player"
local chart_selecting = false
local chart_detail = false
local chart_transition = nil
local medals_selecting = false
local medal_index = 0
local cards_selecting = false
local card_index = 0
local card_open = false
local card_palettes = {}
local chart_index = 0
local chart_cells = {}
local chart_signature = ""
local chart_flash = false
-- The chart's pages (docs/modulos/apps/game.md, «Etapas canónicas»): more than one when a box has
-- more cells than its grid holds; the page shown, the slide between two and the cell a page turn
-- left (chart_turn).
local chart_pages = 1
local chart_page = 1
local chart_slide = nil
local chart_return = nil
local city_selecting = false
local city_index = 0
local city_cells = {}
local city_signature = ""
local city_cursor_cell = nil

local minimap_sizes = { "small", "medium", "large" }
-- Whether the HUD shows the minimap: as the skin decides (auto), always (on) or never (off).
local minimap_modes = { "auto", "on", "off" }
local ui_scales = { 0.75, 0.9, 1.0, 1.15, 1.3, 1.5, 1.75, 2.0 }

local function on_off(value)
  return t(value and "common.yes" or "common.no")
end

-- A list the game publishes as "0,2,1": the value of each position, from 0.
local function number_list(name)
  local values = {}
  local position = 0
  for value in string.gmatch(ui.setting(name) or "", "%d+") do
    values[position] = tonumber(value)
    position = position + 1
  end
  return values
end

local function index_of(list, value)
  for index, item in ipairs(list) do
    if item == value then
      return index
    end
  end
  return nil
end

-- The nearest UI scale step to a value, so a scale set elsewhere still cycles from the right place.
local function nearest_scale(value)
  local best = 3
  for index, scale in ipairs(ui_scales) do
    if math.abs(scale - value) < math.abs(ui_scales[best] - value) then
      best = index
    end
  end
  return best
end

local function cycle(list, index, step)
  return ((index - 1 + step) % #list) + 1
end

-- What each option entry does when confirmed or moved left and right.
local options = {
  ["minimap-mode"] = function(step)
    local index = index_of(minimap_modes, ui.setting("minimap")) or 1
    ui.send("minimap", minimap_modes[cycle(minimap_modes, index, step)])
  end,
  ["minimap-size"] = function(step)
    local index = index_of(minimap_sizes, ui.setting("minimap_size")) or 2
    ui.send("minimap_size", minimap_sizes[cycle(minimap_sizes, index, step)])
  end,
  ["display-mode"] = function(_step)
    ui.send("fullscreen", not ui.setting("fullscreen"))
  end,
  ["ui-scale"] = function(step)
    local index = nearest_scale(ui.setting("ui_scale") or 1.0)
    ui.send("ui_scale", ui_scales[math.max(1, math.min(#ui_scales, index + step))])
  end,
  ["vsync"] = function(_step)
    ui.send("vsync", not ui.setting("vsync"))
  end,
  ["ui-skin"] = function(step)
    skin_choice.change(step)
  end,
  -- The language and the original-looking text, as on the title's options (main_menu).
  ["language"] = function(step)
    local languages = ui.languages()
    if #languages > 0 then
      local index = index_of(languages, ui.setting("language")) or 1
      ui.send("language", languages[cycle(languages, index, step)])
    end
  end,
  ["text-font"] = function(_step)
    ui.send("text_font", ui.setting("text_font") == "original" and "modern" or "original")
  end,
  -- The game's own options are not here: game_options (ui/common/game_options.lua) lists them and
  -- the option rows below the display ones show them (bind_option_rows).
}

local function can_sleep()
  return ui.setting("pause_menu.sleep_enabled") == true
end

-- main/fish.c: getFishingAvailability; 0 off a fishing map, 1 without a rod or bait, 2 ready.
local function fishing()
  return math.floor(ui.setting("pause_menu.fishing") or 0)
end

local function draw_date_digits(document, prefix, count, value)
  local text = tostring(math.max(0, math.floor(tonumber(value) or 0)))
  for index = 1, count do
    local digit = text:sub(index, index)
    local element = document:get(prefix .. index)
    element:set_class("hidden", digit == "")
    element:set_text(digit)
    for number = 0, 9 do
      element:set_class("digit-" .. number, digit == tostring(number))
    end
  end
end

-- The chart's page height (dp): the pages sit one under the other in #chart-pages, which slides up
-- a page to show the next.
local CHART_PAGE_HEIGHT = 564

-- Where a chart cell's box goes (dp, in #chart-pages): the cell's own place on its page.
local function chart_top(cell, offset)
  return ((cell.y - offset) * 3 + (cell.page - 1) * CHART_PAGE_HEIGHT) .. "dp"
end

local function chart_cursor(document)
  local cell = chart_cells[chart_index + 1]
  document:get("chart-cursor"):set_class("hidden", not chart_selecting or cell == nil)
  if not cell then
    return
  end
  local cursor = document:get("chart-cursor")
  cursor:set_property("left", ((cell.x - 15) * 3) .. "dp")
  cursor:set_property("top", chart_top(cell, 33))
end

-- The chart's cells: the original's 61, and the ones a ruleset's patches add (the hidden content).
local ChartCells = 72

local function chart_rows(first, count, step)
  local rows = {}
  for n = 1, count do
    rows[n] = first + (n - 1) * step
  end
  return rows
end

-- The chart's stage boxes (docs/modulos/apps/game.md, «Etapas canónicas»), the columns and rows of
-- their cells (the chart's pixels): the original's grid, Fresh and In-Training four cells 38 apart,
-- Rookie nine 19 apart, Champion four columns and Ultimate two of eight. A page holds a box's grid
-- at most; the cells past it go on to the next page, in the same box.
local BOX_ROWS = chart_rows(43, 8, 19)
local CHART_BOXES = {
  { columns = { 30 }, rows = chart_rows(43, 4, 38) },
  { columns = { 67 }, rows = chart_rows(43, 4, 38) },
  { columns = { 104 }, rows = chart_rows(43, 9, 19) },
  { columns = { 141, 165, 189, 213 }, rows = BOX_ROWS },
  { columns = { 250, 274 }, rows = BOX_ROWS },
  { columns = { 297 }, rows = BOX_ROWS },
}
local MEGA_BOX = 6
-- With the Mega box, a sixth box right of the Ultimate one as tall, the Champion and Ultimate
-- columns stand 22 pixels apart instead of 24, so the six fit the panel's frame.
local MEGA_COLUMNS = { [4] = { 140, 162, 184, 206 }, [5] = { 241, 263 } }
-- The canonical stages (an engine improvement, docs/modulos/apps/game.md, «Etapas canónicas»; the
-- option canonical_stages, off by default): the species the series calls Mega leave the Ultimate
-- box for the Mega box, in this order, as the Mega step's Megas (player.chart.mega). The rules
-- still count them as Ultimates.
local MEGA_ORDER = { houo = 1, hkab = 2, mtet = 3, muge = 4 }
local STAGE_KEYS = { "fresh", "in_training", "rookie", "champion", "ultimate" }

local function canonical_stages()
  return ui.setting("canonical_stages") == true
end

-- The stage label of a chart cell: the original's level names, or the canonical ones.
local function stage_label(base)
  local level = math.floor(ui.setting(base .. "level") or 0)
  if not canonical_stages() then
    return t("pause_menu.chart_level." .. tostring(level))
  end
  if MEGA_ORDER[ui.setting(base .. "id") or ""] then
    return t("pause_menu.chart_stage.mega")
  end
  local key = STAGE_KEYS[level]
  return key and t("pause_menu.chart_stage." .. key) or ""
end

-- A species' place in the Mega box (its order), or nil when it is not a Mega there.
local function mega_rank(id)
  if canonical_stages() and MEGA_ORDER[id] then
    return MEGA_ORDER[id]
  end
  for mega in string.gmatch(ui.setting("player.chart.mega") or "", "[^,]+") do
    if mega == id then
      return MEGA_ORDER[id] or 100 -- after the series' Megas
    end
  end
  return nil
end

-- The box of a cell the game puts at x: the one whose columns start at x or left of it.
local function chart_box_of(x)
  local box = 1
  for n = 1, MEGA_BOX - 1 do
    if x >= CHART_BOXES[n].columns[1] then
      box = n
    end
  end
  return box
end

-- Where each cell goes ({box, column, row, page}) and whether the Mega box is drawn. A cell at a
-- free place of its box's grid keeps it on page 1, so the original's cells stay where the original
-- draws them; the others (a Mega, a ruleset's cell past the grid or on a taken place) take their
-- box's free places in order, down each column and then the next, page 1 first.
local function chart_places(count)
  local places = {}
  local taken = {}
  local waiting = {}
  for box = 1, MEGA_BOX do
    taken[box] = {}
    waiting[box] = {}
  end
  local function place_key(page, column, row)
    return string.format("%d:%d:%d", page, column, row)
  end
  local megas = {}
  for i = 0, count - 1 do
    local base = string.format("player.chart.%d.", i)
    local rank = mega_rank(ui.setting(base .. "id") or "")
    if rank then
      megas[#megas + 1] = { index = i, rank = rank }
    else
      local box = chart_box_of(ui.setting(base .. "x"))
      local column = index_of(CHART_BOXES[box].columns, ui.setting(base .. "x"))
      local row = index_of(CHART_BOXES[box].rows, ui.setting(base .. "y"))
      if column and row and not taken[box][place_key(1, column, row)] then
        taken[box][place_key(1, column, row)] = true
        places[i] = { box = box, column = column, row = row, page = 1 }
      else
        waiting[box][#waiting[box] + 1] = i
      end
    end
  end
  table.sort(megas, function(a, b)
    return a.rank == b.rank and a.index < b.index or a.rank < b.rank
  end)
  for _, mega in ipairs(megas) do
    waiting[MEGA_BOX][#waiting[MEGA_BOX] + 1] = mega.index
  end
  for box = 1, MEGA_BOX do
    local columns, rows = #CHART_BOXES[box].columns, #CHART_BOXES[box].rows
    local page, column, row = 1, 1, 1
    for _, i in ipairs(waiting[box]) do
      while taken[box][place_key(page, column, row)] do
        row = row + 1
        if row > rows then
          row, column = 1, column + 1
          if column > columns then
            column, page = 1, page + 1
          end
        end
      end
      taken[box][place_key(page, column, row)] = true
      places[i] = { box = box, column = column, row = row, page = page }
    end
  end
  return places, #megas > 0
end

-- The cell of a page in column x, or else in the column nearest it (the left one on a tie): its
-- top cell, or its bottom one; nil on a page without cells.
local function chart_column_cell(page, x, bottom)
  local column = nil
  for _, cell in ipairs(chart_cells) do
    if cell.page == page then
      local distance = math.abs(cell.x - x)
      local best = column and math.abs(column - x)
      if best == nil or distance < best or (distance == best and cell.x < column) then
        column = cell.x
      end
    end
  end
  local found = nil
  for i, cell in ipairs(chart_cells) do
    if cell.page == page and cell.x == column then
      local best = found and chart_cells[found + 1]
      if best == nil or (bottom and cell.y > best.y) or (not bottom and cell.y < best.y) then
        found = i - 1
      end
    end
  end
  return found
end

-- The cursor's cell on a page: the cell it is on, or the top of its column there.
local function chart_index_on(page)
  local cell = chart_cells[chart_index + 1]
  if cell and cell.page == page then
    return chart_index
  end
  return chart_column_cell(page, cell and cell.x or 0, false) or 0
end

-- Draws #chart-pages slid up by offset (dp): the boxes of the page shown and, while it slides, of
-- the page it leaves (other), each with its cells. The cells of the other pages are hidden, so the
-- pointer does not hit them past the chart's edge (RmlUi clips drawing only). The arrows and the
-- page number stay in place under the chart.
local function chart_page_view(document, offset, other)
  document:get("chart-pages"):set_property("top", -offset .. "dp")
  for i = 0, MEGA_BOX - 1 do
    document
      :get("chart-stage-" .. i)
      :set_property("top", ((chart_page - 1) * CHART_PAGE_HEIGHT + 21) .. "dp")
    local leaving = document:get("chart-stage-other-" .. i)
    leaving:set_class("off-page", other == nil)
    if other then
      leaving:set_property("top", ((other - 1) * CHART_PAGE_HEIGHT + 21) .. "dp")
    end
  end
  for i, cell in ipairs(chart_cells) do
    document
      :get("chart-cell-" .. (i - 1))
      :set_class("off-page", cell.page ~= chart_page and cell.page ~= other)
  end
  document:get("chart-less"):set_class("off-page", chart_page <= 1)
  document:get("chart-more"):set_class("off-page", chart_page >= chart_pages)
  document:get("chart-page"):set_text(string.format("%d/%d", chart_page, chart_pages))
end

-- Shows a page of the chart, sliding to it (0.2 seconds; at 20 steps a second as the original's
-- boxes unless smooth motion is on), the cursor on index there. instant skips the slide.
local function chart_go_page(document, page, index, instant)
  if page < 1 or page > chart_pages then
    return false
  end
  local from = chart_page
  chart_page = page
  if index then
    chart_index = index
  end
  if instant or from == page then
    chart_slide = nil
    chart_page_view(document, (page - 1) * CHART_PAGE_HEIGHT, nil)
  else
    chart_slide = { from = from, elapsed = 0 }
    chart_page_view(document, (from - 1) * CHART_PAGE_HEIGHT, from)
  end
  chart_cursor(document)
  if chart_selecting then
    document:get("chart-cell-" .. chart_index):focus()
  end
  return true
end

-- Moves the slide on; true while it runs.
local function chart_slide_tick(document, seconds)
  if not chart_slide then
    return false
  end
  chart_slide.elapsed = chart_slide.elapsed + seconds
  local progress = chart_slide.elapsed / 0.2
  if ui.setting("smooth_motion") ~= true then
    progress = math.floor(chart_slide.elapsed * 20 + 0.000001) / 4
  end
  if progress >= 1 then
    chart_slide = nil
    chart_page_view(document, (chart_page - 1) * CHART_PAGE_HEIGHT, nil)
    return false
  end
  local from = chart_slide.from
  local offset = ((from - 1) + (chart_page - from) * progress) * CHART_PAGE_HEIGHT
  chart_page_view(document, math.floor(offset + 0.5), from)
  return true
end

-- Turns to the next or the previous page, the cursor in the column it is in: on its top cell going
-- down, its bottom one going up, or back on the cell it left when it turns straight back.
local function chart_turn(document, page)
  if page < 1 or page > chart_pages or page == chart_page then
    return false
  end
  local index
  if chart_return and chart_return.page == page and chart_return.landed == chart_index then
    index = chart_return.index
  else
    local cell = chart_cells[chart_index + 1]
    index = chart_column_cell(page, cell and cell.x or 0, page < chart_page)
  end
  if index == nil then
    return false
  end
  chart_return = { page = chart_page, index = chart_index, landed = index }
  return chart_go_page(document, page, index)
end

local function refresh_chart(document)
  local count = math.min(ChartCells, tonumber(ui.setting("player.chart.count")) or 0)
  local raised_text = ui.setting("player.chart.raised") or ""
  local canonical = canonical_stages()
  local signature = table.concat({
    tostring(count),
    raised_text,
    tostring(canonical),
    ui.setting("player.chart.mega") or "",
  }, ":")
  if signature == chart_signature then
    return
  end
  chart_signature = signature
  local raised = {}
  for index in string.gmatch(raised_text, "%d+") do
    raised[tonumber(index)] = true
  end
  chart_cells = {}
  local places, mega = chart_places(count)
  -- The Mega box makes the panel as wide as the original's screen; more pages than one draw the
  -- arrows and the page number under the chart.
  chart_pages = 1
  for _, place in pairs(places) do
    chart_pages = math.max(chart_pages, place.page)
  end
  document:get("panel"):set_class("chart-wide", mega)
  document:get("player-chart"):set_class("chart-mega", mega)
  document:get("player-chart"):set_class("chart-paged", chart_pages > 1)
  for i = 0, ChartCells - 1 do
    local button = document:get("chart-cell-" .. i)
    button:set_class("hidden", i >= count)
    if i < count then
      local base = string.format("player.chart.%d.", i)
      local place = places[i]
      local box = CHART_BOXES[place.box]
      local columns = mega and MEGA_COLUMNS[place.box] or box.columns
      local cell = {
        x = columns[place.column],
        y = box.rows[place.row],
        page = place.page,
        u = ui.setting(base .. "u"),
        v = ui.setting(base .. "v"),
        raised = raised[ui.setting(base .. "index")] == true,
        species = math.tointeger(math.floor(tonumber(ui.setting(base .. "index")) or -1)),
      }
      chart_cells[i + 1] = cell
      button:set_property("left", ((cell.x - 12) * 3) .. "dp")
      button:set_property("top", chart_top(cell, 33))
      local icon = document:get("chart-icon-" .. i)
      icon:set_class("hidden", not cell.raised)
      if cell.raised then
        icon:set_attribute("src", "/" .. ui.setting(base .. "source"))
        icon:set_attribute("rect", string.format("%d %d 16 16", cell.u, cell.v))
      end
    end
  end
  -- The cursor stays on the page shown while it still has cells.
  chart_page = math.min(chart_page, chart_pages)
  chart_slide = nil
  chart_return = nil
  chart_index = chart_index_on(chart_page)
  chart_page_view(document, (chart_page - 1) * CHART_PAGE_HEIGHT, nil)
  chart_cursor(document)
end

local function detail_value(group, row, column)
  return tonumber(ui.setting(string.format("player.chart.detail.%s.%d.%d", group, row, column)))
    or 0
end

local function transition_box(document, frame)
  local cell = chart_cells[chart_index + 1]
  local start_x, start_y = cell.x + 5, cell.y + 8
  local function interpolate(start, finish)
    local delta = (finish - start) * frame / 4
    return start + (delta < 0 and math.ceil(delta) or math.floor(delta))
  end
  local x = interpolate(start_x, 10)
  local y = interpolate(start_y, 31)
  local right = interpolate(start_x + 10, 310)
  local bottom = interpolate(start_y + 10, 221)
  local box = document:get("chart-detail-transition")
  box:set_property("left", ((x - 11) * 3) .. "dp")
  box:set_property("top", ((y - 32) * 3) .. "dp")
  box:set_property("width", ((right - x + 1) * 3) .. "dp")
  box:set_property("height", ((bottom - y + 1) * 3) .. "dp")
end

local function begin_chart_transition(document, closing)
  chart_transition = { closing = closing, elapsed = 0 }
  document:get("panel"):set_class("chart-detail-open", false)
  document:get("chart-detail-transition"):set_class("hidden", false)
  transition_box(document, closing and 4 or 0)
end

-- Evolution requirements (an engine improvement, docs/modulos/apps/game.md, «Requisitos de
-- evolución»; the option evolution_requirements, on by default): in the chart's tree a cursor goes
-- over the targets and a panel over the left box tells what the target asks. On the partner's own
-- tree it also tells the partner's values and verdicts, the targets it qualifies for glow green and
-- the rest dim. Every verdict comes from the game (evoreq.*); the menu only lays them out.
local detail_targets = {}
local detail_from = { cells = {}, lines = {} }
local detail_focus = nil
local detail_last_focus = 1
local REQ_ROWS = 15
-- The partner's next form or fallback when the player has not discovered it
-- (chart_requirements.hpp).
local UNDISCOVERED = -2
local GLOW = { "#84f084", "#2c9a40" }
local DIM = { "#5a5a5a", "#3a3a3a" }
local GROUP_KEYS = { care = true, weight = true, stats = true, bonus = true, gate = true }
local STAT_KEYS = { hp = true, mp = true, off = true, def = true, spd = true, brn = true }

local function requirements_on()
  return ui.setting("evolution_requirements") ~= false and ui.setting("evoreq.ready") == true
end

local function numbers(text)
  local values = {}
  for value in string.gmatch(text or "", "-?%d+") do
    values[#values + 1] = tonumber(value)
  end
  return values
end

local function split_rows(text)
  local parts = {}
  for part in string.gmatch(text or "", "[^|]+") do
    parts[#parts + 1] = part
  end
  return parts
end

-- The partner's line: its form, counter, the level's hours, whether the check is due, what it
-- would pick and the rookie's fallback; nil when the game publishes none.
local function partner_outlook()
  local values = numbers(ui.setting("evoreq.partner"))
  if #values < 7 then
    return nil
  end
  return {
    species = values[1],
    timer = values[2],
    hours = values[3],
    due = values[4] == 1,
    next = values[5],
    fallback = values[6],
    fallback_hours = values[7],
  }
end

-- The species of a chart cell, as a whole number (the settings are numbers with a fraction).
local function chart_species(index)
  local species = tonumber(ui.setting(string.format("player.chart.%d.index", index)))
  return species and math.tointeger(math.floor(species)) or nil
end

-- A form the player has discovered: the chart draws it with its picture. Any other form is "???"
-- and shows nothing of its requirement.
local function discovered(species)
  for _, cell in ipairs(chart_cells) do
    if cell.species == species then
      return cell.raised
    end
  end
  return false
end

local function species_name(species)
  for i, cell in ipairs(chart_cells) do
    if cell.species == species and cell.raised then
      return t(ui.setting(string.format("player.chart.%d.name", i - 1)) or "")
    end
  end
  return t("pause_menu.evoreq.unknown")
end

-- The partner's outlook when the open tree is its own form, or nil.
local function partner_tree()
  local outlook = partner_outlook()
  if outlook ~= nil and outlook.species == chart_species(chart_index) then
    return outlook
  end
  return nil
end

-- The partner's score of a target: points, points to pass, groups, then a value and verdict per
-- row; nil when the game publishes none.
local function partner_values(species)
  local parts = split_rows(ui.setting("evoreq.partner." .. species))
  if #parts == 0 then
    return nil
  end
  local score = numbers(parts[1])
  local rows = {}
  for i = 2, #parts do
    local value, met = parts[i]:match("^(.-):(%d)$")
    rows[#rows + 1] = { value = value or "", met = met == "1" }
  end
  return {
    points = score[1] or 0,
    groups = score[3] or 0,
    passes = (score[1] or 0) >= (score[2] or 1),
    rows = rows,
  }
end

local function paint_target(document, target, colors, dim)
  for index, slot in ipairs(target.lines) do
    local band = (index - 1) % 3
    local primary = colors and colors[1] or target.primary
    local edge = colors and colors[2] or target.edge
    document
      :get("chart-detail-line-" .. slot)
      :set_property("background-color", band == 1 and primary or edge)
  end
  document:get("chart-detail-cell-" .. target.cell_slot):set_class("chart-dim", dim)
end

-- One row of the panel: an optional tick or cross, the text and the partner's value.
local function req_row(document, row, text, value, state, item)
  local element = document:get("chart-req-row-" .. row)
  element:set_class("hidden", false)
  element:set_class("item", item == true)
  element:set_class("title", state == "title")
  local mark = document:get("chart-req-mark-" .. row)
  mark:set_class("hidden", state ~= "met" and state ~= "unmet")
  mark:set_class("met", state == "met")
  mark:set_class("unmet", state == "unmet")
  document:get("chart-req-label-" .. row):set_text(text)
  local value_element = document:get("chart-req-value-" .. row)
  value_element:set_text(value or "")
  value_element:set_class("met", state == "met" or state == "value-met")
  value_element:set_class("unmet", state == "unmet" or state == "value-unmet")
end

local function verdict(partner, row, group)
  if partner == nil then
    return group and "plain" or nil
  end
  local met = row ~= nil and row.met
  if group then
    return met and "met" or "unmet"
  end
  return met and "value-met" or "value-unmet"
end

-- The panel for the focused target.
local function draw_requirements(document, outlook)
  for row = 0, REQ_ROWS - 1 do
    document:get("chart-req-row-" .. row):set_class("hidden", true)
  end
  local score = document:get("chart-req-score")
  local next_line = document:get("chart-req-next")
  score:set_text("")
  next_line:set_text("")
  local target = detail_targets[detail_focus]
  if not target then
    return
  end
  local source_level =
    math.floor(ui.setting(string.format("player.chart.%d.level", chart_index)) or 0)
  local hours = numbers(ui.setting("evoreq.hours"))[source_level]
  local partner = outlook and partner_values(target.species) or nil
  local row = 0
  req_row(document, row, species_name(target.species), "", "title")
  row = row + 1
  local known = discovered(target.species)
  local requirement = known and split_rows(ui.setting("evoreq.req." .. target.species)) or {}
  if not known then
    hours = nil
  elseif source_level == 1 then
    req_row(document, row, t("pause_menu.evoreq.always"), "", nil)
    row = row + 1
    requirement = {}
  elseif #requirement == 0 then
    requirement = { "special:" }
  end
  for index, entry in ipairs(requirement) do
    if row >= REQ_ROWS - 1 then
      break
    end
    local key, range = entry:match("^([^:]*):(.*)$")
    local values = partner and partner.rows[index] or nil
    local value = values and values.value or ""
    if key == "special" then
      req_row(document, row, t("pause_menu.evoreq.special"), "", nil)
    elseif GROUP_KEYS[key] then
      local text = t("pause_menu.evoreq." .. key)
      if key == "care" or key == "weight" then
        text = text .. " " .. range
      elseif key == "stats" and range == "top" then
        text = t("pause_menu.evoreq.stats_top")
        value = value ~= "" and t("pause_menu.evoreq.stat." .. value) or ""
      else
        value = ""
      end
      req_row(document, row, text, value, verdict(partner, values, true))
    elseif key == "from" then
      req_row(
        document,
        row,
        t("pause_menu.evoreq.from", { name = species_name(tonumber(range)) }),
        "",
        verdict(partner, values, false),
        true
      )
    else
      local name = STAT_KEYS[key] and t("pause_menu.evoreq.stat." .. key)
        or t("pause_menu.evoreq." .. key)
      req_row(
        document,
        row,
        range ~= "" and (name .. " " .. range) or name,
        value,
        verdict(partner, values, false),
        true
      )
    end
    row = row + 1
  end
  -- The hours the level checks from; the partner's counter next to them.
  if hours and row < REQ_ROWS then
    if outlook then
      req_row(
        document,
        row,
        t("pause_menu.evoreq.time"),
        t("pause_menu.evoreq.hours_of", { hours = outlook.timer, total = hours }),
        outlook.due and "value-met" or "value-unmet"
      )
    else
      req_row(
        document,
        row,
        t("pause_menu.evoreq.time"),
        t("pause_menu.evoreq.hours", { hours = hours }),
        nil
      )
    end
    row = row + 1
  end
  if outlook and outlook.fallback == target.species and row < REQ_ROWS then
    req_row(
      document,
      row,
      t("pause_menu.evoreq.fallback"),
      t("pause_menu.evoreq.hours_of", { hours = outlook.timer, total = outlook.fallback_hours }),
      outlook.timer >= outlook.fallback_hours and "value-met" or "value-unmet"
    )
  end
  if partner and known and source_level ~= 1 and requirement[1] ~= "special:" then
    score:set_text(t("pause_menu.evoreq.ready", { met = partner.points, total = partner.groups }))
    score:set_class("met", partner.passes)
    score:set_class("unmet", not partner.passes)
  end
  if outlook and outlook.next == target.species then
    next_line:set_text(t("pause_menu.evoreq.next"))
  elseif outlook and outlook.next == UNDISCOVERED then
    -- A form not discovered yet: only that the check picks one.
    next_line:set_text(t("pause_menu.evoreq.next_other", { name = species_name(outlook.next) }))
  elseif outlook and outlook.next >= 0 then
    -- The check would pick a form out of the tree: the rookie's fallback.
    local inside = false
    for _, other in ipairs(detail_targets) do
      inside = inside or other.species == outlook.next
    end
    if not inside then
      next_line:set_text(t("pause_menu.evoreq.next_other", { name = species_name(outlook.next) }))
    end
  end
end

-- Lays the requirements over the open tree, or the original tree when they are off.
local function refresh_requirements(document)
  if not chart_detail then
    return
  end
  local on = requirements_on() and #detail_targets > 0
  if not on then
    detail_focus = nil
  end
  local outlook = on and partner_tree() or nil
  for _, target in ipairs(detail_targets) do
    if outlook then
      local values = partner_values(target.species)
      local passes = (values ~= nil and values.passes) or outlook.next == target.species
      paint_target(document, target, passes and GLOW or DIM, not passes)
    else
      paint_target(document, target, nil, false)
    end
  end
  local focused = detail_focus ~= nil
  for _, slot in ipairs(detail_from.lines) do
    document:get("chart-detail-line-" .. slot):set_class("hidden", focused)
  end
  for _, slot in ipairs(detail_from.cells) do
    document:get("chart-detail-cell-" .. slot):set_class("hidden", focused)
  end
  document:get("chart-req"):set_class("hidden", not focused)
  local cursor = document:get("chart-detail-cursor")
  cursor:set_class("hidden", not focused)
  if focused then
    local target = detail_targets[detail_focus]
    cursor:set_property("left", ((target.x - 14) * 3) .. "dp")
    cursor:set_property("top", ((target.y - 32) * 3) .. "dp")
    draw_requirements(document, outlook)
  end
end

-- The cursor of the tree: up and down go over the targets, left leaves them for the original view
-- and right comes back.
local function detail_move(document, action)
  if not requirements_on() or #detail_targets == 0 then
    return
  end
  if detail_focus == nil then
    if action == "right" or action == "up" or action == "down" then
      detail_focus = math.min(detail_last_focus, #detail_targets)
    end
  elseif action == "left" then
    detail_last_focus = detail_focus
    detail_focus = nil
  elseif action == "up" then
    detail_focus = (detail_focus - 2) % #detail_targets + 1
  elseif action == "down" then
    detail_focus = detail_focus % #detail_targets + 1
  end
  refresh_requirements(document)
end

-- The first target the cursor takes: the one the partner's next check would pick, else the first.
local function first_focus()
  local outlook = partner_tree()
  if outlook then
    for index, target in ipairs(detail_targets) do
      if target.species == outlook.next then
        return index
      end
    end
  end
  return 1
end

local function open_chart_detail(document)
  local selected = chart_cells[chart_index + 1]
  if
    not selected
    or not selected.raised
    or ui.setting("player.chart.detail.layout_ready") ~= true
    or ui.setting("player.chart.detail.paths_ready") ~= true
  then
    return
  end
  for i = 0, 98 do
    document:get("chart-detail-line-" .. i):set_class("hidden", true)
  end
  for i = 0, 11 do
    document:get("chart-detail-cell-" .. i):set_class("hidden", true)
  end
  for i = 0, 5 do
    local inset = document:get("chart-detail-inset-" .. i)
    inset:set_property("left", ((detail_value("insets", i, 0) - 11) * 3) .. "dp")
    inset:set_property("top", ((detail_value("insets", i, 1) - 32) * 3) .. "dp")
    inset:set_property("width", ((detail_value("insets", i, 2) + 1) * 3) .. "dp")
    inset:set_property("height", ((detail_value("insets", i, 3) + 1) * 3) .. "dp")
  end
  local function draw_cell(slot, target, x, y)
    local box = document:get("chart-detail-cell-" .. slot)
    box:set_class("hidden", false)
    box:set_property("left", ((x - 11) * 3) .. "dp")
    box:set_property("top", ((y - 32) * 3) .. "dp")
    local cell = chart_cells[target + 1]
    local icon = document:get("chart-detail-icon-" .. slot)
    icon:set_class("hidden", not cell or not cell.raised)
    if cell and cell.raised then
      icon:set_attribute("src", "/" .. ui.setting(string.format("player.chart.%d.source", target)))
      icon:set_attribute("rect", string.format("%d %d 16 16", cell.u, cell.v))
    end
  end
  draw_cell(0, chart_index, 151, 99)
  local line_slot = 0
  local cell_slot = 1
  local function color(row)
    return string.format(
      "#%02x%02x%02x",
      detail_value("colors", row, 0),
      detail_value("colors", row, 1),
      detail_value("colors", row, 2)
    )
  end
  local function draw_path(row, primary, edge)
    for segment = 0, 2 do
      local x1 = detail_value("lines", row, segment)
      local y1 = detail_value("lines", row, segment + 4)
      local dx = detail_value("lines", row, segment + 1) - x1
      local dy = detail_value("lines", row, segment + 5) - y1
      for band = -1, 1 do
        local line = document:get("chart-detail-line-" .. line_slot)
        line_slot = line_slot + 1
        line:set_class("hidden", false)
        line:set_property("left", ((x1 + 149) * 3) .. "dp")
        line:set_property("top", ((y1 + 88 + band) * 3) .. "dp")
        line:set_property("width", ((math.sqrt(dx * dx + dy * dy) + 1) * 3) .. "dp")
        line:set_property("background-color", band == 0 and primary or edge)
        line:set_property(
          "transform",
          string.format("rotate(%fdeg)", math.atan(dy, dx) * 180 / math.pi)
        )
      end
    end
  end
  detail_targets = {}
  detail_from = { cells = {}, lines = {} }
  for _, side in ipairs({ "from", "to" }) do
    local refs = {}
    local count = 0
    local slots = side == "from" and 5 or 6
    for j = 0, slots - 1 do
      local value = tonumber(
        ui.setting(string.format("player.chart.%d.%s.%d", chart_index, side, j))
      ) or -1
      refs[j + 1] = value
      if value ~= -1 then
        count = count + 1
      end
    end
    local odd = count % 2 == 1
    local offset = side == "from" and (odd and 4 or 0) or (odd and 15 or 9)
    local length = side == "from" and (odd and 5 or 4) or (odd and 5 or 6)
    for j = 0, length - 1 do
      local target = refs[j + 1]
      if target and target >= 0 then
        local primary = side == "from" and "#e39407" or color(j * 2)
        local edge = side == "from" and "#db6500" or color(j * 2 + 1)
        local first_line = line_slot
        draw_path(offset + j, primary, edge)
        local x = detail_value("sprites", offset + j, 0) + 159
        local y = detail_value("sprites", offset + j, 1) + 119
        draw_cell(cell_slot, target, x, y)
        local lines = {}
        for slot = first_line, line_slot - 1 do
          lines[#lines + 1] = slot
        end
        if side == "from" then
          for _, slot in ipairs(lines) do
            detail_from.lines[#detail_from.lines + 1] = slot
          end
          detail_from.cells[#detail_from.cells + 1] = cell_slot
        else
          detail_targets[#detail_targets + 1] = {
            species = chart_species(target),
            x = x,
            y = y,
            cell_slot = cell_slot,
            lines = lines,
            primary = primary,
            edge = edge,
          }
        end
        cell_slot = cell_slot + 1
      end
    end
  end
  local base = string.format("player.chart.%d.", chart_index)
  document:get("chart-detail-name"):set_text(t(ui.setting(base .. "name") or ""))
  document:get("chart-detail-level"):set_text(stage_label(base))
  chart_detail = true
  -- The targets top to bottom, for the cursor.
  table.sort(detail_targets, function(a, b)
    return a.y < b.y
  end)
  detail_focus = requirements_on() and #detail_targets > 0 and first_focus() or nil
  detail_last_focus = detail_focus or 1
  refresh_requirements(document)
  begin_chart_transition(document, false)
  document:get("player-tab-chart"):focus()
end

local function medals_cursor(document)
  local cursor = document:get("medals-cursor")
  cursor:set_class("hidden", not medals_selecting)
  cursor:set_property("left", ((23 + (medal_index % 5) * 38) * 3) .. "dp")
  cursor:set_property("top", ((27 + math.floor(medal_index / 5) * 24) * 3) .. "dp")
end

-- renderMedalView: a medal the player holds shows without its dark cover, and the one under the
-- selector (held) shows its name and its three lines below the medals, and turns in the right box
-- (renderDigiviceMedals): its cell is marked `showing` for the game, which draws it into
-- #medal-figure.
local function refresh_medals(document)
  local held = number_list("player.medals.held")
  local shown = (held[medal_index] or 0) ~= 0
  for i = 0, 14 do
    local cell = document:get("medal-" .. i)
    cell:set_property("left", ((25 + (i % 5) * 38) * 3) .. "dp")
    cell:set_property("top", ((27 + math.floor(i / 5) * 24) * 3) .. "dp")
    cell:set_class("held", (held[i] or 0) ~= 0)
    cell:set_class("showing", shown and i == medal_index)
  end
  document:get("medal-figure"):set_class("hidden", not shown)
  local base = string.format("player.medal.%d.", medal_index)
  document:get("medal-name"):set_text(shown and t(ui.setting(base .. "name") or "") or "")
  for line = 0, 2 do
    document
      :get("medal-line-" .. line)
      :set_text(shown and t(ui.setting(base .. "line." .. line) or "") or "")
  end
  medals_cursor(document)
end

function medal_pick(document, index)
  medal_index = math.max(0, math.min(14, index))
  medals_selecting = true
  refresh_medals(document)
  document:get("medal-" .. medal_index):focus()
end

-- The card list (game_menu.c: renderCardsView, tickPlayerMenu): 11 by 6 slots of 18 x 18 at 24
-- pixels from (19, 25) of the page; the cursor's 24 x 20 frame from (17, 24).
local card_columns = 11
local card_slots = 66

local function cards_cursor(document)
  local cursor = document:get("cards-cursor")
  cursor:set_class("hidden", not cards_selecting)
  cursor:set_property("left", ((17 + (card_index % card_columns) * 24) * 3) .. "dp")
  cursor:set_property("top", ((24 + math.floor(card_index / card_columns) * 24) * 3) .. "dp")
end

-- renderCardsView: a card the player holds shows its 16 x 16 icon of SBOY, in its own palette.
local function refresh_cards(document)
  local copies = number_list("player.cards.copies")
  for i = 0, card_slots - 1 do
    local cell = document:get("card-" .. i)
    cell:set_property("left", ((19 + (i % card_columns) * 24) * 3) .. "dp")
    cell:set_property("top", ((25 + math.floor(i / card_columns) * 24) * 3) .. "dp")
    local icon = document:get("card-icon-" .. i)
    local held = (copies[i] or 0) > 0
    icon:set_class("hidden", not held)
    local palette =
      math.floor(tonumber(ui.setting(string.format("player.card.%d.palette", i))) or 0)
    if held and card_palettes[i] ~= palette then
      card_palettes[i] = palette
      icon:set_attribute("src", "/texture/etcdat/sboy?palette=" .. palette)
    end
  end
  cards_cursor(document)
end

-- The card's picture and copies (tickPlayerMenu: renderCardImage, renderCardCount) open from the
-- middle of its slot: the picture 150 x 180 at (74, 5) of the page, the copies 54 x 24 at
-- (223, 157). Only a card the player holds opens.
local function card_from(document)
  local rect = original_box.rect(document, "card-" .. card_index)
  return { rect[1] + 27, rect[2] + 27, 3, 3 }
end

local function open_card(document)
  local copies = number_list("player.cards.copies")[card_index] or 0
  if copies == 0 then
    return
  end
  card_open = true
  document:get("card-detail-image"):set_attribute("src", string.format("/card/%03d", card_index))
  document:get("card-count"):set_text(string.format("%d", copies))
  local from = card_from(document)
  original_box.open(document, "card-detail", from)
  original_box.open(document, "card-count-box", from)
end

local function close_card(document)
  card_open = false
  local to = card_from(document)
  original_box.close(document, "card-detail", to)
  original_box.close(document, "card-count-box", to)
end

-- Picks a card of the list (tickPlayerMenu, PLAYER_MENU_STATE 3): left and right stop at the row's
-- ends, up and down at the first and last rows.
local function card_move(document, action)
  local column = card_index % card_columns
  if action == "left" and column ~= 0 then
    card_index = card_index - 1
  elseif action == "right" and column ~= card_columns - 1 then
    card_index = card_index + 1
  elseif action == "up" and card_index >= card_columns then
    card_index = card_index - card_columns
  elseif action == "down" and card_index < card_slots - card_columns then
    card_index = card_index + card_columns
  else
    return
  end
  cards_cursor(document)
end

-- The City tab (a dwe addition, docs/modulos/apps/screens.md): every species that can join the
-- city, grouped by level, in colour once it lives there and as a silhouette while it is missing;
-- the prosperity and how many joined. The game publishes the lists (player.city.*) from the city's
-- rules; the icons and names are the evolution chart's (player.chart.*). Drawn again only when
-- those change; a cursor step redraws two cells and the footer.
local city_slots = 64
-- Both looks lay the grid out 12 to a row (style.rcss). The original look's cells are 73dp apart
-- from (10, 84) of the page, and its cursor is a frame 3dp outside the selected one.
local city_columns = 12
local city_original_left = 10
local city_original_top = 84
local city_original_pitch = 73
local city_cursor_outside = 3
-- A missing resident's icon: the same image in its grey tone (engine/ui image_reference.hpp).
local city_grey_tone = "tone=grey"

local function original_skin()
  return ui.setting("ui_skin") == "original"
end

local function city_footer(document)
  local cell = city_cells[city_index + 1]
  local shown = city_selecting and cell ~= nil
  document:get("city-name"):set_text(shown and cell.name or "")
  document:get("city-points"):set_text(shown and string.format("+%d", cell.points) or "")
end

local function city_cursor(document)
  local cell = city_cells[city_index + 1]
  local shown = city_selecting and cell ~= nil
  if city_cursor_cell ~= nil and city_cursor_cell ~= city_index then
    document:get("city-cell-" .. city_cursor_cell):set_class("cursor", false)
  end
  city_cursor_cell = shown and city_index or nil
  if shown then
    document:get("city-cell-" .. city_index):set_class("cursor", true)
  end
  local cursor = document:get("city-cursor")
  cursor:set_class("hidden", not shown)
  if shown then
    local column = city_index % city_columns
    local row = math.floor(city_index / city_columns)
    cursor:set_property(
      "left",
      (city_original_left + column * city_original_pitch - city_cursor_outside) .. "dp"
    )
    cursor:set_property(
      "top",
      (city_original_top + row * city_original_pitch - city_cursor_outside) .. "dp"
    )
  end
  city_footer(document)
end

local function refresh_city(document)
  local chart_count = math.floor(tonumber(ui.setting("player.chart.count")) or 0)
  local signature = table.concat({
    ui.setting("player.city.species") or "",
    ui.setting("player.city.recruited") or "",
    tostring(ui.setting("player.city.prosperity") or 0),
    tostring(chart_count),
  }, ";")
  if signature == city_signature then
    return
  end
  city_signature = signature
  local by_species = {}
  for i = 0, chart_count - 1 do
    local index = tonumber(ui.setting(string.format("player.chart.%d.index", i)))
    if index then
      by_species[math.floor(index)] = string.format("player.chart.%d.", i)
    end
  end
  local species = number_list("player.city.species")
  local levels = number_list("player.city.levels")
  local points = number_list("player.city.points")
  local recruited = number_list("player.city.recruited")
  city_cells = {}
  local joined = 0
  for i = 0, city_slots - 1 do
    local button = document:get("city-cell-" .. i)
    local number = species[i]
    button:set_class("hidden", number == nil)
    if number ~= nil then
      local base = by_species[number]
      local cell = {
        name = base and t(ui.setting(base .. "name") or "") or "",
        points = points[i] or 0,
        recruited = (recruited[i] or 0) ~= 0,
      }
      city_cells[i + 1] = cell
      if cell.recruited then
        joined = joined + 1
      end
      for level = 3, 5 do
        button:set_class("level-" .. level, levels[i] == level)
      end
      button:set_class("missing", not cell.recruited)
      local icon = document:get("city-icon-" .. i)
      icon:set_class("hidden", base == nil)
      if base then
        local source = "/" .. ui.setting(base .. "source")
        if not cell.recruited then
          source = source .. (source:find("?", 1, true) and "&" or "?") .. city_grey_tone
        end
        icon:set_attribute("src", source)
        icon:set_attribute(
          "rect",
          string.format("%d %d 16 16", ui.setting(base .. "u"), ui.setting(base .. "v"))
        )
      end
    end
  end
  document
    :get("city-prosperity")
    :set_text(tostring(math.floor(tonumber(ui.setting("player.city.prosperity")) or 0)))
  document:get("city-residents"):set_text(string.format("%d/%d", joined, #city_cells))
  if city_index >= #city_cells then
    city_index = 0
  end
  city_cursor(document)
end

function city_pick(document, index)
  if not city_cells[index + 1] then
    return
  end
  city_index = index
  city_selecting = true
  city_cursor(document)
  document:get("city-cell-" .. index):focus()
end

-- Left and right stop at the row's ends, up at the first row; down from a full row onto a shorter
-- last one lands on its last cell.
local function city_move(document, action)
  local columns = city_columns
  local count = #city_cells
  local column = city_index % columns
  local target = city_index
  if action == "left" and column ~= 0 then
    target = city_index - 1
  elseif action == "right" and column ~= columns - 1 and city_index + 1 < count then
    target = city_index + 1
  elseif action == "up" and city_index >= columns then
    target = city_index - columns
  elseif action == "down" then
    if city_index + columns < count then
      target = city_index + columns
    elseif math.floor(city_index / columns) < math.floor((count - 1) / columns) then
      target = count - 1
    end
  end
  if target ~= city_index then
    city_pick(document, target)
  end
end

function show_player_tab(document, tab)
  chart_detail = false
  chart_transition = nil
  document:get("chart-detail-transition"):set_class("hidden", true)
  document:get("panel"):set_class("chart-detail-open", false)
  medals_selecting = false
  medals_cursor(document)
  cards_selecting = false
  cards_cursor(document)
  card_open = false
  document:get("card-detail"):set_class("box-closed", true)
  document:get("card-count-box"):set_class("box-closed", true)
  player_tab = tab
  chart_selecting = false
  document:get("panel"):set_class("player-chart", tab == "chart")
  document:get("player-tab-player"):set_class("selected", tab == "player")
  document:get("player-tab-chart"):set_class("selected", tab == "chart")
  document:get("player-tab-medals"):set_class("selected", tab == "medals")
  document:get("panel"):set_class("player-medals", tab == "medals")
  document:get("player-tab-card"):set_class("selected", tab == "card")
  document:get("panel"):set_class("player-cards", tab == "card")
  document:get("player-tab-city"):set_class("selected", tab == "city")
  document:get("panel"):set_class("player-city", tab == "city")
  city_selecting = false
  if tab == "city" then
    refresh_city(document)
  end
  city_cursor(document)
  -- The chart opens on page 1.
  chart_go_page(document, 1, chart_index_on(1), true)
  document:get("player-tab-" .. tab):focus()
end

function chart_pick(document, index)
  if not chart_cells[index + 1] then
    return
  end
  chart_index = index
  chart_selecting = true
  chart_cursor(document)
  document:get("chart-cell-" .. index):focus()
end

-- Moves the cursor a cell in a direction on the page shown; whether it moved.
local function chart_move(document, action)
  local cell = chart_cells[chart_index + 1]
  if not cell then
    return false
  end
  local target = cell
  if action == "up" or action == "down" then
    local delta = action == "up" and -1 or 1
    local distance = math.huge
    for _, candidate in ipairs(chart_cells) do
      local dy = (candidate.y - cell.y) * delta
      if candidate.page == cell.page and candidate.x == cell.x and dy > 0 and dy < distance then
        target = candidate
        distance = dy
      end
    end
  else
    local delta = action == "left" and -1 or 1
    local distance = math.huge
    for _, candidate in ipairs(chart_cells) do
      local dx = (candidate.x - cell.x) * delta
      if candidate.page == cell.page and dx > 0 and dx < distance then
        distance = dx
        target = candidate
      end
    end
    if target ~= cell then
      local column_x = target.x
      local best_y = -math.huge
      local first_y = math.huge
      local first = target
      for _, candidate in ipairs(chart_cells) do
        if candidate.page == cell.page and candidate.x == column_x then
          if candidate.y < first_y then
            first = candidate
            first_y = candidate.y
          end
          if candidate.y <= cell.y and candidate.y > best_y then
            target = candidate
            best_y = candidate.y
          end
        end
      end
      if best_y == -math.huge then
        target = first
      end
    end
  end
  if target == cell then
    return false
  end
  for i, candidate in ipairs(chart_cells) do
    if candidate == target then
      chart_pick(document, i - 1)
      return true
    end
  end
  return false
end

-- The wheel over the chart turns its page: down to the next, up to the previous.
function chart_wheel(document, event)
  if chart_pages < 2 or chart_slide or chart_transition or chart_detail then
    return
  end
  local wheel = event.wheel
  if wheel > 0 then
    chart_turn(document, chart_page + 1)
  elseif wheel < 0 then
    chart_turn(document, chart_page - 1)
  end
  event:stop()
end

-- The arrows under the chart: a click turns to the next page (down) or the previous one (up).
local function chart_arrow(document, step)
  if chart_pages < 2 or chart_slide or chart_transition or chart_detail then
    return
  end
  chart_turn(document, chart_page + step)
end

function chart_more(document)
  chart_arrow(document, 1)
end

function chart_less(document)
  chart_arrow(document, -1)
end

local refresh_player

-- The option rows the game's options fill (docs/modulos/apps/game.md, «Opciones en partida»): the
-- document's free rows option-slot-<n>, given the option's id once so routes and the focus find
-- them by it. option_rows[n] is the row of game_options[n].
local option_rows = nil
local max_option_rows = 16

local function bind_option_rows(document)
  if option_rows ~= nil then
    return
  end
  option_rows = {}
  for slot = 1, max_option_rows do
    local row = document:get("option-slot-" .. slot)
    local option = game_options[slot]
    if option ~= nil then
      row:set_attribute("id", option.id)
      document:get("option-slot-" .. slot .. "-value"):set_attribute("id", option.id .. "-value")
      row:set_class("hidden", false)
      option_rows[slot] = row
    end
  end
end

local function refresh_option_rows(document)
  bind_option_rows(document)
  for slot, row in ipairs(option_rows) do
    local option = game_options[slot]
    document:get("option-slot-" .. slot .. "-label"):set_text(t(option.label))
    document:get(option.id .. "-value"):set_text(game_options.text(option))
    row:set_class("hidden", false)
  end
end

-- The row of an option, confirmed (step 1) or moved left and right.
function adjust_slot(_document, slot, step)
  local option = game_options[tonumber(slot)]
  if option ~= nil then
    game_options.change(option, tonumber(step) or 1)
  end
end

local function refresh(document)
  refresh_medals(document)
  refresh_cards(document)
  refresh_chart(document)
  refresh_city(document)
  draw_date_digits(document, "field-year-", 3, ui.setting("hud.clock.year"))
  draw_date_digits(document, "field-day-", 2, ui.setting("hud.clock.day"))
  local original = ui.setting("ui_skin") == "original"
  document:get("panel"):set_class("use-menu-art", (ui.setting("language") or "en") == "en")
  document:get("sleep"):set_class("unavailable", not can_sleep())
  document:get("panel"):set_class("has-fishing", original and fishing() ~= 0)
  document:get("sleep"):set_class("hidden", false)
  document:get("fish"):set_class("hidden", fishing() == 0)
  document:get("fish"):set_class("unavailable", fishing() ~= 2)
  local mode = index_of(minimap_modes, ui.setting("minimap")) and ui.setting("minimap") or "auto"
  document:get("minimap-mode-value"):set_text(t("pause_menu.minimap_mode." .. mode))
  local size = index_of(minimap_sizes, ui.setting("minimap_size")) and ui.setting("minimap_size")
    or "medium"
  document:get("minimap-size-value"):set_text(t("pause_menu.minimap_size." .. size))
  document:get("display-mode-value"):set_text(
    t(
      ui.setting("fullscreen") and "pause_menu.display_mode.fullscreen"
        or "pause_menu.display_mode.window"
    )
  )
  local percent = math.floor((ui.setting("ui_scale") or 1.0) * 100 + 0.5)
  document:get("ui-scale-value"):set_text(t("pause_menu.ui_scale.value", { percent = percent }))
  document:get("vsync-value"):set_text(on_off(ui.setting("vsync")))
  document:get("ui-skin-value"):set_text(skin_choice.name())
  document:get("language-value"):set_text(t("language.name"))
  local font = ui.setting("text_font") == "original" and "original" or "modern"
  document:get("text-font-value"):set_text(t("main_menu.text_font." .. font))
  refresh_option_rows(document)
  refresh_player(document)
end

-- The PLAYER page's own fields: its name, level, rank, bits, counts, items and play time.
refresh_player = function(document)
  local name = ui.setting("player.name") or ""
  document:get("player-name"):set_text(name)
  -- A name longer than the original's six letters (the engine's long names) widens its box.
  document:get("panel"):set_class("long-player-name", ((utf8 and utf8.len(name)) or #name) > 6)
  local level = ui.setting("player.level")
  document
    :get("player-level")
    :set_text(type(level) == "number" and string.format("%d", level) or "")
  local rank = ""
  if type(level) == "number" and level >= 0 and level % 1 == 0 then
    local key = string.format("main.game_menu_level_titles.%04d", level)
    local label = t(key)
    if label ~= key then
      rank = label
    end
  end
  document:get("player-rank"):set_text(rank)
  local bits = ui.setting("player.bits")
  document:get("player-bits"):set_text(type(bits) == "number" and string.format("%d", bits) or "")
  for _, field in ipairs({ "raised", "medals" }) do
    local value = ui.setting("player." .. field)
    document
      :get("player-" .. field)
      :set_text(type(value) == "number" and string.format("%d", value) or "")
  end
  -- renderPlayerInfoView: the item boxes show the rods and the two other items the player has.
  local items = number_list("player.items.held")
  for slot = 0, 3 do
    document:get("player-item-" .. slot):set_class("hidden", (items[slot] or 0) == 0)
  end
  local seconds = ui.setting("player.seconds")
  document:get("player-time"):set_text(
    type(seconds) == "number"
        and string.format(
          ui.setting("ui_skin") == "original" and "%d : %d" or "%d:%02d",
          math.floor(seconds / 3600),
          math.floor(seconds / 60) % 60
        )
      or ""
  )
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
  document:get("prompt-cancel"):set_text(ui.prompt("cancel"))
  -- The player page's prompts name the key of the device in use (a PC game: the keyboard's keys
  -- first, a pad's buttons when a pad plays), not the disc's fixed pad glyphs.
  for _, page in ipairs({ "chart", "medals", "cards", "city" }) do
    document:get(page .. "-confirm-key"):set_text(ui.prompt("confirm"))
    document:get(page .. "-cancel-key"):set_text(ui.prompt("cancel"))
  end
end

local function show_page(document, name, focus, tools)
  for _, page in ipairs(pages) do
    document:get("page-" .. page):set_class("hidden", page ~= name)
  end
  if name == "player" and current == "main" then
    player_tab = "player"
    chart_selecting = false
    document:get("player-tab-player"):set_class("selected", true)
    document:get("player-tab-chart"):set_class("selected", false)
    document:get("player-tab-medals"):set_class("selected", false)
    document:get("player-tab-city"):set_class("selected", false)
    chart_cursor(document)
    city_selecting = false
    city_cursor(document)
  end
  current = name
  document:get("panel"):set_class("player-chart", name == "player" and player_tab == "chart")
  document:get("panel"):set_class("player-medals", name == "player" and player_tab == "medals")
  document:get("panel"):set_class("player-city", name == "player" and player_tab == "city")
  player_tools_open = tools == true
  document:get("panel"):set_class("player-tools-open", player_tools_open)
  document
    :get("panel")
    :set_class("on-player", name == "player" and ui.setting("ui_skin") == "original")
  document
    :get("title")
    :set_text(t(player_tools_open and "pause_menu.title.tools" or "pause_menu.title." .. name))
  -- The original's menu box heads its icons with the date (game_menu.c: renderGameMenu); a skin
  -- shows it on the main page through the panel's class "on-main".
  document:get("panel"):set_class("on-main", name == "main")
  -- The settings pages: a skin may give their label-and-value rows a wider box.
  document:get("panel"):set_class("on-settings", name == "options" or name == "minimap")
  local first = name == "main"
      and ui.setting("ui_skin") == "original"
      and (fishing() ~= 0 and "fish" or "open-items")
    or (
      name == "player"
        and ui.setting("ui_skin") == "original"
        and (player_tools_open and "player-minimap" or "player-tab-" .. player_tab)
      or first_entry[name]
    )
  document:get(focus or first):focus()
end

function open_page(document, name)
  parents[name] = current
  show_page(document, name)
end

-- The original's boxes (ui/common/original_box.lua): the menu box grows from the player and shrinks
-- back into it (game_menu.c: createMenuBox, closeUIBoxIfOpen); going to a page closes it first and
-- opens the page's box from the player then, ten frames in all (measured: the outline's frames
-- 4..0, then 0..4).
local function close_menu_box(document, done)
  original_box.close(document, "panel", original_box.player_rect(document), done)
end

local function open_menu_box(document)
  original_box.open(document, "panel", original_box.player_rect(document))
end

function open_player(document)
  if current ~= "main" or not original_box.enabled() then
    open_page(document, "player")
    return
  end
  close_menu_box(document, function()
    open_page(document, "player")
    open_menu_box(document)
  end)
end

local function close_menu(document)
  close_menu_box(document, function()
    ui.hide(document_name)
  end)
end

-- The modern look's City entry on the player page: the view takes the page until Back.
function open_city(document)
  show_player_tab(document, "city")
  document:get("title"):set_text(t("pause_menu.player_tab_city"))
  city_pick(document, city_index)
end

local function close_city(document)
  show_player_tab(document, "player")
  document:get("title"):set_text(t("pause_menu.title.player"))
  document:get("player-city"):focus()
end

function back(document)
  if current == "player" and player_tab == "city" and not original_skin() then
    close_city(document)
    return
  end
  if current == "player" and player_tools_open then
    player_tools_open = false
    document:get("panel"):set_class("player-tools-open", false)
    document:get("title"):set_text(t("pause_menu.title.player"))
    document:get("player-tab-" .. player_tab):focus()
    return
  end
  if current == "options" and options_direct then
    ui.hide(document_name)
  elseif current == "main" then
    close_menu(document)
  elseif current == "player" and original_box.enabled() then
    close_menu_box(document, function()
      show_page(document, "main", opener.player)
      open_menu_box(document)
    end)
  else
    local parent = parents[current] or "main"
    local focus = parent == "player" and "player-" .. current or opener[current]
    show_page(document, parent, focus, parent == "player" and ui.setting("ui_skin") == "original")
  end
end

-- The document shown last, for the buttons' event attributes that only name the page.
local shown_document = nil

function open_partner(page)
  close_menu_box(shown_document, function()
    ui.send("open_partner_menu", page)
  end)
end

-- The game closes the menu and puts the partner to bed (docs/architecture/guardar-y-cargar.md,
-- 3.6).
function sleep()
  if can_sleep() then
    ui.send("sleep")
  end
end

-- The game closes the menu and the player takes out the rod (game_menu.c: initializeFishing).
function fish()
  if fishing() == 2 then
    ui.send("fish")
  end
end

function resume()
  ui.hide(document_name)
end

function adjust(_document, id, step)
  options[id](step)
end

function on_show(document)
  shown_document = document
  chart_detail = false
  chart_transition = nil
  chart_go_page(document, 1, chart_index_on(1), true)
  document:get("chart-detail-transition"):set_class("hidden", true)
  document:get("panel"):set_class("chart-detail-open", false)
  parents = {}
  icon_time = 0
  refresh(document)
  local entry = ui.setting("ui_skin") == "original" and ui.setting("pause_menu.start_entry") or nil
  -- F10 (Select on a pad) opens the menu straight on its options page (pause_menu.start_page,
  -- «Opciones en partida»); going back from there closes the menu.
  options_direct = ui.setting("pause_menu.start_page") == "options"
  if options_direct then
    show_page(document, "options")
  elseif ui.setting("pause_menu.preview_player") == true then
    parents.player = "main"
    show_page(document, "player")
    if ui.setting("player.chart.preview") == true then
      show_player_tab(document, "chart")
    end
  else
    show_page(document, "main", entry)
    open_menu_box(document)
  end
  refresh_prompts(document)
end

function on_hide(document)
  original_box.cancel(document)
end

function on_tick(document, seconds)
  original_box.tick(document, seconds)
  chart_slide_tick(document, seconds)
  if chart_transition then
    chart_transition.elapsed = chart_transition.elapsed + seconds
    local step = math.floor(chart_transition.elapsed * 20 + 0.000001)
    if step >= 5 then
      local closing = chart_transition.closing
      chart_transition = nil
      document:get("chart-detail-transition"):set_class("hidden", true)
      document:get("panel"):set_class("chart-detail-open", not closing)
      if closing then
        chart_detail = false
        document:get("chart-cell-" .. chart_index):focus()
      end
    else
      transition_box(document, chart_transition.closing and 4 - step or step)
    end
  end
  if current == "player" and player_tab == "chart" and not player_tools_open then
    icon_time = icon_time + seconds
    local flash = math.floor(icon_time * 20) % 10 < 5
    if flash ~= chart_flash then
      chart_flash = flash
      for i, cell in ipairs(chart_cells) do
        if cell.raised then
          document:get("chart-icon-" .. (i - 1)):set_attribute(
            "rect",
            string.format(
              "%d %d 16 16",
              cell.u + (chart_selecting and chart_index == i - 1 and flash and 16 or 0),
              cell.v
            )
          )
        end
      end
    end
  end
  if current ~= "main" or ui.setting("ui_skin") ~= "original" then
    return
  end
  icon_time = icon_time + seconds
  local flash = math.floor(icon_time * 20) % 10 < 5
  if flash ~= icon_flash then
    icon_flash = flash
    document:get("panel"):set_class("icon-flash", flash)
  end
end

-- A setting changed: only what reads it is drawn again. The game publishes the PLAYER page's
-- values one by one and the HUD's each frame it changes, and a whole refresh for each of them made
-- opening the menu stall; any other setting refreshes the whole menu.
function on_setting(document, name)
  if name:sub(1, 15) == "save_indicator." then
    -- The save indicator's own (ui/save_indicator), which change every frame while it fades.
    return
  elseif name:sub(1, 4) == "hud." then
    if name == "hud.clock.year" or name == "hud.clock.day" then
      draw_date_digits(document, "field-year-", 3, ui.setting("hud.clock.year"))
      draw_date_digits(document, "field-day-", 2, ui.setting("hud.clock.day"))
    end
  elseif name:sub(1, 7) == "evoreq." then
    -- The game sets evoreq.version last, once the rest of a change is in.
    if name == "evoreq.version" or name == "evoreq.ready" then
      refresh_requirements(document)
    end
  elseif name == "evolution_requirements" then
    refresh(document)
    refresh_requirements(document)
  elseif name:sub(1, 12) == "player.chart" then
    refresh_chart(document)
    refresh_city(document)
  elseif name:sub(1, 11) == "player.city" then
    refresh_city(document)
  elseif name:sub(1, 12) == "player.medal" then
    refresh_medals(document)
    refresh_player(document)
  elseif name:sub(1, 11) == "player.card" then
    refresh_cards(document)
  elseif name:sub(1, 7) == "player." then
    refresh_player(document)
  else
    refresh(document)
  end
end

function on_device(document)
  refresh_prompts(document)
end

-- The language changed: the elements with a text-key are already in the new one; the rest are
-- written here.
function on_language(document)
  document:get("title"):set_text(t("pause_menu.title." .. current))
  refresh(document)
  refresh_prompts(document)
end

-- Navigation steps arrive here first. Returning true means the step was handled here and suppresses
-- the default (moving the focus, confirming, closing); returning false lets the default run.
function on_action(document, action)
  -- tickUIBox: a box takes no input until it is in place.
  if chart_transition or chart_slide or original_box.busy() then
    return true
  end
  if chart_detail then
    if action == "cancel" then
      begin_chart_transition(document, true)
    elseif action == "up" or action == "down" or action == "left" or action == "right" then
      detail_move(document, action)
    end
    return true
  end
  if current == "player" and player_tab == "city" and not original_skin() then
    if action == "up" or action == "down" or action == "left" or action == "right" then
      city_move(document, action)
      return true
    end
    if action == "confirm" then
      return true
    end
  end
  if current == "player" and not player_tools_open and ui.setting("ui_skin") == "original" then
    if player_tab == "city" and city_selecting then
      if action == "cancel" then
        city_selecting = false
        city_cursor(document)
        document:get("player-tab-city"):focus()
      elseif action == "up" or action == "down" or action == "left" or action == "right" then
        city_move(document, action)
      end
      return true
    end

    if player_tab == "medals" and medals_selecting then
      if action == "cancel" then
        medals_selecting = false
        medals_cursor(document)
        document:get("player-tab-medals"):focus()
      elseif action == "left" and medal_index % 5 > 0 then
        medal_pick(document, medal_index - 1)
      elseif action == "right" and medal_index % 5 < 4 then
        medal_pick(document, medal_index + 1)
      elseif action == "up" and medal_index >= 5 then
        medal_pick(document, medal_index - 5)
      elseif action == "down" and medal_index < 10 then
        medal_pick(document, medal_index + 5)
      end
      return true
    end

    if player_tab == "card" and card_open then
      if action == "cancel" then
        close_card(document)
      end
      return true
    end

    if player_tab == "card" and cards_selecting then
      if action == "confirm" then
        open_card(document)
      elseif action == "cancel" then
        cards_selecting = false
        cards_cursor(document)
        document:get("player-tab-card"):focus()
      elseif action == "up" or action == "down" or action == "left" or action == "right" then
        card_move(document, action)
      end
      return true
    end

    if player_tab == "chart" and chart_selecting then
      if action == "cancel" then
        chart_selecting = false
        chart_cursor(document)
        document:get("player-tab-chart"):focus()
        return true
      end
      if action == "up" or action == "down" or action == "left" or action == "right" then
        -- Down past a column's last cell turns to the next page, up past its first to the previous.
        if not chart_move(document, action) and (action == "down" or action == "up") then
          chart_turn(document, chart_page + (action == "down" and 1 or -1))
        end
        return true
      end
      if action == "confirm" then
        open_chart_detail(document)
        return true
      end
    else
      if action == "right" or action == "left" then
        -- tickPlayerMenu: the four pages are always there, in this order; dwe adds the City tab.
        local tabs = { "player", "chart", "medals", "card", "city" }
        local current_index = index_of(tabs, player_tab)
        local delta = action == "left" and -1 or 1
        show_player_tab(document, tabs[math.max(1, math.min(#tabs, current_index + delta))])
        return true
      end
      if action == "confirm" and player_tab == "city" then
        city_pick(document, city_index)
        return true
      end
      if action == "confirm" and player_tab == "card" then
        card_index = 0
        cards_selecting = true
        cards_cursor(document)
        return true
      end
      if action == "confirm" and player_tab == "medals" then
        -- tickPlayerMenu: Cross starts the selector on the first medal.
        medal_pick(document, 0)
        return true
      end
      if action == "confirm" and player_tab == "chart" then
        chart_pick(document, chart_index)
        return true
      end
      -- Up and down on the chart's tab turn its page while it has more than one.
      if
        player_tab == "chart"
        and (action == "down" or action == "up")
        and chart_turn(document, chart_page + (action == "down" and 1 or -1))
      then
        return true
      end
    end
  end
  if action == "cancel" then
    back(document)
    return true
  end
  if action == "menu" and current == "player" and ui.setting("ui_skin") == "original" then
    player_tools_open = not player_tools_open
    document:get("panel"):set_class("player-tools-open", player_tools_open)
    document
      :get("title")
      :set_text(t(player_tools_open and "pause_menu.title.tools" or "pause_menu.title.player"))
    document:get(player_tools_open and "player-minimap" or "player-tab-" .. player_tab):focus()
    return true
  end
  if action == "menu" then
    if current == "main" then
      close_menu(document)
    else
      ui.hide(document_name)
    end
    return true
  end
  if
    current == "main"
    and ui.setting("ui_skin") == "original"
    and (action == "up" or action == "down" or action == "left" or action == "right")
  then
    local grid = { "open-items", "open-partner", "open-player", "praise", "scold", "sleep" }
    if fishing() ~= 0 then
      table.insert(grid, "fish")
    end
    for index, id in ipairs(grid) do
      if document:get(id):has_focus() then
        local next_index
        if action == "left" or action == "right" then
          next_index = (index - 1 + (action == "left" and -1 or 1)) % #grid + 1
        elseif action == "up" then
          next_index = index - 3
          if next_index < 1 then
            next_index = next_index + math.floor((#grid + 2) / 3) * 3
          end
          if next_index > #grid then
            next_index = next_index - 3
          end
        else
          next_index = index + 3
          if next_index > #grid then
            next_index = next_index - math.floor((#grid + 2) / 3) * 3
          end
          if next_index < 1 then
            next_index = next_index + 3
          end
        end
        document:get(grid[next_index]):focus()
        return true
      end
    end
  end
  if action == "left" or action == "right" then
    for id, change in pairs(options) do
      local entry = document:get(id)
      if entry ~= nil and entry:has_focus() then
        change(action == "left" and -1 or 1)
        return true
      end
    end
    for slot, row in ipairs(option_rows or {}) do
      if row:has_focus() then
        adjust_slot(document, slot, action == "left" and -1 or 1)
        return true
      end
    end
  end
  return false
end
