-- The notice of an item on the field's ground or in a chest (tamer.c: tamerTickPickupItem,
-- tamerTickTakeChest). The game publishes field_item_notice.text (a text key) and
-- field_item_notice.item (the item's name key, empty for an empty chest) and shows this
-- document; it sends field_item_notice_ok. Like the original's text box, it shows the item's name
-- over the words and is answered only by confirming.

local function refresh(document)
  local item_key = ui.setting("field_item_notice.item") or ""
  local item = document:get("item")
  item:set_text(item_key ~= "" and t(item_key) or "")
  item:set_class("hidden", item_key == "")
  document:get("text"):set_text(t(ui.setting("field_item_notice.text") or ""))
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
