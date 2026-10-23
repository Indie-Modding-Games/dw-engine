-- The fishing's notices (fish.c: FISH_showTextBox): what happened at the hook, and what the player
-- caught. The game publishes fishing_notice.text (a text key), fishing_notice.item (an item's name
-- key, or empty) and fishing_notice.size (a fish's length in centimetres, or 0) and shows this
-- document; it sends fishing_notice_ok. Like the original's text box, it is answered only by
-- confirming.

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
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
end

function on_show(document)
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

function on_action(_document, action)
  return action == "cancel" or action == "menu"
end
