-- The «Multiplayer» hub (ui/online_hub), read and asked through the UI API's online namespace
-- (docs/modding/ui-api.md): the online versus, the co-op rooms and the online profile, with the
-- profile's icon, name and versus record on top. Up and down pick an entry; cancel goes back to the
-- title menu (online_close, which shows it again on its multiplayer entry).

local Entries = { "versus", "rooms", "profile", "back" }

local current = 1

local function say(key, values)
  return (
    string.gsub(t(key), "{([%w_]+)}", function(name)
      local value = values and values[name]
      return value ~= nil and tostring(value) or ""
    end)
  )
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

local function refresh(document)
  local profile = online.profile()
  local icon = document:get("player-icon")
  for _, choice in ipairs(online.icons()) do
    icon:set_class("icon-" .. choice.id, choice.id == profile.icon)
  end
  local name = document:get("player-name-text")
  name:set_text(profile.name)
  local column = document:get("player-name")
  fit_text.fit(name, nil, function()
    local _, _, width = column:rect()
    return width
  end)
  local record = document:get("player-record-text")
  record:set_text(say("online.hub.record", { wins = profile.wins, losses = profile.losses }))
  local record_column = document:get("player-record")
  fit_text.fit(record, nil, function()
    local _, _, width = record_column:rect()
    return width
  end)
  -- Each entry's name and the line under it are squeezed into the entry, past its icon.
  for _, entry in ipairs({ "versus", "rooms", "profile" }) do
    local button = document:get("entry-" .. entry)
    for _, part in ipairs({ "name", "hint" }) do
      local text = document:get("entry-" .. entry .. "-" .. part)
      fit_text.fit(text, nil, function()
        local button_x, _, button_width = button:rect()
        local text_x = text:rect()
        return button_x + button_width - text_x - 12
      end)
    end
  end
  document:get("entry-" .. Entries[current]):focus()
end

-- The title menu again: the game hides the online screens and shows it (online_close).
local function leave()
  online.close()
  ui.hide(document_name)
end

function pick(_document, entry)
  for index, id in ipairs(Entries) do
    if id == entry then
      current = index
    end
  end
  if entry == "versus" then
    -- The versus screen takes over; leaving it shows the title menu again.
    ui.hide(document_name)
    ui.send("versus")
  elseif entry == "rooms" then
    ui.hide(document_name)
    ui.show("online_rooms", true)
  elseif entry == "profile" then
    ui.hide(document_name)
    ui.show("online_profile", true)
  else
    leave()
  end
end

function on_show(document)
  refresh_prompts(document)
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
  screen_fit.apply(document, document:get("panel"))
end

function on_action(document, action)
  if action == "cancel" then
    leave()
  elseif action == "confirm" then
    pick(document, Entries[current])
  elseif action == "up" then
    current = math.max(1, current - 1)
    document:get("entry-" .. Entries[current]):focus()
  elseif action == "down" then
    current = math.min(#Entries, current + 1)
    document:get("entry-" .. Entries[current]):focus()
  end
  return true
end
