-- The notice of an item on the field's ground or in a chest (tamer.c: tamerTickPickupItem,
-- tamerTickTakeChest). The game publishes field_item_notice.text (a text key) and
-- field_item_notice.item (the item's name key) and shows this document; it sends
-- field_item_notice_ok. Like the original's text box, it is answered only by confirming.

local function refresh(document)
  local item_key = ui.setting("field_item_notice.item") or ""
  local item = item_key ~= "" and t(item_key) or ""
  document:get("text"):set_text(t(ui.setting("field_item_notice.text") or "", { item = item }))
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
