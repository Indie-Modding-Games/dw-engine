-- The egg box after a death (doo2.c: DOO2_openEggBox, DOO2_tickEggInput). The game publishes the
-- number of eggs (egg_box_eggs) and shows this document; it sends egg_box_move with the egg, from
-- 0, when the egg changes, and egg_box_choose with the egg taken. Right moves to the next egg and
-- left to the one before, around the box; the box opens on the first egg and cannot be left
-- without one. Once the egg is taken and the box has shrunk away it sends egg_box_closed.
--
-- In the original look (ui/common/original_box.lua) the box grows out of the 16-pixel square in
-- the middle of the screen (DOO2_openEggBox: MAIN_D_80134B98) and shrinks back into it once an egg
-- is taken (MAIN_D_80134BA4); the eggs move and are taken only while it stands (DOO2_tickEggInput
-- waits for DOO2_tickEggBox). It opens without the box sound: the game plays the egg box's own, and
-- the document is own-sounds, so moving and taking an egg sound only the game's egg box sounds.

local Slots = 8
local egg = 0
local taken = false
-- One pixel of the original is 3dp.
local dp = 3

-- The 16-pixel square in the middle of the screen.
local function middle_square(document)
  local _, _, width, height = document:rect()
  return { width / 2 - 8 * dp, height / 2 - 8 * dp, 16 * dp, 16 * dp }
end

local function count()
  return math.min(Slots, math.floor(ui.setting("egg_box_eggs") or 0))
end

local function refresh(document)
  for slot = 1, Slots do
    local entry = document:get("egg-" .. slot)
    entry:set_class("hidden", slot > count())
    document:get("egg-" .. slot .. "-name"):set_text(t("egg_box.egg", { number = slot }))
  end
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
end

local function focus(document)
  if count() > 0 then
    document:get("egg-" .. (egg + 1)):focus()
  end
end

function choose(document, slot)
  if taken or original_box.busy() or slot > count() then
    return
  end
  taken = true
  ui.send("egg_box_choose", slot - 1)
  original_box.close(document, "panel", middle_square(document), function()
    ui.send("egg_box_closed", slot - 1)
  end)
end

local function move(document, step)
  if taken or original_box.busy() or count() == 0 then
    return
  end
  egg = (egg + step) % count()
  focus(document)
  ui.send("egg_box_move", egg)
end

function on_show(document)
  egg = 0
  taken = false
  refresh(document)
  refresh_prompts(document)
  focus(document)
  original_box.open(document, "panel", middle_square(document), nil, true)
end

function on_hide(document)
  original_box.cancel(document)
end

function on_tick(document, seconds)
  original_box.tick(document, seconds)
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

-- Left and right move the egg (DOO2_tickEggInput reads only those two); up, down, cancel and menu
-- do nothing: an egg must be taken.
function on_action(document, action)
  if action == "left" then
    move(document, -1)
    return true
  end
  if action == "right" then
    move(document, 1)
    return true
  end
  if action == "confirm" then
    choose(document, egg + 1)
    return true
  end
  return action == "cancel" or action == "menu" or action == "up" or action == "down"
end
