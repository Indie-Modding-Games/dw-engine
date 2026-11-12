-- The HUD's view: draws what presenter.lua says on the document. Under the round minimap it writes
-- the game's day and time (#minimap-day, #minimap-time) and the part of the day (#minimap-phase,
-- class "phase-<part>"). Its menu button shows the control that opens the pause menu with the last
-- device used. On a map change the panels fade out with the game's fade, as the original's fade
-- covers its HUD, and the name of the map it goes to shows in front of the fade.
--
-- The field status redraws once per change of the game's field state (hud.revision), so a frame
-- where nothing changed writes nothing. The labels come once, as text keys. While a menu or a
-- battle stops the field the panel and the menu's hint lose their class "shown" and each skin
-- decides how they leave. Every skin draws this same document and script and differs only in its
-- style sheets: a bar with the class "pips" fills in the original's 25 steps, and the dial
-- (#field-clock-dial) and the class "night" on #field-clock are there for a skin that draws the
-- original's clock; the class "digital" on #field-clock asks for the time in figures (#field-time)
-- too. #field-partner-bubble hangs over the partner's head with the class of the need the
-- original's bubble would show, or, with several needs and need_bubble_stack, the class "stack-2"
-- to "stack-5"; its icons #field-partner-bubble-icon-<i> carry the needs it shows, the most urgent
-- first, and the class "frame-<n>" says which of the original's frames the bubble is on, for the
-- skin; as the partner starts to evolve it shows the evolving bubble instead, with the class
-- "evolving" on it and on its first icon. How the partner feels about the map puts the class
-- "area-liked" or "area-disliked" on #field-status, where a skin can swap the happiness face as the
-- original does.

local function refresh(document)
  local state = hud.minimap()
  local minimap = document:get("minimap")
  minimap:set_class("hidden", state.hidden)
  minimap:set_class("covered", state.covered)
  for name in pairs(hud.minimap_sizes) do
    minimap:set_class("size-" .. name, name == state.size)
  end
end

local function refresh_prompts(document)
  document:get("prompt-menu"):set_prompt("menu")
end

local panels = { "minimap", "menu-button", "field-status" }

local function refresh_fade(document)
  local opacity = string.format("%.3f", hud.opacity())
  for _, id in ipairs(panels) do
    document:get(id):set_property("opacity", opacity)
  end
end

local function refresh_title(document)
  local key = hud.map_title()
  local element = document:get("map-title")
  element:set_text(key and t(key) or "")
  element:set_class("shown", key ~= nil)
end

local function label(key)
  return key ~= "" and t(key) or ""
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
-- minute; the hour dot goes once a day round a 24-hour ring, 15 degrees an hour, hour 0 at the top.
local minute_hand_degrees = 6
local hour_dot_degrees = 15
local hour_dot_radius_percent = 50

local phases = { "morning", "day", "late", "night" }

local function two_digits(value)
  return string.format("%02d", value)
end

local function draw_field_labels(document)
  local labels = hud.labels()
  for index = 0, hud.meter_count - 1 do
    document:get("field-meter-" .. index .. "-label"):set_text(label(labels.meters[index]))
  end
  for index = 0, hud.need_count - 1 do
    document:get("field-need-" .. index):set_text(label(labels.needs[index]))
  end
  for index = 0, hud.pool_count - 1 do
    document:get("field-pool-" .. index .. "-label"):set_text(label(labels.pools[index]))
  end
end

local function draw_field_dial(document, clock)
  document:get("field-clock"):set_class("night", clock.night)
  document
    :get("field-clock-minute-hand")
    :set_property("transform", string.format("rotate(%ddeg)", clock.minute * minute_hand_degrees))
  local angle = math.rad(clock.hour * hour_dot_degrees)
  local dot = document:get("field-clock-hour-dot")
  dot:set_property("left", string.format("%.2f%%", 50 + hour_dot_radius_percent * math.sin(angle)))
  dot:set_property("top", string.format("%.2f%%", 50 - hour_dot_radius_percent * math.cos(angle)))
end

-- The time in figures as last written, so a redraw for another value leaves it alone and it is only
-- written again when the minute changes (or the language, which clears it).
local drawn_time = nil

local function draw_digital(document)
  document:get("field-clock"):set_class("digital", hud.digital_time())
end

local function draw_field_clock(document, clock)
  draw_field_dial(document, clock)
  local time = t("hud.clock.time", {
    hour = two_digits(clock.hour),
    minute = two_digits(clock.minute),
  })
  if time ~= drawn_time then
    document:get("field-time"):set_text(time)
    document:get("minimap-time"):set_text(time)
    -- the day-part chip answers on the hour, as the old dial's sun and moon did on the minute
    local phase_chip = document:get("minimap-phase")
    for _, phase in ipairs(phases) do
      phase_chip:set_class("phase-" .. phase, clock.phase == phase)
    end
    drawn_time = time
  end
  document:get("minimap-day"):set_text(t("hud.minimap.day", { day = clock.day }))
  document:get("field-date"):set_text(t("hud.clock.date", { day = clock.day, year = clock.year }))
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

-- A two-tone bar fills from the minimum to the middle of its range, then again in its second colour
-- from the middle to the maximum, as the original's field bars (partner_impl.c: renderStatusBars).
-- Below the middle the original swaps the bar's icon, so a skin draws its icons on the class "low"
-- of the bar's row.
local function draw_field_meter(document, index, meter)
  local prefix = "field-meter-" .. index
  local bar = document:get(prefix .. "-bar")
  local fill, high = bar_fill(bar, meter.value, meter.min, meter.max), 0
  if meter.two_tone then
    local middle = (meter.min + meter.max) / 2
    fill = bar_fill(bar, meter.value, meter.min, middle)
    high = bar_fill(bar, meter.value, middle, meter.max)
  end
  document:get(prefix .. "-fill"):set_property("width", fill .. "%")
  document:get(prefix .. "-high"):set_property("width", high .. "%")
  document:get(prefix):set_class("low", meter.low)
  document:get(prefix .. "-value"):set_text(tostring(meter.value))
end

local function draw_field_pool(document, index, pool)
  local bar = document:get("field-pool-" .. index .. "-bar")
  document
    :get("field-pool-" .. index .. "-fill")
    :set_property("width", bar_fill(bar, pool.value, 0, pool.max) .. "%")
  document:get("field-pool-" .. index .. "-value"):set_text(pool.value .. " / " .. pool.max)
end

-- The room a stacked balloon keeps from the window's sides, in dp.
local bubble_margin_dp = 6

-- A stacked balloon is wider than the original's: over a partner near a side of the window it moves
-- in, as little as it needs, so it stays whole. The shift goes on its margin, which its transforms
-- (the anchor at its bottom centre, the growth) leave alone; the original's bubble never moves. The
-- balloon's width is the skin's, read from its layout, so a change of size lands on the next tick.
local function keep_bubble_inside(document, bubble, stacked, x)
  local shift = 0
  local _, _, window_width = document:rect()
  local _, _, width = bubble:rect()
  local room = window_width - 2 * bubble_margin_dp
  if stacked and width > 0 and width <= room then
    local centre = x / 100 * window_width
    local half = width / 2
    local wanted =
      math.max(bubble_margin_dp + half, math.min(centre, bubble_margin_dp + room - half))
    shift = wanted - centre
  end
  local value = string.format("%.1fdp", shift)
  if bubble:attribute("data-shift") ~= value then
    bubble:set_attribute("data-shift", value)
    bubble:set_property("margin-left", value)
  end
end

-- The frame the bubble shows, as its class "frame-<n>".
local bubble_frame = nil

local function draw_bubble_frame(bubble, frame)
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
  local state = hud.bubble()
  local bubble = document:get("field-partner-bubble")
  local evolving = state.evolving ~= nil
  bubble:set_property("left", string.format("%.2f%%", state.x))
  bubble:set_property("top", string.format("%.2f%%", state.y))
  bubble:set_class("shown", state.visible and (evolving or state.need ~= nil))
  bubble:set_class("evolving", evolving)
  draw_bubble_frame(bubble, state.frame)
  -- One need alone is the original's bubble either way: its need on the bubble and on the first
  -- icon. The evolving bubble shows no need.
  local count = #state.stack
  for size = 2, hud.bubble_stack_most do
    bubble:set_class("stack-" .. size, count == size)
  end
  for need in pairs(hud.bubble_needs) do
    bubble:set_class(need, not evolving and count == 0 and need == state.need)
  end
  local icons = count > 0 and state.stack or { not evolving and state.need or nil }
  for index = 0, hud.bubble_stack_most - 1 do
    local icon = document:get("field-partner-bubble-icon-" .. index)
    for need in pairs(hud.bubble_needs) do
      icon:set_class(need, icons[index + 1] == need)
    end
    icon:set_class("evolving", evolving and index == 0)
  end
  keep_bubble_inside(document, bubble, count > 0, state.x)
end

local function pick_bubble(document)
  hud.pick_bubble()
  draw_bubble(document)
end

local function tick_bubble(document, seconds)
  local step = hud.tick_bubble(seconds)
  if step == nil then
    return
  end
  local bubble = document:get("field-partner-bubble")
  local state = hud.bubble()
  keep_bubble_inside(document, bubble, #state.stack > 0, state.x)
  if step == "need" then
    draw_bubble(document)
  else
    draw_bubble_frame(bubble, state.frame)
  end
end

local function draw_field(document)
  local field = hud.field()
  local status = document:get("field-status")
  status:set_class("shown", field.shown)
  document:get("menu-button"):set_class("shown", field.shown)
  status:set_class("area-liked", field.area == "liked")
  status:set_class("area-disliked", field.area == "disliked")
  draw_field_clock(document, field.clock)
  for index = 0, hud.meter_count - 1 do
    draw_field_meter(document, index, field.meters[index])
  end
  for index = 0, hud.need_count - 1 do
    document:get("field-need-" .. index):set_class("active", field.needs[index])
  end
  for index = 0, hud.pool_count - 1 do
    draw_field_pool(document, index, field.pools[index])
  end
  pick_bubble(document)
end

-- The shop job's bar: #shop-job takes the class "shown", and "better" or "joins" once the tally
-- reaches a mark; under it, the next mark to reach.
local function draw_shop_job(document)
  local job = hud.shop_job()
  local panel = document:get("shop-job")
  panel:set_class("shown", job.shown)
  if not job.shown then
    return
  end
  document
    :get("shop-job-fill")
    :set_property("width", string.format("%.2f%%", percent(job.tally, 0, job.goal)))
  local mark = string.format("%.2f%%", percent(job.better_pay, 0, job.goal))
  document:get("shop-job-mark-pay"):set_property("left", mark)
  panel:set_class("better", job.tally >= job.better_pay and job.tally < job.goal)
  panel:set_class("joins", job.tally >= job.goal)
  local next_mark
  if job.tally >= job.goal then
    next_mark = t("hud.shop_job.joins")
  elseif job.tally >= job.better_pay then
    next_mark = t("hud.shop_job.join_from", { tally = job.goal })
  else
    next_mark = t("hud.shop_job.better_pay", { tally = job.better_pay })
  end
  document:get("shop-job-next"):set_text(next_mark)
  document:get("shop-job-customers"):set_text(t("hud.shop_job.customers", {
    served = job.customer,
    total = job.customers,
  }))
  document
    :get("shop-job-tally")
    :set_text(t("hud.shop_job.tally", { tally = job.tally, goal = job.goal }))
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
  draw_field_clock(document, hud.clock())
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
  elseif name == "minimap" or name == "minimap_size" or name == "battle.active" then
    -- Only what refresh reads: a menu publishes hundreds of settings as it opens, and refreshing
    -- the HUD for each of them stalled that frame (#221).
    refresh(document)
  end
end

function on_tick(document, seconds)
  tick_bubble(document, seconds)
end
