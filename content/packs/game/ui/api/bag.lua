-- The UI API's bag (docs/modding/ui-api.md, namespace bag): the bag as the game publishes it while
-- a screen shows it, and the commands that ask the game to use, drop, move or sort its items. A
-- command only asks: the game checks it again and answers with bag.changed and bag.result.
--
-- Every function returns a fresh table; changing it changes nothing in the game.

bag = {}

-- How many slots the bag has (the ruleset's size).
function bag.count()
  local count = ui.setting("bag.slot.count")
  if type(count) ~= "number" then
    return 0
  end
  return math.floor(count)
end

-- The player's money.
function bag.money()
  local money = ui.setting("bag.money")
  if type(money) ~= "number" then
    return 0
  end
  return math.floor(money)
end

local fields = {
  empty = true,
  name = "",
  description = "",
  description_species = "",
  count = 0,
  dimmed = false,
  usable = false,
  discardable = false,
  item_color = 255,
  icon = "",
  icon_rect = "",
  favorite = false,
}

-- One field of a slot (from 0) as published, or its fallback.
function bag.field(index, name)
  local value = ui.setting("bag.slot." .. index .. "." .. name)
  if value == nil then
    return fields[name]
  end
  return value
end

-- The slot (from 0): empty, name (text key), description (text key) and description_species (the
-- species' name key it names), count, dimmed (an item the bag never uses), usable and discardable
-- here, item_color (0 field, 1 battle, 2 both, 255 none), icon and icon_rect (an image and its
-- part), favorite (the partner's favourite food). A slot past the bag's size is empty.
function bag.slot(index)
  local slot = {}
  for name, fallback in pairs(fields) do
    slot[name] = index < bag.count() and bag.field(index, name) or fallback
  end
  return slot
end

-- The last action's answer: its message's text key and the item, or nil before any.
function bag.result()
  local key = ui.setting("bag.result")
  if type(key) ~= "string" or key == "" then
    return nil
  end
  local item = ui.setting("bag.result_item")
  return { key = key, item = type(item) == "string" and item or "" }
end

local function slot_index(index)
  return type(index) == "number"
    and index == math.floor(index)
    and index >= 0
    and index < bag.count()
end

local function filled(index)
  return slot_index(index) and bag.field(index, "empty") ~= true
end

-- Asks the game to use the item of a slot. Returns true when the request is queued, or false and
-- why.
function bag.use(index)
  if not filled(index) then
    return false, "no item in that slot"
  end
  ui.send("bag_use", index)
  return true
end

function bag.drop(index)
  if not filled(index) then
    return false, "no item in that slot"
  end
  ui.send("bag_drop", index)
  return true
end

-- Asks the game to swap two slots.
function bag.move(from, to)
  if not slot_index(from) or not slot_index(to) then
    return false, "both slots must be in the bag"
  end
  ui.send("bag_move", string.format("%d %d", from, to))
  return true
end

-- Asks the game to sort the bag in one of its orders (from 0).
function bag.sort(order)
  if type(order) ~= "number" or order ~= math.floor(order) or order < 0 or order > 15 then
    return false, "order must be a whole number from 0 to 15"
  end
  ui.send("bag_sort", order)
  return true
end

-- bag.changed: the bag changed; bag.result: the game answered an action.
event_source("bag.revision", "bag.changed")
event_source("bag.result", "bag.result")
