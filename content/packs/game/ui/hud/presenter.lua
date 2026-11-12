-- The HUD's presenter: what the field HUD shows, as plain tables, from the UI API only (clock,
-- partner, field, options: docs/modding/ui-api.md). It touches no element and knows no skin:
-- view.lua draws its tables. The rules (night, the part of the day, a bar's low half) are the
-- game's; only the need bubble's turns are counted here, as the original's bubble counts its
-- frames.
--
-- Everything is in the table `hud` of the document's environment.

hud = {}

-- The lists the game publishes, in its order (field_hud_view.hpp): happiness and discipline;
-- hunger, sleep, toilet, tiredness, injury and sickness; HP and MP.
hud.meter_count = 2
hud.need_count = 6
hud.pool_count = 2

hud.minimap_sizes = { small = true, medium = true, large = true }

-- The round minimap (an engine improvement): hidden unless the player's option and the field allow
-- it (field.minimap), covered while a window lies over it, and its size.
function hud.minimap()
  local minimap = field.minimap()
  return { hidden = not minimap.shown, covered = minimap.covered, size = minimap.size }
end

-- How much of the panels shows through the map change's fade.
function hud.opacity()
  return 1 - field.fade()
end

-- The text key of the map's name to show on arrival, or nil.
function hud.map_title()
  return field.map_title()
end

-- The time in digits (digital_time, an engine improvement, on unless turned off).
function hud.digital_time()
  return options.get("digital_time") ~= false
end

-- The labels of the bars, the needs and the pools, as text keys ("" for none), from 0.
function hud.labels()
  local status, needs = partner.status(), partner.needs()
  local labels = { meters = {}, needs = {}, pools = {} }
  for index = 0, hud.meter_count - 1 do
    labels.meters[index] = status.meters[index + 1].label_key
  end
  for index = 0, hud.need_count - 1 do
    labels.needs[index] = needs[index + 1].label_key
  end
  for index = 0, hud.pool_count - 1 do
    labels.pools[index] = status.pools[index + 1].label_key
  end
  return labels
end

-- The game clock (clock.now): hour, minute, day, year, night and the part of the day.
function hud.clock()
  return clock.now()
end

-- The field status: whether it shows (while the game time runs), how the partner feels about the
-- map ("liked", "disliked" or nil), the clock, the bars (each with `low` for its mood), the needs
-- the partner has and HP and MP, from 0.
function hud.field()
  local now, status, needs = clock.now(), partner.status(), partner.needs()
  local state = {
    shown = now.running,
    area = status.area,
    clock = now,
    meters = {},
    needs = {},
    pools = {},
  }
  for index = 0, hud.meter_count - 1 do
    local meter = status.meters[index + 1]
    state.meters[index] = {
      value = meter.value,
      min = meter.min,
      max = meter.max,
      two_tone = meter.two_tone,
      low = meter.mood == "low",
    }
  end
  for index = 0, hud.need_count - 1 do
    state.needs[index] = needs[index + 1].active
  end
  for index = 0, hud.pool_count - 1 do
    state.pools[index] = status.pools[index + 1]
  end
  return state
end

-- The need bubble over the partner (bubble.c), one need at a time. The original picks it in this
-- order, highest first (partner_impl.c: handleConditionBubble checks them from the lowest and the
-- last match wins), and while a bubble has shown for 50 of its 20-a-second frames it swaps to the
-- highest other need the partner has, if any, so two needs take turns; a third waits until one of
-- them goes. Injury and sickness come last, below the toilet.
local bubble_priority = { "sleep", "tired", "hunger", "toilet", "injured", "sick" }
local bubble_need_index = { hunger = 0, sleep = 1, toilet = 2, tired = 3, injured = 4, sick = 5 }
hud.bubble_needs = bubble_need_index

-- The partner's needs by id ({active, urgency}), read once per question.
local function needs_by_id()
  local by_id = {}
  for _, need in ipairs(partner.needs()) do
    by_id[need.id] = need
  end
  return by_id
end
-- A bubble's frames (bubble.c: tickConditionBubble, renderConditionBubble): the game makes it and
-- it is drawn on frames 1 to 51, growing on the first 10 and bursting on the 51st, then it is gone
-- until the game makes it again 60 frames after the last time (handleConditionBubble:
-- CONDITION_BUBBLE_TIMER), or at 50 for the other need's bubble. The presenter counts them; the
-- skin draws each frame, the growth, the need's own animation, the burst and the gap. They stop
-- while the bubble is not drawn (renderConditionBubble and tickConditionBubble do nothing in a
-- menu).
local bubble_frames_per_second = 20
local bubble_swap_frames = 50
local bubble_cycle_frames = 60
-- need_bubble_stack: up to this many needs at once, side by side in one balloon, the most urgent
-- first.
hud.bubble_stack_most = 5
-- No bubble when the game starts (bubble.c: initializeConditionBubbles marks every bubble free).
local bubble_need = nil
local bubble_seconds = 0

local function need_active(need)
  local found = needs_by_id()[need]
  return found ~= nil and found.active
end

-- The highest need the partner has other than `except`, or nil.
local function highest_need(except)
  local needs = needs_by_id()
  for _, need in ipairs(bubble_priority) do
    if need ~= except and needs[need] ~= nil and needs[need].active then
      return need
    end
  end
  return nil
end

-- How urgent a need is: the game ranks the needs the partner has by how soon each does its lasting
-- harm if left alone (hud.need.<i>.urgency, 1 the soonest); a need without a rank goes after the
-- ranked ones.
local function urgency_of(needs, need)
  local rank = needs[need] and needs[need].urgency or 0
  if rank <= 0 then
    return math.huge
  end
  return rank
end

-- The needs the stacked bubble shows (need_bubble_stack, on unless turned off): the partner's, most
-- urgent first, up to bubble_stack_most; empty when off. Needs of the same urgency keep the
-- original bubble's order.
function hud.stacked_needs()
  local needs = {}
  if options.get("need_bubble_stack") == false then
    return needs
  end
  local by_id = needs_by_id()
  local order = {}
  for place_in_order, need in ipairs(bubble_priority) do
    order[need] = place_in_order
    if by_id[need] ~= nil and by_id[need].active then
      needs[#needs + 1] = need
    end
  end
  table.sort(needs, function(first, second)
    local first_rank, second_rank = urgency_of(by_id, first), urgency_of(by_id, second)
    if first_rank ~= second_rank then
      return first_rank < second_rank
    end
    return order[first] < order[second]
  end)
  for index = #needs, hud.bubble_stack_most + 1, -1 do
    needs[index] = nil
  end
  return needs
end

-- The bubble as it is now: whether the partner is on the window, where its head is (x and y in
-- percent of the window), the evolving frame or nil (bubble.c: CONDITION_ICON_EVOLVING, which the
-- partner shows once as it starts to evolve; the game steps it, 1 to 51), the need it shows or nil,
-- the needs stacked in it (more than one, or empty) and its frame, 1 to 60.
function hud.bubble()
  local head = partner.bubble()
  local evolving = head.evolving
  local stack = evolving and {} or hud.stacked_needs()
  return {
    visible = head.visible,
    x = head.x,
    y = head.y,
    evolving = evolving,
    need = bubble_need,
    stack = #stack > 1 and stack or {},
    frame = evolving or math.floor(bubble_seconds * bubble_frames_per_second) + 1,
  }
end

-- The needs changed: a bubble whose need went gives way to the highest one left, in a new bubble.
function hud.pick_bubble()
  if bubble_need == nil or not need_active(bubble_need) then
    bubble_need = highest_need(nil)
    bubble_seconds = 0
  end
end

-- Counts the bubble's time. Returns "need" when the bubble swapped to the other need (draw it all
-- again), "frame" when only its frame may have moved, nil when it does not run.
function hud.tick_bubble(seconds)
  -- The game steps the evolving bubble itself.
  local head = partner.bubble()
  if bubble_need == nil or not head.visible or head.evolving then
    return nil
  end
  bubble_seconds = bubble_seconds + seconds
  local swap = bubble_swap_frames / bubble_frames_per_second
  local cycle = bubble_cycle_frames / bubble_frames_per_second
  -- The stacked bubble already shows the other needs: it only starts again.
  local stacked = #hud.stacked_needs() > 1
  local other = not stacked and highest_need(bubble_need) or nil
  if other ~= nil and bubble_seconds >= swap then
    bubble_need = other
    bubble_seconds = 0
    return "need"
  end
  while bubble_seconds >= cycle do
    bubble_seconds = bubble_seconds - cycle
  end
  return "frame"
end

-- The shop job's bar (shop_job_bar, an engine improvement, on unless turned off): while the canyon
-- shop's job runs (field.shop_job), the bar runs from nothing to the tally the shopkeeper joins
-- from (goal), with a mark where the better pay starts.
function hud.shop_job()
  local job = field.shop_job()
  if job == nil then
    return { shown = false, tally = 0, goal = 0, better_pay = 0, customer = 0, customers = 0 }
  end
  job.shown = true
  return job
end
