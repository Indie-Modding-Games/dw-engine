-- The HUD follows the minimap settings the game reports: on or off (minimap), and its size; under
-- the round minimap it writes the game's day and time (#minimap-day, #minimap-time).
-- Its menu button asks the game to open the pause menu (the open_menu command) and shows the
-- control that opens it with the last device used. On a map change the game subtracts its fade from
-- the scene under the HUD and reports how much it takes away (screen_fade, 0 to 1): the panels fade
-- out with it, as the original's fade covers its HUD. The name of the map it goes to shows in front
-- of the fade, in the player's language: the game reports its text key (map_title_key, empty when
-- none).
--
-- The field status shows the game time and the partner's state, which the game publishes as hud.*
-- settings (apps/screens: field_hud_view.hpp): it bumps hud.revision once per change, and only then
-- this script redraws, so a frame where nothing changed writes nothing. The labels come once, as
-- text keys. hud.shown is false while a menu or a battle stops the field: the panel and the menu's
-- hint lose their class "shown" and each skin decides how they leave. Every skin draws this same
-- document and script and differs only in its style sheets: a bar with the class "pips" fills in
-- the original's 25 steps, and the dial (#field-clock-dial) and the class "night" on #field-clock
-- are there for a skin that draws the original's clock; the class "digital" on #field-clock asks
-- for the time in figures (#field-time) too (digital_time). #field-partner-bubble hangs over the
-- partner's head (hud.partner.*) with the class of the need the original's bubble would show, or,
-- with several needs and need_bubble_stack (an engine improvement, on unless turned off), the class
-- "stack-2" or "stack-3"; its icons #field-partner-bubble-icon-<i> carry the needs it shows, and
-- the class "frame-<n>" says which of the original's frames the bubble is on, for the skin. How
-- the partner feels about the map (hud.partner.area) puts the class "area-liked" or "area-disliked"
-- on #field-status, where a skin can swap the happiness face as the original does.

local sizes = { small = true, medium = true, large = true }
local function refresh(document)
  local minimap = document:get("minimap")
  -- The round minimap (an engine improvement) shows unless the player turns it off (minimap: off;
  -- on, and auto from older settings, show it). It also hides while the game says the field is
  -- not the player's to walk (hud.minimap_allowed false: battles, scenes and talk, minigames;
  -- apps/game: minimap_setting.hpp) and in a battle (its HP bars and end box take that corner).
  minimap:set_class(
    "hidden",
    ui.setting("minimap") == "off"
      or ui.setting("minimap") == false
      or ui.setting("battle.active") == true
      or ui.setting("hud.minimap_allowed") == false
  )
  -- The game hides it while a window lies over it (hud.minimap_covered, apps/game:
  -- minimap_setting.hpp), whatever the skin.
  minimap:set_class("covered", ui.setting("hud.minimap_covered") == true)
  local size = ui.setting("minimap_size")
  if not sizes[size] then
    size = "medium"
  end
  for name in pairs(sizes) do
    minimap:set_class("size-" .. name, name == size)
  end
end

local function refresh_prompts(document)
  document:get("prompt-menu"):set_prompt("menu")
end

local panels = { "minimap", "menu-button", "field-status" }

local function refresh_fade(document)
  local level = ui.setting("screen_fade")
  if type(level) ~= "number" then
    level = 0
  end
  local opacity = string.format("%.3f", 1 - math.min(math.max(level, 0), 1))
  for _, id in ipairs(panels) do
    document:get(id):set_property("opacity", opacity)
  end
end

local function refresh_title(document)
  local key = ui.setting("map_title_key")
  local shown = type(key) == "string" and key ~= ""
  local element = document:get("map-title")
  element:set_text(shown and t(key) or "")
  element:set_class("shown", shown)
end

local field_meters = 2
local field_needs = 6
local field_pools = 2
-- The classes of hud.partner.area's values, the original's numbers: 1 the partner likes the area, 2
-- it dislikes it, 0 (no class) neither.
local area_classes = { [1] = "area-liked", [2] = "area-disliked" }

local function field_number(name)
  local value = ui.setting(name)
  if type(value) ~= "number" then
    return 0
  end
  return math.floor(value)
end

local function field_label(name)
  local key = ui.setting(name)
  if type(key) ~= "string" or key == "" then
    return ""
  end
  return t(key)
end

local function percent(value, minimum, maximum)
  if maximum <= minimum then
    return 0
  end
  return math.max(0, math.min(100, (value - minimum) * 100 / (maximum - minimum)))
end

-- The original's field bars are 25 squares of 2 pixels (partner_impl.c: renderStatusBars), so a bar
-- with the class "pips" fills in whole squares.
local pips_per_bar = 25

-- The original's dial (clock.c: renderGameClock): the minute hand turns once an hour, 6 degrees a
-- minute; the hour dot goes once a day round a 24-hour ring, 15 degrees an hour, hour 0 at the top;
-- the moon replaces the sun before 6:00 and from 17:00.
local minute_hand_degrees = 6
local hour_dot_degrees = 15
local hour_dot_radius_percent = 50
local night_ends_hour = 6
local night_starts_hour = 17

local function two_digits(value)
  return string.format("%02d", value)
end

local function draw_field_labels(document)
  for index = 0, field_meters - 1 do
    document
      :get("field-meter-" .. index .. "-label")
      :set_text(field_label("hud.meter." .. index .. ".label"))
  end
  for index = 0, field_needs - 1 do
    document:get("field-need-" .. index):set_text(field_label("hud.need." .. index .. ".label"))
  end
  for index = 0, field_pools - 1 do
    document
      :get("field-pool-" .. index .. "-label")
      :set_text(field_label("hud.pool." .. index .. ".label"))
  end
end

local function draw_field_dial(document, hour, minute)
  document
    :get("field-clock")
    :set_class("night", hour < night_ends_hour or hour >= night_starts_hour)
  document
    :get("field-clock-minute-hand")
    :set_property("transform", string.format("rotate(%ddeg)", minute * minute_hand_degrees))
  local angle = math.rad(hour * hour_dot_degrees)
  local dot = document:get("field-clock-hour-dot")
  dot:set_property("left", string.format("%.2f%%", 50 + hour_dot_radius_percent * math.sin(angle)))
  dot:set_property("top", string.format("%.2f%%", 50 - hour_dot_radius_percent * math.cos(angle)))
end

-- The time in figures as last written, so a redraw for another value leaves it alone and it is only
-- written again when the minute changes (or the language, which clears it).
local drawn_time = nil

-- The time in digits (digital_time, an engine improvement, on unless turned off): the class
-- "digital" on #field-clock, where a skin shows #field-time.
local function draw_digital(document)
  document:get("field-clock"):set_class("digital", ui.setting("digital_time") ~= false)
end

local function draw_field_clock(document)
  local hour, minute = field_number("hud.clock.hour"), field_number("hud.clock.minute")
  draw_field_dial(document, hour, minute)
  local time = t("hud.clock.time", {
    hour = two_digits(hour),
    minute = two_digits(minute),
  })
  if time ~= drawn_time then
    document:get("field-time"):set_text(time)
    document:get("minimap-time"):set_text(time)
    drawn_time = time
  end
  document
    :get("minimap-day")
    :set_text(t("hud.minimap.day", { day = field_number("hud.clock.day") }))
  document:get("field-date"):set_text(t("hud.clock.date", {
    day = field_number("hud.clock.day"),
    year = field_number("hud.clock.year"),
  }))
end

-- A two-tone bar fills from the minimum to the middle of its range, then again in its second colour
-- from the middle to the maximum, as the original's field bars (partner_impl.c: renderStatusBars).
-- Whether a bar's value is below the middle of its range. The original swaps the bar's icon there:
-- the happiness face turns sad below 0 and the discipline icon changes below 50 (partner_impl.c:
-- renderStatusBars), so a skin draws its icons on the class "low" of the bar's row.
local function in_low_half(value, minimum, maximum)
  return value < (minimum + maximum) / 2
end

-- A bar's fill in percent; in whole pips when the bar has the class "pips", rounding down as the
-- original does.
local function bar_fill(bar, value, minimum, maximum)
  local fill = percent(value, minimum, maximum)
  if bar:has_class("pips") then
    local step = 100 / pips_per_bar
    fill = math.floor(fill / step + 1e-9) * step
  end
  return fill
end

local function draw_field_meter(document, index)
  local base = "hud.meter." .. index .. "."
  local prefix = "field-meter-" .. index
  local bar = document:get(prefix .. "-bar")
  local value, low, top =
    field_number(base .. "value"), field_number(base .. "min"), field_number(base .. "max")
  local fill, high = bar_fill(bar, value, low, top), 0
  if ui.setting(base .. "two_tone") == true then
    local middle = (low + top) / 2
    fill = bar_fill(bar, value, low, middle)
    high = bar_fill(bar, value, middle, top)
  end
  document:get(prefix .. "-fill"):set_property("width", fill .. "%")
  document:get(prefix .. "-high"):set_property("width", high .. "%")
  document:get(prefix):set_class("low", in_low_half(value, low, top))
  document:get(prefix .. "-value"):set_text(tostring(value))
end

local function draw_field_pool(document, index)
  local base = "hud.pool." .. index .. "."
  local current, maximum = field_number(base .. "value"), field_number(base .. "max")
  local bar = document:get("field-pool-" .. index .. "-bar")
  document
    :get("field-pool-" .. index .. "-fill")
    :set_property("width", bar_fill(bar, current, 0, maximum) .. "%")
  document:get("field-pool-" .. index .. "-value"):set_text(current .. " / " .. maximum)
end

-- The need bubble over the partner (bubble.c), one need at a time. The original picks it in this
-- order, highest first (partner_impl.c: handleConditionBubble checks them from the lowest and the
-- last match wins), and while a bubble has shown for 50 of its 20-a-second frames it swaps to the
-- highest other need the partner has, if any, so two needs take turns; a third waits until one of
-- them goes. Injury and sickness come last, below the toilet.
local bubble_priority = { "sleep", "tired", "hunger", "toilet", "injured", "sick" }
local bubble_need_index = { hunger = 0, sleep = 1, toilet = 2, tired = 3, injured = 4, sick = 5 }
-- A bubble's frames (bubble.c: tickConditionBubble, renderConditionBubble): the game makes it and
-- it is drawn on frames 1 to 51, growing on the first 10 and bursting on the 51st, then it is gone
-- until the game makes it again 60 frames after the last time (handleConditionBubble:
-- CONDITION_BUBBLE_TIMER), or at 50 for the other need's bubble. The script counts them and puts
-- the class "frame-<n>" (1 to 60) on #field-partner-bubble; the skin draws each frame, the growth,
-- the need's own animation, the burst and the gap. They stop while the bubble is not drawn
-- (renderConditionBubble and tickConditionBubble do nothing in a menu).
local bubble_frames_per_second = 20
local bubble_swap_frames = 50
local bubble_cycle_frames = 60
-- No bubble when the game starts (bubble.c: initializeConditionBubbles marks every bubble free).
local bubble_need = nil
local bubble_seconds = 0
local bubble_frame = nil
-- need_bubble_stack: up to this many needs at once, side by side, the most urgent first.
local bubble_stack_most = 3

local function need_active(need)
  return ui.setting("hud.need." .. bubble_need_index[need] .. ".active") == true
end

-- The highest need the partner has other than `except`, or nil.
local function highest_need(except)
  for _, need in ipairs(bubble_priority) do
    if need ~= except and need_active(need) then
      return need
    end
  end
  return nil
end

-- A coordinate of the partner's head, in percent of the window.
local function place(name)
  local value = ui.setting(name)
  if type(value) ~= "number" then
    return 0
  end
  return value
end

-- The needs the stacked bubble shows (need_bubble_stack, on unless turned off): the partner's,
-- most urgent first, up to bubble_stack_most; empty when off.
local function stacked_needs()
  local needs = {}
  if ui.setting("need_bubble_stack") == false then
    return needs
  end
  for _, need in ipairs(bubble_priority) do
    if #needs < bubble_stack_most and need_active(need) then
      needs[#needs + 1] = need
    end
  end
  return needs
end

-- Moves the class "frame-<n>" to the bubble's frame now.
local function draw_bubble_frame(bubble)
  local frame = math.floor(bubble_seconds * bubble_frames_per_second) + 1
  if frame == bubble_frame then
    return
  end
  if bubble_frame ~= nil then
    bubble:set_class("frame-" .. bubble_frame, false)
  end
  bubble:set_class("frame-" .. frame, true)
  bubble_frame = frame
end

local function draw_bubble(document)
  local bubble = document:get("field-partner-bubble")
  local visible = ui.setting("hud.partner.visible") == true
  bubble:set_property("left", string.format("%.2f%%", place("hud.partner.x")))
  bubble:set_property("top", string.format("%.2f%%", place("hud.partner.y")))
  bubble:set_class("shown", visible and bubble_need ~= nil)
  draw_bubble_frame(bubble)
  -- One need alone is the original's bubble either way: its need on the bubble and on the first
  -- icon.
  local stack = stacked_needs()
  local count = #stack > 1 and #stack or 0
  for size = 2, bubble_stack_most do
    bubble:set_class("stack-" .. size, count == size)
  end
  for need in pairs(bubble_need_index) do
    bubble:set_class(need, count == 0 and need == bubble_need)
  end
  local icons = count > 0 and stack or { bubble_need }
  for index = 0, bubble_stack_most - 1 do
    local icon = document:get("field-partner-bubble-icon-" .. index)
    for need in pairs(bubble_need_index) do
      icon:set_class(need, icons[index + 1] == need)
    end
  end
end

-- The needs changed: a bubble whose need went gives way to the highest one left, in a new bubble.
local function pick_bubble(document)
  if bubble_need == nil or not need_active(bubble_need) then
    bubble_need = highest_need(nil)
    bubble_seconds = 0
  end
  draw_bubble(document)
end

local function tick_bubble(document, seconds)
  if bubble_need == nil or ui.setting("hud.partner.visible") ~= true then
    return
  end
  bubble_seconds = bubble_seconds + seconds
  local swap = bubble_swap_frames / bubble_frames_per_second
  local cycle = bubble_cycle_frames / bubble_frames_per_second
  -- The stacked bubble already shows the other needs: it only starts again.
  local other = #stacked_needs() < 2 and highest_need(bubble_need) or nil
  if other ~= nil and bubble_seconds >= swap then
    bubble_need = other
    bubble_seconds = 0
    draw_bubble(document)
    return
  end
  while bubble_seconds >= cycle do
    bubble_seconds = bubble_seconds - cycle
  end
  draw_bubble_frame(document:get("field-partner-bubble"))
end

local function draw_partner_area(status)
  local area = ui.setting("hud.partner.area")
  for value, class in pairs(area_classes) do
    status:set_class(class, area == value)
  end
end

local function draw_field(document)
  local status = document:get("field-status")
  status:set_class("shown", ui.setting("hud.shown") == true)
  document:get("menu-button"):set_class("shown", ui.setting("hud.shown") == true)
  draw_partner_area(status)
  draw_field_clock(document)
  for index = 0, field_meters - 1 do
    draw_field_meter(document, index)
  end
  for index = 0, field_needs - 1 do
    document
      :get("field-need-" .. index)
      :set_class("active", ui.setting("hud.need." .. index .. ".active") == true)
  end
  for index = 0, field_pools - 1 do
    draw_field_pool(document, index)
  end
  pick_bubble(document)
end

-- The shop job's bar (shop_job_bar, an engine improvement, on unless turned off): while the canyon
-- shop's job runs the game publishes shop_job.* (apps/game: shop_job_bar.hpp) and bumps
-- shop_job.revision. The bar runs from nothing to the tally the shopkeeper joins from
-- (shop_job.goal), with a mark where the better pay starts (shop_job.better_pay) and, under it,
-- the next mark to reach; #shop-job takes the class "shown", and "better" or "joins" once the
-- tally reaches a mark.
local function draw_shop_job(document)
  local panel = document:get("shop-job")
  local shown = ui.setting("shop_job.shown") == true
  panel:set_class("shown", shown)
  if not shown then
    return
  end
  local tally, goal = field_number("shop_job.tally"), field_number("shop_job.goal")
  local better = field_number("shop_job.better_pay")
  document
    :get("shop-job-fill")
    :set_property("width", string.format("%.2f%%", percent(tally, 0, goal)))
  local mark = string.format("%.2f%%", percent(better, 0, goal))
  document:get("shop-job-mark-pay"):set_property("left", mark)
  panel:set_class("better", tally >= better and tally < goal)
  panel:set_class("joins", tally >= goal)
  local next_mark
  if tally >= goal then
    next_mark = t("hud.shop_job.joins")
  elseif tally >= better then
    next_mark = t("hud.shop_job.join_from", { tally = goal })
  else
    next_mark = t("hud.shop_job.better_pay", { tally = better })
  end
  document:get("shop-job-next"):set_text(next_mark)
  document:get("shop-job-customers"):set_text(t("hud.shop_job.customers", {
    served = field_number("shop_job.customer"),
    total = field_number("shop_job.customers"),
  }))
  document:get("shop-job-tally"):set_text(t("hud.shop_job.tally", { tally = tally, goal = goal }))
end

function on_show(document)
  refresh(document)
  draw_digital(document)
  drawn_time = nil
  refresh_fade(document)
  refresh_title(document)
  refresh_prompts(document)
  draw_field_labels(document)
  draw_field(document)
  draw_shop_job(document)
end

function on_device(document)
  refresh_prompts(document)
end

-- The language changed: the elements with a text-key are already in the new one; the prompts, the
-- map's name and the field status's texts are written here.
function on_language(document)
  refresh_title(document)
  refresh_prompts(document)
  draw_field_labels(document)
  drawn_time = nil
  draw_field_clock(document)
  draw_shop_job(document)
end

function on_setting(document, name)
  if name == "screen_fade" then
    refresh_fade(document)
  elseif name == "map_title_key" then
    refresh_title(document)
  elseif name == "hud.revision" then
    draw_field(document)
  elseif name == "hud.partner.revision" or name == "need_bubble_stack" then
    draw_bubble(document)
  elseif name == "digital_time" then
    draw_digital(document)
  elseif name == "shop_job.revision" then
    draw_shop_job(document)
  elseif name == "ui_skin" then
    -- A new skin may set or clear classes the drawing reads (pips): draw the field status and the
    -- bubble again.
    refresh(document)
    draw_field(document)
  elseif name == "hud.minimap_covered" or name == "hud.minimap_allowed" then
    refresh(document)
  elseif string.find(name, "^hud%.") or string.find(name, "^shop_job%.") then
    if string.find(name, "%.label$") then
      draw_field_labels(document)
    end
  else
    refresh(document)
  end
end

function on_tick(document, seconds)
  tick_bubble(document, seconds)
end
