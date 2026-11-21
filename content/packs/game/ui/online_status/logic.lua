-- Getting into a room and why it failed (ui/online_status), read and asked through the UI API's
-- online namespace (docs/modding/ui-api.md):
-- - While the room connects (creating it, or joining it from the list or by its code): what is
--   happening, dots that come and go, and Cancel, which leaves the attempt.
-- - Once it closed (the code was wrong, the server did not answer, the other player left): why,
--   with Retry when the game can ask for the same room again, and Back to the room search.
-- Once the room opens, its own screen (ui/online_room) takes over; without a room, the search does.

local focus = "back"
local clock = 0
local was_closed = false

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

local function show(document, id, visible)
  document:get(id):set_class("hidden", not visible)
end

local function place_focus(document)
  document:get(focus):focus()
end

local function refresh(document)
  local room = online.room()
  if room == nil then
    ui.hide(document_name)
    ui.show("online_rooms", true)
    return
  end
  if room.phase ~= "connecting" and room.phase ~= "closed" then
    ui.hide(document_name)
    ui.show("online_room", true)
    return
  end
  local closed = room.phase == "closed"
  local panel = document:get("panel")
  panel:set_class("closed", closed)
  panel:set_class("connecting", not closed)
  if closed then
    document:get("title"):set_text(t("online.error.title"))
    document:get("status-text"):set_text(t(room.error_key))
    -- An error the game has no words for keeps its own code, small, under the generic text.
    local unknown = room.error_key == "online.error.unknown" and room.detail ~= nil
    document
      :get("status-detail")
      :set_text(unknown and t("online.error.code", { code = room.detail }) or "")
  else
    document:get("title"):set_text(t(room.host and "online.create" or "online.room_title.guest"))
    document
      :get("status-text")
      :set_text(t(room.host and "online.status.hosting" or "online.status.joining"))
    document:get("status-detail"):set_text(t("online.status.wait_hint"))
  end
  local can_retry = closed and online.state().can_retry
  show(document, "retry", can_retry)
  -- Once it fails, the cursor starts on Retry.
  if closed and not was_closed and can_retry then
    focus = "retry"
  end
  was_closed = closed
  show(document, "status-icon", not closed)
  local back_key = closed and "online.back" or "online.cancel"
  document:get("back"):set_text(t(back_key))
  document:get("prompt-cancel-text"):set_text(t(back_key))
  if not can_retry then
    focus = "back"
  end
  place_focus(document)
end

-- Back to the search: a closed room is dismissed, a connecting one left.
local function back()
  local room = online.room()
  if room ~= nil and room.phase == "closed" then
    online.dismiss()
  else
    online.leave()
  end
end

function press(document, button)
  focus = button
  if button == "retry" then
    online.retry()
  else
    back()
  end
  if ui.is_visible(document_name) then
    refresh(document)
  end
end

function on_show(document)
  refresh_prompts(document)
  focus = "back"
  was_closed = false
  clock = 0
  refresh(document)
end

hooks.on("online.changed", function(_, document)
  if ui.is_visible(document_name) then
    refresh(document)
  end
end)

function on_language(document)
  refresh_prompts(document)
  refresh(document)
end

function on_device(document)
  refresh_prompts(document)
end

-- The dots light one more each third of a second.
function on_tick(document, seconds)
  clock = clock + (seconds or 0)
  local lit = math.floor(clock * 3) % 4
  for dot = 1, 3 do
    document:get("dot-" .. dot):set_class("lit", dot <= lit)
  end
  screen_fit.apply(document, document:get("panel"))
end

function on_action(document, action)
  if action == "cancel" then
    back()
    if ui.is_visible(document_name) then
      refresh(document)
    end
  elseif action == "confirm" then
    press(document, focus)
  elseif action == "left" or action == "right" then
    if not document:get("retry"):has_class("hidden") then
      focus = action == "left" and "retry" or "back"
      place_focus(document)
    end
  end
  return true
end
