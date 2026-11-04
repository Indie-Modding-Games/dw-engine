-- The fishing's notices (fish.c: FISH_showTextBox): what happened at the hook, and what the player
-- caught. The game publishes fishing_notice.text (a text key), fishing_notice.item (an item's name
-- key, or empty) and fishing_notice.size (a fish's length in centimetres, or 0) and shows this
-- document; it sends fishing_notice_ok. Like the original's text box (FISH_tickTextBox), Confirm
-- closes it once it has been open for 10 of the original's frames (20 a second), Cancel at once,
-- and both sound the confirm cue; the view turns to the notice's point meanwhile (the game's
-- FISH_scrollViewToTarget).

-- FISH_tickTextBox: framesOpen >= 10 before Confirm counts.
local CONFIRM_AFTER_SECONDS = 10 / 20

local open_seconds = 0

-- FISH_tickRod: the length in centimetres, or in metres and centimetres from one metre.
local function length(size)
  if size >= 100 then
    return t("fishing.length_m", { m = math.floor(size / 100), cm = size % 100 })
  end
  return t("fishing.length_cm", { cm = size })
end

local function refresh(document)
  local item_key = ui.setting("fishing_notice.item") or ""
  local item = item_key ~= "" and t(item_key) or ""
  local size = math.floor(ui.setting("fishing_notice.size") or 0)
  document:get("text"):set_text(t(ui.setting("fishing_notice.text") or "", {
    item = item,
    length = size > 0 and length(size) or "",
  }))
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
end

-- FISH_closeTextBox and its playSound(0, 3).
function close_notice()
  ui.cue("confirm")
  ui.send("fishing_notice_ok")
end

function on_show(document)
  open_seconds = 0
  refresh(document)
  refresh_prompts(document)
  document:get("ok"):focus()
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
end

function on_device(document)
  refresh_prompts(document)
end

function on_tick(_document, seconds)
  open_seconds = open_seconds + seconds
end

function on_action(_document, action)
  if action == "cancel" or (action == "confirm" and open_seconds >= CONFIRM_AFTER_SECONDS) then
    close_notice()
    return true
  end
  return action == "confirm" or action == "menu"
end
