-- The online profile (ui/online_profile), read and asked through the UI API's online namespace
-- (docs/modding/ui-api.md): the name the other players see, the icon and the online versus record,
-- kept in the game's settings, and how rooms connect (directly, or through the server's relay
-- only). Up and down go through the name, the icons, the connection and Back; left and right pick
-- an icon at once, and confirm, left or right switch the connection. Confirm on the name gives the
-- keyboard's letters to it (the game types them, online.edit): Enter keeps the name, cancel erases
-- the last letter or, with none left, leaves the field keeping the old name. A gamepad's confirm
-- keeps what is typed.

local Rows = { "name", "icon", "relay", "back" }
local EditSettleSeconds = 0.25

local row = 1
local editing = false
local edit_seen = false
local settle = 0

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

local function show(document, id, visible)
  document:get(id):set_class("hidden", not visible)
end

-- The text without its last letter (a letter may take several bytes).
local function drop_last_letter(text)
  local at = #text
  while at > 0 do
    local byte = string.byte(text, at)
    if byte < 128 or byte >= 192 then
      break
    end
    at = at - 1
  end
  return string.sub(text, 1, math.max(0, at - 1))
end

local function icon_index(icons, id)
  for index, icon in ipairs(icons) do
    if icon.id == id then
      return index
    end
  end
  return 1
end

local function set_icon_class(element, icons, id)
  for _, icon in ipairs(icons) do
    element:set_class("icon-" .. icon.id, icon.id == id)
  end
end

local function place_focus(document)
  local id = Rows[row]
  if id == "name" then
    document:get("name-field"):focus()
  elseif id == "icon" then
    local icons = online.icons()
    document:get("icon-" .. icon_index(icons, online.profile().icon)):focus()
  elseif id == "relay" then
    document:get("relay-field"):focus()
  else
    document:get("profile-back"):focus()
  end
end

local function refresh(document)
  local profile = online.profile()
  local state = online.state()
  local icons = online.icons()
  -- The game ended the field (Enter): the field is no longer the keyboard's.
  if editing then
    if state.field == "name" then
      edit_seen = true
    elseif edit_seen then
      editing = false
      settle = EditSettleSeconds
    end
  end

  set_icon_class(document:get("head-icon"), icons, profile.icon)
  local head_name = document:get("head-name-text")
  head_name:set_text(profile.name)
  local head_column = document:get("head-name")
  fit_text.fit(head_name, nil, function()
    local _, _, width = head_column:rect()
    return width
  end)
  document:get("head-record"):set_text(
    (
      string.gsub(
        t("online.hub.record"),
        "{([%w_]+)}",
        { wins = profile.wins, losses = profile.losses }
      )
    )
  )

  local field = document:get("name-field")
  field:set_class("editing", editing)
  field:set_class("default-name", not editing and not profile.named)
  document:get("name-value"):set_text(editing and profile.draft or profile.name)
  show(document, "name-caret", editing)
  document
    :get("name-hint")
    :set_text(t(editing and "online.profile.name_editing" or "online.profile.name_hint"))

  for index = 1, 6 do
    local icon = icons[index]
    local button = document:get("icon-" .. index)
    button:set_class("hidden", icon == nil)
    if icon ~= nil then
      button:set_class("selected", icon.id == profile.icon)
      set_icon_class(document:get("icon-" .. index .. "-picture"), icons, icon.id)
    end
  end
  local chosen = icons[icon_index(icons, profile.icon)]
  document:get("icon-name"):set_text(chosen ~= nil and t(chosen.label_key) or "")

  local relay = document:get("relay-value")
  relay:set_class("on", profile.relay_only)
  relay:set_text(t(profile.relay_only and "online.profile.relay_on" or "online.profile.relay_off"))

  document:get("record-wins"):set_text(tostring(profile.wins))
  document:get("record-losses"):set_text(tostring(profile.losses))

  show(document, "toast", state.notice ~= "")
  document:get("toast-text"):set_text(state.notice ~= "" and t(state.notice_key) or "")

  document
    :get("prompt-confirm-text")
    :set_text(t(editing and "online.profile.save" or "online.accept"))
  document:get("prompt-cancel-text"):set_text(t("online.back"))
  if not editing then
    place_focus(document)
  end
end

local function start_edit(document)
  editing = true
  edit_seen = false
  online.dismiss()
  online.edit("name")
  refresh(document)
end

-- Leaves the field: `keep` keeps what is typed as the name.
local function end_edit(document, keep)
  editing = false
  if keep then
    online.save_name()
  else
    online.edit(nil)
  end
  refresh(document)
end

local function leave()
  if editing then
    online.edit(nil)
    editing = false
  end
  online.dismiss()
  ui.hide(document_name)
  ui.show("online_hub", true)
end

function pick_icon(document, index)
  local icon = online.icons()[index]
  row = 2
  if icon ~= nil then
    online.set_icon(icon.id)
  end
  refresh(document)
end

local function switch_relay(document)
  row = 3
  online.dismiss()
  online.set_relay_only(not online.profile().relay_only)
  refresh(document)
end

function press(document, button)
  if button == "relay" then
    switch_relay(document)
  elseif button == "name" then
    row = 1
    if editing then
      end_edit(document, true)
    else
      start_edit(document)
    end
  else
    leave()
  end
end

function on_show(document)
  refresh_prompts(document)
  row = 1
  editing = false
  settle = 0
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

function on_tick(document, seconds)
  settle = math.max(0, settle - (seconds or 0))
  fit_text.tick()
  screen_fit.apply(document, document:get("panel"))
end

-- While the name takes the letters the keyboard's letters also make steps (F is confirm, W and S
-- up and down), so only cancel (erase) and a gamepad's confirm (keep) act here.
local function edit_step(document, action)
  if action == "cancel" then
    local draft = online.profile().draft
    if draft ~= "" then
      online.set_name_draft(drop_last_letter(draft))
    else
      end_edit(document, false)
    end
  elseif action == "confirm" and ui.device() == "gamepad" then
    end_edit(document, true)
  end
  return true
end

function on_action(document, action)
  if editing then
    return edit_step(document, action)
  end
  if action == "cancel" then
    leave()
  elseif action == "confirm" then
    if settle > 0 then
      return true
    end
    if Rows[row] == "name" then
      press(document, "name")
    elseif Rows[row] == "relay" then
      switch_relay(document)
    elseif Rows[row] == "back" then
      leave()
    end
  elseif action == "up" or action == "down" then
    row = math.max(1, math.min(#Rows, row + (action == "up" and -1 or 1)))
    online.dismiss()
    place_focus(document)
  elseif (action == "left" or action == "right") and Rows[row] == "icon" then
    local icons = online.icons()
    local index = icon_index(icons, online.profile().icon) + (action == "left" and -1 or 1)
    if index >= 1 and index <= #icons then
      online.set_icon(icons[index].id)
    end
  elseif (action == "left" or action == "right") and Rows[row] == "relay" then
    switch_relay(document)
  end
  return true
end
