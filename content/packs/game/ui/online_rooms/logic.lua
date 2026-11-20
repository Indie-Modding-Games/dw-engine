-- «Search rooms» (apps/game/src/online_rooms.hpp), read and asked only through the UI API's online
-- namespace (docs/modding/ui-api.md). The public rooms are the main way in: pick one, accept, and
-- the player is in its room. Beside the list: the search by name, «Join by code» for private rooms
-- and friends, «Create room» (public or private, public first) and «Find a game», which joins the
-- first open room or offers to create one.
--
-- The cursor is this script's: up and down move through the list, which scrolls five rows at a
-- time, and left and right go between the list and the right column. The text fields take the
-- keyboard's letters once confirmed: the game types them (online.edit), Enter ends the search or
-- joins the code, and cancel erases the last letter or, with none left, leaves the field. A gamepad
-- picks the code's letters with up and down. Cancel goes back to the «Multiplayer» hub; the next
-- key (`tab_next`) asks for the list again and the previous one (`tab_previous`) finds a game. With
-- the developer tools on, the menu key switches to the sample rooms.
--
-- Once the player is in a room the room's screen (ui/online_room) takes over; while it connects,
-- and when it did not open, the waiting and error screen (ui/online_status) does.

local VisibleRows = 5
local MaxCells = 6
-- The signaling server's room code letters (apps/versus-signal: codes.ts ALPHABET), for a gamepad.
local Alphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"
local SideItems = { "filter", "code", "enter", "create", "quick" }
local Dialogs = {
  create = { "create-public", "create-private", "create-cancel" },
  quick = { "quick-yes", "quick-no" },
}
-- After the game ends a text field (Enter), the confirm of that same key press is not a new one.
local EditSettleSeconds = 0.25

local zone = "list"
local selected = 1
local first = 1
local side = 2
local dialog = nil
local choice = 1
local editing = nil
local edit_seen = false
local settle = 0
local pad_letter = 1
local rooms = {}
-- The quick match's and the room's answers already shown, so each opens its box once.
local quick_shown = false

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

local function code_letters(state)
  return math.min(MaxCells, state.code_length)
end

-- Puts the focus where the cursor is: a dialog's choice, a row of the list or an item of the
-- right column.
local function place_focus(document)
  if dialog ~= nil then
    document:get(Dialogs[dialog][choice]):focus()
  elseif zone == "list" and #rooms > 0 then
    document:get("row-" .. (selected - first + 1)):focus()
  else
    zone = "side"
    document:get(SideItems[side]):focus()
  end
end

local function open_dialog(document, name)
  dialog = name
  choice = 1
  show(document, "create-dialog", name == "create")
  show(document, "quick-dialog", name == "quick")
  place_focus(document)
end

local function close_dialog(document)
  dialog = nil
  show(document, "create-dialog", false)
  show(document, "quick-dialog", false)
  place_focus(document)
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
  document:get("prompt-refresh"):set_prompt("tab_next")
  document:get("prompt-quick"):set_prompt("tab_previous")
  document:get("prompt-sample"):set_prompt("menu")
end

local function fill_rows(document)
  for row = 1, VisibleRows do
    local room = rooms[first + row - 1]
    local id = "row-" .. row
    local element = document:get(id)
    element:set_class("empty", room == nil)
    element:set_class("hidden", #rooms == 0)
    if room ~= nil then
      -- A long name is cut into its column with "..." (common/fit_text.lua).
      local name_text = document:get(id .. "-name-text")
      fit_text.cut(name_text, room.name, column_width(document:get(id .. "-name")))
      -- A game already under way that takes players says so in place of its mode (#356).
      document
        :get(id .. "-mode")
        :set_text(room.in_progress and t("online.mode_in_progress") or t(room.mode_key))
      element:set_class("in-progress", room.in_progress)
      document
        :get(id .. "-players")
        :set_text(say("online.players", { players = room.players, capacity = room.capacity }))
      element:set_class("full", not room.joinable)
      element:set_class("room-icon-versus", room.mode == "versus")
      local ping = document:get(id .. "-ping")
      for level = 0, 4 do
        ping:set_class("level-" .. level, room.ping_level == level)
      end
    end
  end
end

local function fill_list(document, list, state)
  local message, hint = nil, nil
  if list.state == "loading" then
    message = "online.list.loading"
  elseif list.state == "unavailable" then
    message, hint = list.error_key, "online.list.unavailable_hint"
  elseif list.total == 0 then
    message, hint = "online.list.empty", "online.list.empty_hint"
  elseif #rooms == 0 and state.filter ~= "" then
    message = "online.list.no_match"
  end
  show(document, "list-message", message ~= nil)
  if message ~= nil then
    document:get("list-message-text"):set_text(t(message))
    document:get("list-message-hint"):set_text(hint ~= nil and t(hint) or "")
  end
  fill_rows(document)
  -- The scroll bar: the arrows lit while there is more that way, the thumb as the shown share.
  local more_up = first > 1
  local more_down = first + VisibleRows - 1 < #rooms
  document:get("scroll-up"):set_class("off", not more_up)
  document:get("scroll-down"):set_class("off", not more_down)
  local share = #rooms > VisibleRows and VisibleRows / #rooms or 1
  local offset = #rooms > VisibleRows and (first - 1) / #rooms or 0
  local thumb = document:get("scroll-thumb")
  thumb:set_property("height", string.format("%.1f%%", share * 100))
  thumb:set_property("top", string.format("%.1f%%", offset * 100))
end

local function fill_side(document, state)
  local filter = document:get("filter-text")
  filter:set_text(state.filter ~= "" and state.filter or t("online.search_hint"))
  filter:set_class("placeholder", state.filter == "")
  show(document, "filter-caret", editing == "filter")
  document:get("filter"):set_class("editing", editing == "filter")
  local letters = code_letters(state)
  local typed = string.upper(state.code)
  for cell = 1, MaxCells do
    local element = document:get("cell-" .. cell)
    element:set_class("hidden", cell > letters)
    local letter = string.sub(typed, cell, cell)
    local current = editing == "code" and cell == #typed + 1
    if current and ui.device() == "gamepad" then
      letter = string.sub(Alphabet, pad_letter, pad_letter)
    end
    element:set_text(letter ~= "" and letter or "-")
    element:set_class("current", current)
  end
  document:get("code"):set_class("editing", editing == "code")
  local hint = ""
  if editing == "code" then
    hint = t(ui.device() == "gamepad" and "online.code_pad_hint" or "online.code_hint")
  end
  document:get("code-hint"):set_text(hint)
  document:get("enter"):set_class("unavailable", #state.code < letters)
end

-- A notice beside the prompts: a step the game did not take.
local function fill_toast(document, state)
  show(document, "toast", state.notice_key ~= "")
  if state.notice_key ~= "" then
    document:get("toast-text"):set_text(t(state.notice_key))
  end
end

local function refresh(document)
  local state = online.state()
  local list = online.list()
  rooms = list.rooms
  -- The game ended the field (Enter): the field is no longer the keyboard's.
  if editing ~= nil then
    if state.field == editing then
      edit_seen = true
    elseif edit_seen then
      editing = nil
      settle = EditSettleSeconds
    end
  end
  selected = math.max(1, math.min(selected, #rooms))
  if selected < first then
    first = selected
  elseif selected > first + VisibleRows - 1 then
    first = selected - VisibleRows + 1
  end
  first = math.max(1, math.min(first, math.max(1, #rooms - VisibleRows + 1)))
  show(document, "sample-badge", state.sample)
  show(document, "prompt-sample-box", state.sample_offered)
  fill_list(document, list, state)
  fill_side(document, state)
  fill_toast(document, state)
  -- In a room: its screen takes over, or the waiting and error screen while it connects or once
  -- it did not open (ui/online_status).
  local room = online.room()
  if room ~= nil then
    local waiting = room.phase == "connecting" or room.phase == "closed"
    ui.hide(document_name)
    ui.show(waiting and "online_status" or "online_room", true)
    return
  end
  if state.quick == "none" and not quick_shown and dialog == nil then
    quick_shown = true
    open_dialog(document, "quick")
    return
  end
  if state.quick ~= "none" then
    quick_shown = false
  end
  if zone == "list" and #rooms == 0 then
    zone = "side"
  end
  if editing == nil then
    place_focus(document)
  end
end

local function start_edit(document, field)
  editing = field
  edit_seen = false
  pad_letter = 1
  online.edit(field)
  document:get(field):focus()
end

local function end_edit()
  editing = nil
  online.edit(nil)
end

local function join_code()
  local state = online.state()
  if #state.code >= code_letters(state) then
    end_edit()
    online.join_by_code()
  end
end

local function activate_side(document, item)
  if item == "filter" or item == "code" then
    start_edit(document, item)
  elseif item == "enter" then
    join_code()
  elseif item == "create" then
    open_dialog(document, "create")
  elseif item == "quick" then
    online.quick_match()
  end
end

local function activate_row(row)
  local room = rooms[row]
  if room ~= nil then
    online.join(room.id)
  end
end

-- A step while a text field takes the letters. The keyboard's letters also make steps (F is
-- confirm, W and S up and down), so the keyboard only erases and leaves here; a gamepad picks the
-- code's letters.
local function edit_step(document, action)
  local state = online.state()
  local gamepad = ui.device() == "gamepad"
  local text = editing == "code" and state.code or state.filter
  local function write(value)
    if editing == "code" then
      online.set_code(value)
    else
      online.set_filter(value)
    end
  end
  if action == "cancel" then
    if text ~= "" then
      write(string.sub(text, 1, #text - 1))
    else
      end_edit()
      place_focus(document)
    end
  elseif gamepad and editing == "code" then
    if action == "up" or action == "down" then
      pad_letter = (pad_letter - 1 + (action == "up" and 1 or -1)) % #Alphabet + 1
      refresh(document)
    elseif action == "right" or action == "confirm" then
      if #text < code_letters(state) then
        write(text .. string.sub(Alphabet, pad_letter, pad_letter))
        pad_letter = 1
      elseif action == "confirm" then
        join_code()
      end
    elseif action == "left" then
      write(string.sub(text, 1, math.max(0, #text - 1)))
    end
  elseif gamepad and action == "confirm" then
    end_edit()
    place_focus(document)
  end
  return true
end

local function move_list(document, step)
  if #rooms == 0 then
    return
  end
  selected = math.max(1, math.min(#rooms, selected + step))
  refresh(document)
end

function pick_row(document, row)
  if editing ~= nil then
    end_edit()
  end
  zone = "list"
  selected = first + row - 1
  activate_row(selected)
  place_focus(document)
end

function click_side(document, item)
  if editing ~= nil and editing ~= item then
    end_edit()
  end
  zone = "side"
  for index, name in ipairs(SideItems) do
    if name == item then
      side = index
    end
  end
  if editing ~= item then
    activate_side(document, item)
  end
end

function scroll_list(document, step)
  move_list(document, step * VisibleRows)
end

function choose_dialog(document, option)
  local name = dialog
  close_dialog(document)
  if name == "create" then
    if option == 1 or option == 2 then
      online.host(option == 1)
    end
  elseif name == "quick" then
    online.dismiss()
    if option == 1 then
      online.host(true)
    end
  end
end

-- Back to the «Multiplayer» hub (ui/online_hub), which goes back to the title menu.
local function leave_screen()
  if editing ~= nil then
    end_edit()
  end
  ui.hide(document_name)
  ui.show("online_hub", true)
end

function on_show(document)
  dialog = nil
  editing = nil
  quick_shown = false
  show(document, "create-dialog", false)
  show(document, "quick-dialog", false)
  refresh_prompts(document)
  online.open()
  zone = "list"
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
  refresh(document)
end

function on_tick(document, seconds)
  if settle > 0 then
    settle = math.max(0, settle - seconds)
  end
  fit_text.tick()
  screen_fit.apply(document, document:get("stage"))
end

function on_action(document, action)
  if editing ~= nil then
    return edit_step(document, action)
  end
  if dialog ~= nil then
    local options = Dialogs[dialog]
    if action == "up" or action == "down" then
      choice = math.max(1, math.min(#options, choice + (action == "up" and -1 or 1)))
      place_focus(document)
    elseif action == "confirm" and settle <= 0 then
      choose_dialog(document, choice)
    elseif action == "cancel" then
      choose_dialog(document, #options)
    end
    return true
  end
  if action == "cancel" then
    leave_screen()
  elseif action == "menu" then
    local state = online.state()
    if state.sample_offered then
      online.set_sample(not state.sample)
    end
  elseif action == "tab_next" then
    online.refresh()
  elseif action == "tab_previous" then
    online.quick_match()
  elseif action == "confirm" then
    if settle > 0 then
      return true
    end
    if zone == "list" then
      activate_row(selected)
    else
      activate_side(document, SideItems[side])
    end
  elseif zone == "list" then
    if action == "up" or action == "down" then
      move_list(document, action == "up" and -1 or 1)
    elseif action == "right" then
      zone = "side"
      place_focus(document)
    end
  else
    if action == "up" or action == "down" then
      side = math.max(1, math.min(#SideItems, side + (action == "up" and -1 or 1)))
      place_focus(document)
    elseif action == "left" and #rooms > 0 then
      zone = "list"
      place_focus(document)
    end
  end
  return true
end
