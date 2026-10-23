-- The notice of an item on the field's ground or in a chest (tamer.c: tamerTickPickupItem,
-- tamerTickTakeChest): the story's speech box with the item's name over the words. The game
-- publishes field_item_notice.text (a text key) and field_item_notice.item (the item's name key,
-- empty for an empty chest) and shows this document; confirming sends field_item_notice_ok, the
-- only answer the original's box takes (there is no button).

local function refresh(document)
  local item_key = ui.setting("field_item_notice.item") or ""
  local item = document:get("item")
  item:set_text(item_key ~= "" and t(item_key) or "")
  item:set_class("hidden", item_key == "")
  document:get("text"):set_text(t(ui.setting("field_item_notice.text") or ""))
end

function on_show(document)
  refresh(document)
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
end

function on_action(_document, action)
  if action == "confirm" then
    ui.send("field_item_notice_ok")
    return true
  end
  return action == "cancel" or action == "menu"
end
