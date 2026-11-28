-- The room the player is in (apps/game/src/online_rooms.hpp), read and asked only through the UI
-- API's online namespace (docs/modding/ui-api.md), as its host or as a guest:
-- - The host picks the game mode from the list (the modes the game cannot play in rooms yet are
--   greyed and say so), and starts once the guests are ready. The host has no ready mark of its
--   own: it is the one who starts.
-- - A guest sees the chosen mode and marks itself ready or not.
-- Both see the room's card, the players with the host's crown and the ready marks, the code to
-- share (public rooms can be shared too) and a notice of what happens next. Cancel goes to «Leave
-- room» first and leaves from there, so a stray cancel does not drop the room. A room that closes
-- (the other left, the link failed) hands over to the waiting and error screen (ui/online_status),
-- which says why and offers to retry.
-- Once the game is under way the room goes on taking players: the host chooses with the menu
-- button whether it takes new ones then or only the guests coming back to their seat, and a guest
-- that joins a game in progress readies (picks its save) to go straight into it.

local ModeRows = 4
local MemberRows = 4

-- The cursor: a mode row (host), or one of the buttons under the boxes.
local focus = "start"
local mode_row = 1

local function say(key, values)
  return (
    string.gsub(t(key), "{([%w_]+)}", function(name)
      local value = values and values[name]
      return value ~= nil and tostring(value) or ""
    end)
  )
end

local function show(document, id, visible)
  document:get(id):set_class("hidden", not visible)
end

-- The room a name has: the width of the column round it, once laid out (common/fit_text.lua).
local function column_width(column)
  return function()
    local _, _, width = column:rect()
    return width
  end
end

local squeezed_name_letters = 24

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
  document:get("prompt-join"):set_prompt("menu")
end

local function host_name(room)
  for _, member in ipairs(room.members) do
    if member.host then
      return member.name
    end
  end
  return ""
end

local function place_focus(document)
  if focus == "mode" then
    document:get("mode-" .. mode_row):focus()
  else
    document:get(focus):focus()
  end
end

local function fill_modes(document, room)
  local modes = online.modes()
  for row = 1, ModeRows do
    local mode = modes[row]
    local element = document:get("mode-" .. row)
    element:set_class("hidden", mode == nil)
    if mode ~= nil then
      element:set_class("unavailable", not mode.available)
      element:set_class("selected", mode.selected)
      for _, id in ipairs({ "campaign", "versus", "training", "free" }) do
        element:set_class("mode-" .. id, mode.id == id)
      end
      document:get("mode-" .. row .. "-name"):set_text(t(mode.label_key))
      document
        :get("mode-" .. row .. "-text")
        :set_text(t(mode.available and mode.short_key or "online.unavailable"))
    end
  end
  -- The guest's card of the chosen mode.
  local card = document:get("mode-card")
  for _, id in ipairs({ "campaign", "versus", "training", "free" }) do
    card:set_class("mode-" .. id, room.mode == id)
  end
  document:get("mode-card-name"):set_text(t(room.mode_key))
  document:get("mode-card-text"):set_text(t("online.mode." .. room.mode .. ".description"))
end

local function fill_members(document, room)
  for row = 1, MemberRows do
    local member = room.members[row]
    local id = "member-" .. row
    local element = document:get(id)
    element:set_class("hidden", row > room.capacity)
    element:set_class("waiting", member == nil)
    element:set_class("host", member ~= nil and member.host)
    element:set_class("ready", member ~= nil and member.ready and not member.host)
    element:set_class("local", member ~= nil and member["local"])
    -- A seat taken by a player still connecting: the row is theirs, its mark says they are coming.
    element:set_class("arriving", member ~= nil and member.arriving)
    local name_text = document:get(id .. "-name-text")
    name_text:set_text(member ~= nil and member.name or t("online.waiting_player"))
    fit_text.fit(name_text, nil, column_width(document:get(id .. "-name")))
    local mark = ""
    if member ~= nil and member.host then
      mark = t("online.host_mark")
    elseif member ~= nil and member.arriving then
      mark = t("online.arriving_mark")
    end
    document:get(id .. "-mark"):set_text(mark)
  end
  document
    :get("players-tab")
    :set_text(say("online.players_title", { players = #room.members, capacity = room.capacity }))
end

-- The notice: why the room closed, a step the game did not take, else what happens next.
local function fill_toast(document, room)
  local state = online.state()
  local key
  local warning = false
  if room.phase == "closed" then
    key, warning = room.error_key ~= "" and room.error_key or "online.info.closed", true
  elseif state.notice_key ~= "" then
    key, warning = state.notice_key, true
  elseif room.phase == "connecting" then
    key = "online.info.connecting"
  elseif room.host then
    if room.can_start then
      key = "online.info.host_ready"
    elseif #room.members > 1 then
      key = "online.info.host_not_ready"
    else
      key = room.listed and "online.info.host_public" or "online.info.host_waiting"
    end
  elseif room.in_progress then
    key = room.ready and "online.info.guest_joining" or "online.info.guest_in_progress"
  else
    key = room.ready and "online.info.guest_ready" or "online.info.guest"
  end
  document:get("toast"):set_class("warning", warning)
  document:get("toast-text"):set_text(t(key))
end

local function refresh(document)
  local room = online.room()
  if room == nil then
    -- Left (or never in one): back to the search.
    ui.hide(document_name)
    ui.show("online_rooms", true)
    return
  end
  if room.phase == "connecting" or room.phase == "closed" then
    -- Not open (yet), or closed: the waiting and error screen says what happens and why.
    ui.hide(document_name)
    ui.show("online_status", true)
    return
  end
  local state = online.state()
  show(document, "sample-badge", state.sample)
  document
    :get("title")
    :set_text(t(room.host and "online.room_title.host" or "online.room_title.guest"))
  local code = room.code ~= "" and room.code or "----"
  local room_text, room_column = document:get("info-room-text"), document:get("info-room")
  if ((utf8 and utf8.len(room.name)) or #room.name) > squeezed_name_letters then
    room_text:set_property("transform", "none")
    fit_text.cut(room_text, room.name, column_width(room_column))
  else
    room_text:set_text(room.name)
    fit_text.fit(room_text, nil, column_width(room_column))
  end
  document:get("info-code"):set_text(code)
  document:get("info-host-text"):set_text(host_name(room))
  fit_text.fit(document:get("info-host-text"), nil, column_width(document:get("info-host")))
  document:get("info-mode-text"):set_text(t(room.mode_key))
  fit_text.fit(document:get("info-mode-text"), nil, column_width(document:get("info-mode")))
  document:get("info-state-text"):set_text(t(room.phase_key))
  fit_text.fit(document:get("info-state-text"), nil, column_width(document:get("info-state")))
  document:get("info-players-text"):set_text(say("online.players", {
    players = #room.members,
    capacity = room.capacity,
  }))
  fit_text.fit(document:get("info-players-text"), nil, column_width(document:get("info-players")))
  -- Public or private, and whether the game takes new players once under way (#356).
  document:get("info-visibility-text"):set_text(say("online.visibility.joining", {
    visibility = t(room.listed and "online.visibility.public" or "online.visibility.private"),
    joining = t(room.open_to_join and "online.join_open" or "online.join_closed"),
  }))
  fit_text.fit(
    document:get("info-visibility-text"),
    nil,
    column_width(document:get("info-visibility"))
  )
  local difficulty_key = room.mode == "campaign" and room.coop_difficulty_key or nil
  show(document, "info-difficulty-row", difficulty_key ~= nil)
  if difficulty_key ~= nil then
    -- Only the host's counts (#361): a guest whose own option differs sees both.
    local own = options.get("coop_difficulty")
    local difficulty = t(difficulty_key)
    if not room.host and own ~= nil and own ~= room.coop_difficulty then
      difficulty = say("online.info.difficulty_own", {
        host = difficulty,
        own = t("pause_menu.coop_difficulty." .. own),
      })
    end
    document:get("info-difficulty-text"):set_text(difficulty)
    fit_text.fit(
      document:get("info-difficulty-text"),
      nil,
      column_width(document:get("info-difficulty"))
    )
  end
  show(document, "prompt-join-box", room.host and room.phase ~= "closed")
  document
    :get("prompt-join-text")
    :set_text(t(room.open_to_join and "online.close_to_join" or "online.open_to_join"))
  document:get("chip-code"):set_text(code)
  for _, id in ipairs({ "campaign", "versus", "training", "free" }) do
    document:get("scene"):set_class("mode-" .. id, room.mode == id)
  end

  show(document, "modes-box", room.host)
  show(document, "mode-card", not room.host)
  fill_modes(document, room)
  fill_members(document, room)

  local closed = room.phase == "closed"
  local start = document:get("start")
  local start_text
  if closed then
    start_text = "online.back"
  elseif room.host then
    start_text = "online.start"
  else
    start_text = room.ready and "online.unready" or "online.ready"
  end
  local label = document:get("start-text")
  label:set_text(t(start_text))
  -- A long label is squeezed between the icon and the button's right padding (as wide as its left).
  fit_text.fit(label, nil, function()
    local start_x, _, start_width = start:rect()
    local label_x = label:rect()
    local icon_x = document:get("start-icon"):rect()
    return start_x + start_width - (icon_x - start_x) - label_x
  end)
  start:set_class("action-start", room.host and not closed)
  start:set_class("action-ready", not room.host and not closed)
  start:set_class("is-ready", room.ready)
  start:set_class(
    "unavailable",
    not closed and (room.host and not room.can_start or not room.host and not room.can_ready)
  )
  fill_toast(document, room)
  if focus == "mode" and not room.host then
    focus = "start"
  end
  place_focus(document)
end

function pick_mode(document, row)
  local modes = online.modes()
  local mode = modes[row]
  focus = "mode"
  mode_row = row
  if mode ~= nil and mode.available then
    online.set_mode(mode.id)
  end
  place_focus(document)
end

local function leave_room(document)
  online.leave()
  focus = "start"
  refresh(document)
end

function press(document, button)
  local room = online.room()
  if room == nil then
    return
  end
  focus = button
  if button == "leave" or room.phase == "closed" then
    leave_room(document)
  elseif room.host then
    online.start()
  elseif room.can_ready then
    online.set_ready(not room.ready)
  end
  place_focus(document)
end

function on_show(document)
  refresh_prompts(document)
  local room = online.room()
  focus = (room ~= nil and room.host) and "mode" or "start"
  mode_row = 1
  if room ~= nil and room.host then
    for row, mode in ipairs(online.modes()) do
      if mode.selected then
        mode_row = row
      end
    end
  end
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

function on_tick(document, _seconds)
  fit_text.tick()
  screen_fit.apply(document, document:get("stage"))
end

-- Up and down go through the modes and down to the buttons, left and right along the buttons.
function on_action(document, action)
  local room = online.room()
  if room == nil then
    return true
  end
  local buttons = { "start", "leave" }
  if action == "menu" and room.host then
    online.set_open_to_join(not room.open_to_join)
    return true
  end
  if action == "cancel" then
    if focus == "leave" or room.phase == "closed" then
      leave_room(document)
    else
      focus = "leave"
      place_focus(document)
    end
  elseif action == "confirm" then
    if focus == "mode" then
      pick_mode(document, mode_row)
    else
      press(document, focus)
    end
  elseif action == "up" then
    if focus == "mode" then
      mode_row = math.max(1, mode_row - 1)
    elseif room.host then
      focus = "mode"
      mode_row = ModeRows
    end
    place_focus(document)
  elseif action == "down" then
    if focus == "mode" then
      if mode_row < ModeRows then
        mode_row = mode_row + 1
      else
        focus = "start"
      end
    end
    place_focus(document)
  elseif action == "left" or action == "right" then
    if focus ~= "mode" then
      focus = buttons[action == "left" and 1 or 2]
    end
    place_focus(document)
  end
  return true
end
