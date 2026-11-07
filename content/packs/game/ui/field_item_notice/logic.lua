-- The notice of an item on the field's ground or in a chest (tamer.c: tamerTickPickupItem,
-- tamerTickTakeChest): the story's speech box with the item's name over the words. The game
-- publishes field_item_notice.text (a text key) and field_item_notice.item (the item's name key,
-- empty for an empty chest) and shows this document; confirming sends field_item_notice_ok, the
-- only answer the original's box takes (there is no button). The game says in
-- field_item_notice.answer whether it takes a confirm now ("sound", "quiet" for a chest's, which
-- sounds nothing, or "" while the counter waits, when a press is lost), and sets
-- field_item_notice.phase to "closing" once the notice is answered.
--
-- In the original look (ui/common/original_box.lua) the box grows out of the player
-- (tamerTickPickupItem, tamerTickTakeChest: createAnimatedUIBox from the 10 x 10 pixels round the
-- player, sounding box_open) and shrinks back into it (removeAnimatedUIBox); the document then
-- sends field_item_notice_closed, at once outside that look.

local closed = false

local function refresh(document)
  local item_key = ui.setting("field_item_notice.item") or ""
  local item = document:get("item")
  item:set_text(item_key ~= "" and t(item_key) or "")
  item:set_class("hidden", item_key == "")
  document:get("text"):set_text(t(ui.setting("field_item_notice.text") or ""))
end

-- The confirm button in the box's corner (script_textbox.c: renderUIBox), as the device in use's
-- glyph.
local function refresh_prompt(document)
  document:get("confirm-icon"):set_prompt("confirm", true)
end

function on_show(document)
  closed = false
  refresh_prompt(document)
  refresh(document)
  original_box.open(document, "box", original_box.player_rect(document))
end

function on_hide(document)
  original_box.cancel(document)
end

local function close(document)
  if closed then
    return
  end
  closed = true
  original_box.close(document, "box", original_box.player_rect(document), function()
    ui.send("field_item_notice_closed")
  end)
end

function on_device(document)
  refresh_prompt(document)
end

function on_setting(document, name)
  if name == "field_item_notice.phase" then
    if ui.setting(name) == "closing" then
      close(document)
    end
    return
  end
  if name ~= "field_item_notice.answer" then
    refresh(document)
  end
end

function on_language(document)
  refresh_prompt(document)
  refresh(document)
end

-- A UI scale larger than the window allows draws the box smaller, inside the window
-- (common/screen_fit.lua); the box draws nothing while its outline grows or shrinks.
function on_tick(document, seconds)
  original_box.tick(document, seconds)
  local hidden = closed or original_box.busy()
  screen_fit.apply(document, document:get("box"), hidden and 0 or nil)
end

-- The confirm button or a click on the box; the box takes neither while it grows or shrinks
-- (tickUIBox runs only in its state 1).
function answer(_document)
  if closed or original_box.busy() then
    return
  end
  if ui.setting("field_item_notice.answer") == "sound" then
    ui.cue("confirm")
  end
  ui.send("field_item_notice_ok")
end

function on_action(document, action)
  if action == "confirm" then
    answer(document)
    return true
  end
  return action == "cancel" or action == "menu"
end
