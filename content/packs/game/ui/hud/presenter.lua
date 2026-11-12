-- The HUD's presenter: what the field HUD shows, as plain tables, from the data the game publishes
-- for it (docs/modding/ui-api.md, screen hud). It touches no element and knows no skin: view.lua
-- draws its tables. The rules (night, the part of the day, a bar's low half) are the game's, which
-- publishes them with the clock and the bars (packages/screens: field_hud_view.hpp); only the need
-- bubble's turns are counted here, as the original's bubble counts its frames.
--
-- Everything is in the table `hud` of the document's environment.

hud = {}

-- The lists the game publishes, in its order (field_hud_view.hpp): happiness and discipline;
-- hunger, sleep, toilet, tiredness, injury and sickness; HP and MP.
hud.meter_count = 2
hud.need_count = 6
hud.pool_count = 2

local function number(name)
  local value = ui.setting(name)
  if type(value) ~= "number" then
    return 0
  end
  return math.floor(value)
end

local function text_key(name)
  local key = ui.setting(name)
  if type(key) ~= "string" then
    return ""
  end
  return key
end

hud.minimap_sizes = { small = true, medium = true, large = true }

-- The round minimap (an engine improvement) shows unless the player turns it off (minimap: off; on,
-- and auto from older settings, show it). It also hides while the game says the field is not the
-- player's to walk (hud.minimap_allowed false: battles, scenes and talk, minigames) and in a battle
-- (its HP bars and end box take that corner). The game hides it while a window lies over it
-- (hud.minimap_covered), whatever the skin.
function hud.minimap()
  local size = ui.setting("minimap_size")
  return {
    hidden = ui.setting("minimap") == "off" or ui.setting("minimap") == false or ui.setting(
      "battle.active"
    ) == true or ui.setting("hud.minimap_allowed") == false,
    covered = ui.setting("hud.minimap_covered") == true,
    size = hud.minimap_sizes[size] and size or "medium",
  }
end

-- How much of the panels shows through the map change's fade (screen_fade, 0 to 1).
function hud.opacity()
  local level = ui.setting("screen_fade")
  if type(level) ~= "number" then
    level = 0
  end
  return 1 - math.min(math.max(level, 0), 1)
end

-- The text key of the map's name to show on arrival, or nil.
function hud.map_title()
  local key = text_key("map_title_key")
  return key ~= "" and key or nil
end

-- The time in digits (digital_time, an engine improvement, on unless turned off).
function hud.digital_time()
  return ui.setting("digital_time") ~= false
end

-- The labels of the bars, the needs and the pools, as text keys ("" for none), from 0.
function hud.labels()
  local labels = { meters = {}, needs = {}, pools = {} }
  for index = 0, hud.meter_count - 1 do
    labels.meters[index] = text_key("hud.meter." .. index .. ".label")
  end
  for index = 0, hud.need_count - 1 do
    labels.needs[index] = text_key("hud.need." .. index .. ".label")
  end
  for index = 0, hud.pool_count - 1 do
    labels.pools[index] = text_key("hud.pool." .. index .. ".label")
  end
  return labels
end

-- The game clock: hour, minute, day, year, and the game's night and part of the day. Until the game
-- publishes it the clock reads 0:00, which is night.
function hud.clock()
  local phase = ui.setting("hud.clock.phase")
  return {
    hour = number("hud.clock.hour"),
    minute = number("hud.clock.minute"),
    day = number("hud.clock.day"),
    year = number("hud.clock.year"),
    night = ui.setting("hud.clock.night") ~= false,
    phase = type(phase) == "string" and phase or "night",
  }
end

-- The classes of hud.partner.area's values, the original's numbers: 1 the partner likes the area, 2
-- it dislikes it, 0 (none) neither.
local areas = { [1] = "liked", [2] = "disliked" }

-- The field status: whether it shows, how the partner feels about the map ("liked", "disliked" or
-- nil), the clock, the bars (each with `low`, the game's mood), the needs the partner has and HP
-- and MP, from 0.
function hud.field()
  local state = {
    shown = ui.setting("hud.shown") == true,
    area = areas[ui.setting("hud.partner.area")],
    clock = hud.clock(),
    meters = {},
    needs = {},
    pools = {},
  }
  for index = 0, hud.meter_count - 1 do
    local base = "hud.meter." .. index .. "."
    state.meters[index] = {
      value = number(base .. "value"),
      min = number(base .. "min"),
      max = number(base .. "max"),
      two_tone = ui.setting(base .. "two_tone") == true,
      low = ui.setting(base .. "mood") == "low",
    }
  end
  for index = 0, hud.need_count - 1 do
    state.needs[index] = ui.setting("hud.need." .. index .. ".active") == true
  end
  for index = 0, hud.pool_count - 1 do
    local base = "hud.pool." .. index .. "."
    state.pools[index] = { value = number(base .. "value"), max = number(base .. "max") }
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
  for need, index in pairs(bubble_need_index) do
    local base = "hud.need." .. index .. "."
    local urgency = ui.setting(base .. "urgency")
    by_id[need] = {
      active = ui.setting(base .. "active") == true,
      urgency = type(urgency) == "number" and urgency or 0,
    }
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
  if ui.setting("need_bubble_stack") == false then
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

-- Where the partner's head is: visible, x and y in percent of the window, and the evolving bubble
-- (bubble.c: CONDITION_ICON_EVOLVING), which the partner shows once as it starts to evolve
-- (main/evl.c: tickEvoSequenceLoading): the game steps it with the evolution's frames and publishes
-- its frame, 1 to 51 (the burst), as hud.partner.evolving (0, here nil, for none).
local function head()
  local x, y = ui.setting("hud.partner.x"), ui.setting("hud.partner.y")
  local evolving = ui.setting("hud.partner.evolving")
  return {
    visible = ui.setting("hud.partner.visible") == true,
    x = type(x) == "number" and x or 0,
    y = type(y) == "number" and y or 0,
    evolving = (type(evolving) == "number" and evolving >= 1) and math.floor(evolving) or nil,
  }
end

-- The bubble as it is now: whether the partner is on the window, where its head is (x and y in
-- percent of the window), the evolving frame or nil, the need it shows or nil, the needs stacked in
-- it (more than one, or empty) and its frame, 1 to 60.
function hud.bubble()
  local partner_head = head()
  local evolving = partner_head.evolving
  local stack = evolving and {} or hud.stacked_needs()
  return {
    visible = partner_head.visible,
    x = partner_head.x,
    y = partner_head.y,
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
  local partner_head = head()
  if bubble_need == nil or not partner_head.visible or partner_head.evolving then
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
-- shop's job runs the game publishes shop_job.* (apps/game: shop_job_bar.hpp). The bar runs from
-- nothing to the tally the shopkeeper joins from (goal), with a mark where the better pay starts.
function hud.shop_job()
  return {
    shown = ui.setting("shop_job.shown") == true,
    tally = number("shop_job.tally"),
    goal = number("shop_job.goal"),
    better_pay = number("shop_job.better_pay"),
    customer = number("shop_job.customer"),
    customers = number("shop_job.customers"),
  }
end
