-- The fishing's bait menu (fish.c: FISH_tickBaitMenu, the food of the bag in its order). The game
-- publishes the baits as settings (fishing_baits, the count; fishing_bait_name.<n>, the item's name
-- key, and fishing_bait_count.<n>, how many the bag holds, from 0) and shows this document; it
-- sends choose_bait with the bait, from 0, or no_bait. Pages of eight, turned with left and right.

local PageSize = 8
local page = 1

local function count()
  return math.floor(ui.setting("fishing_baits") or 0)
end

local function pages()
  return math.max(1, math.ceil(count() / PageSize))
end

-- The bait of a slot of the current page, from 0, or nil when the slot is empty.
local function bait_of(slot)
  local bait = (page - 1) * PageSize + slot - 1
  if bait < count() then
    return bait
  end
  return nil
end

local function label_of(bait)
  local key = ui.setting("fishing_bait_name." .. bait) or ""
  local held = math.floor(ui.setting("fishing_bait_count." .. bait) or 0)
  return t("fishing.bait.entry", { item = key ~= "" and t(key) or "?", count = held })
end

local function refresh(document)
  for slot = 1, PageSize do
    local bait = bait_of(slot)
    local entry = document:get("bait-" .. slot)
    entry:set_class("hidden", bait == nil)
    if bait ~= nil then
      entry:set_text(label_of(bait))
    end
  end
  local several = pages() > 1
  document:get("previous"):set_class("hidden", not several)
  document:get("next"):set_class("hidden", not several)
  document:get("page"):set_class("hidden", not several)
  document:get("page"):set_text(t("exploration.page", { page = page, pages = pages() }))
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

function choose(slot)
  local bait = bait_of(slot)
  if bait ~= nil then
    ui.send("choose_bait", bait)
  end
end

function none()
  ui.send("no_bait", true)
end

function turn(document, step)
  page = ((page - 1 + step) % pages()) + 1
  refresh(document)
  document:get("bait-1"):focus()
end

function on_show(document)
  page = 1
  refresh(document)
  refresh_prompts(document)
  document:get(count() > 0 and "bait-1" or "none"):focus()
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

-- Cancel and menu put the rod away; left and right turn the page.
function on_action(document, action)
  if action == "cancel" or action == "menu" then
    none()
    return true
  end
  if (action == "left" or action == "right") and pages() > 1 then
    turn(document, action == "left" and -1 or 1)
    return true
  end
  return false
end
