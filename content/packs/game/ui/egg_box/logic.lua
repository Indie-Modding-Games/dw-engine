-- The egg box after a death (doo2.c: DOO2_openEggBox, DOO2_tickEggInput). The game publishes the
-- number of eggs (egg_box_eggs) and shows this document; it sends egg_box_move with the egg, from
-- 0, when the egg changes, and egg_box_choose with the egg taken. Right moves to the next egg and
-- left to the one before, around the box; the box opens on the first egg and cannot be left
-- without one.

local Slots = 8
local egg = 0

local function count()
  return math.min(Slots, math.floor(ui.setting("egg_box_eggs") or 0))
end

local function refresh(document)
  for slot = 1, Slots do
    local entry = document:get("egg-" .. slot)
    entry:set_class("hidden", slot > count())
    entry:set_text(t("egg_box.egg", { number = slot }))
  end
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
end

local function focus(document)
  if count() > 0 then
    document:get("egg-" .. (egg + 1)):focus()
  end
end

function choose(slot)
  if slot <= count() then
    ui.send("egg_box_choose", slot - 1)
  end
end

local function move(document, step)
  if count() == 0 then
    return
  end
  egg = (egg + step) % count()
  focus(document)
  ui.send("egg_box_move", egg)
end

function on_show(document)
  egg = 0
  refresh(document)
  refresh_prompts(document)
  focus(document)
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

-- Left and right (and up and down) move the egg; cancel and menu do nothing: an egg must be taken.
function on_action(document, action)
  if action == "left" or action == "up" then
    move(document, -1)
    return true
  end
  if action == "right" or action == "down" then
    move(document, 1)
    return true
  end
  if action == "confirm" then
    choose(egg + 1)
    return true
  end
  return action == "cancel" or action == "menu"
end
