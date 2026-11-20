-- The UI API's online (docs/modding/ui-api.md, namespace online): the public room list, the room
-- the player is in and the steps between them, as the game publishes them (apps/game:
-- online_rooms.cpp over the co-op connection, online_coop_room.cpp). Where the list and the room
-- come from is the game's: today rooms open and join by code over the co-op link and the list is
-- unavailable until the network lists public rooms; the developer tools' sample source fills both
-- with sample data (online.state().sample). A step only asks; the game answers with
-- online.changed.
--
-- Every function returns a fresh table; changing it changes nothing in the game.

online = {}

local function text(name)
  local found = ui.setting(name)
  return found == nil and "" or tostring(found)
end

local function whole(name)
  return math.floor(tonumber(ui.setting(name)) or 0)
end

local function flag(name)
  return ui.setting(name) == true
end

-- The error codes the game gives (the room's, the list's and the co-op link's) that have their own
-- text; any other shows online.error.unknown, and its words stay in detail.
local Errors = {
  bad_code = true,
  no_join_transport = true,
  no_host_transport = true,
  transport_unavailable = true,
  rejected = true,
  no_server = true,
  server_unreachable = true,
  wrong_code = true,
  room_full = true,
  signal_timeout = true,
  no_direct_route = true,
  connection_closed = true,
  server_refused = true,
  no_relay = true,
  list_limit = true,
  content_identity_unavailable = true,
  ruleset_unavailable = true,
  choose_saved_game = true,
  host_save_map_mismatch = true,
  no_answer = true,
  silence = true,
  protocol_mismatch = true,
  version_mismatch = true,
  content_mismatch = true,
  busy = true,
  damaged = true,
  disconnected = true,
  directory_unavailable = true,
  unknown = true,
}

-- The notices of a step the game did not take (online.notice), with their own text.
local Notices = {
  room_gone = true,
  room_full = true,
  mode_unavailable = true,
  bad_code = true,
  not_ready = true,
  sample_start = true,
  name_empty = true,
}

-- The text key of an error code ("" for none).
function online.error_key(code)
  if code == nil or code == "" then
    return ""
  end
  return Errors[code] and ("online.error." .. code) or "online.error.unknown"
end

-- The text key of a notice code ("" for none).
function online.notice_key(code)
  if code == nil or code == "" then
    return ""
  end
  if Notices[code] then
    return "online.notice." .. code
  end
  return online.error_key(code)
end

-- The screens' state: open (the rooms screens are open), sample (showing sample rooms),
-- sample_offered (the switch to them is offered: the developer tools are on), field (the text
-- field taking the keyboard: "filter", "code", "name" or ""), filter and code (what is typed in
-- each), code_length (a room code's characters), quick ("none" when the quick match found no room
-- to join, so the screen offers to create one; else ""), notice and notice_key (a step the game did
-- not take, and its text), can_retry (a room was asked for, which online.retry asks again).
function online.state()
  local notice = text("online.notice")
  return {
    open = flag("online.open"),
    sample = flag("online.sample"),
    sample_offered = flag("online.sample_offered"),
    field = text("online.field"),
    filter = text("online.filter"),
    code = text("online.code"),
    code_length = math.max(1, whole("online.code_length")),
    quick = text("online.quick"),
    notice = notice,
    notice_key = online.notice_key(notice),
    can_retry = flag("online.can_retry"),
  }
end

-- This player's online profile (kept in the game's settings, not in a save): name (the one the
-- other players see), named (false while it is the default one), icon (an id of online.icons()),
-- wins and losses (the online versus record), draft (the name being typed on the profile
-- screen) and relay_only (rooms connect through the server's relay only, so the other player never
-- learns this machine's address).
function online.profile()
  return {
    name = text("online.profile.name"),
    named = flag("online.profile.named"),
    icon = text("online.profile.icon"),
    wins = whole("online.profile.wins"),
    losses = whole("online.profile.losses"),
    draft = text("online.profile.draft"),
    relay_only = flag("online.profile.relay_only"),
  }
end

-- The icons a profile can show, in order: ids, each with its text key (online.icon.<id>).
function online.icons()
  local icons = {}
  for index = 0, whole("online.icons") - 1 do
    local id = text("online.icon." .. index .. ".id")
    table.insert(icons, { id = id, label_key = "online.icon." .. id })
  end
  return icons
end

-- Replaces the name being typed (an erased letter); online.save_name keeps it.
function online.set_name_draft(value)
  if type(value) ~= "string" then
    return false, "value must be a string"
  end
  ui.send("online_profile_name", value)
  return true
end

-- Keeps the typed name as the profile's (notice name_empty when nothing is left of it).
function online.save_name()
  ui.send("online_profile_save")
  return true
end

-- Changes the profile's icon.
function online.set_icon(id)
  if type(id) ~= "string" then
    return false, "id must be a string"
  end
  ui.send("online_profile_icon", id)
  return true
end

-- Connects the next rooms through the server's relay only (true) or directly when possible (false).
function online.set_relay_only(on)
  if type(on) ~= "boolean" then
    return false, "on must be a boolean"
  end
  ui.send("online_profile_relay", on)
  return true
end

-- Asks again for the room the player last asked for (hosting it, or joining it from the list or
-- by its code), after a failure.
function online.retry()
  ui.send("online_retry")
  return true
end

-- How strong a ping is, from 1 (poor) to 4 (good); 0 when nobody measured it.
local function ping_level(ms)
  if ms < 0 then
    return 0
  elseif ms <= 60 then
    return 4
  elseif ms <= 120 then
    return 3
  elseif ms <= 200 then
    return 2
  end
  return 1
end

-- A name the game gave, else its text key's text.
local function shown_name(name, key)
  if name ~= "" then
    return name
  end
  return key ~= "" and t(key) or ""
end

-- A text key's text with its {words} filled from values.
local function say(key, values)
  return (
    string.gsub(t(key), "{([%w_]+)}", function(word)
      local value = values[word]
      return value ~= nil and tostring(value) or ""
    end)
  )
end

-- A room's name: the one it was given, else its host's ("Ana's room"), else untitled. The list and
-- the room say it alike.
local function room_title(name, key, host)
  local shown = shown_name(name, key)
  if shown ~= "" then
    return shown
  end
  if host ~= "" then
    return say("online.room_of", { name = host })
  end
  return t("online.untitled_room")
end

-- The public room list: state (loading, ready or unavailable), error and error_key (why it is
-- unavailable), total (rooms listed) and rooms, those whose name has the typed filter in it, each
-- {id, code, name, message, host_name, state (waiting, full or starting), mode, mode_key, players,
-- capacity, ping (milliseconds, -1 unknown), ping_level (0 to 4), full, joinable}. A full room is
-- listed but not joinable.
function online.list()
  local state = text("online.list.state")
  local error = text("online.list.error")
  local filter = string.lower(text("online.filter"))
  local rooms = {}
  local total = whole("online.rooms")
  for index = 0, total - 1 do
    local prefix = "online.room." .. index .. "."
    local name =
      room_title(text(prefix .. "name"), text(prefix .. "name_key"), text(prefix .. "host_name"))
    if filter == "" or string.find(string.lower(name), filter, 1, true) ~= nil then
      local players = whole(prefix .. "players")
      local capacity = whole(prefix .. "capacity")
      local ping = tonumber(ui.setting(prefix .. "ping")) or -1
      local mode = text(prefix .. "mode")
      local room_state = text(prefix .. "state")
      table.insert(rooms, {
        id = text(prefix .. "id"),
        code = text(prefix .. "code"),
        name = name,
        message = text(prefix .. "message"),
        host_name = text(prefix .. "host_name"),
        state = room_state,
        mode = mode,
        mode_key = "online.mode." .. mode,
        players = players,
        capacity = capacity,
        ping = ping,
        ping_level = ping_level(ping),
        full = room_state ~= "waiting" or players >= capacity,
        joinable = flag(prefix .. "joinable"),
      })
    end
  end
  return {
    state = state ~= "" and state or "unavailable",
    error = error,
    error_key = online.error_key(error),
    total = total,
    rooms = rooms,
  }
end

-- The game modes a room offers, in order, each {id, label_key, description_key, short_key (the
-- description in a few words), available (it can be played in rooms now), selected (the room's
-- mode, when in a room)}.
function online.modes()
  local selected = flag("online.current.active") and text("online.current.mode") or ""
  local modes = {}
  for index = 0, whole("online.modes") - 1 do
    local id = text("online.mode." .. index .. ".id")
    table.insert(modes, {
      id = id,
      label_key = "online.mode." .. id,
      description_key = "online.mode." .. id .. ".description",
      short_key = "online.mode." .. id .. ".short",
      available = flag("online.mode." .. index .. ".available"),
      selected = id == selected,
    })
  end
  return modes
end

-- The room the player is in, or nil: code, name, host (this player hosts it), listed (public) ,
-- phase (connecting, open, connected, ready, playing, closed) and phase_key, mode and mode_key,
-- capacity, error, error_key and detail (why it closed, and the game's own words), can_start (the
-- host can start), can_ready (the guest can mark itself ready), ready (this player is ready) and
-- members, each {name, host, ready, local, arriving}: a room seats up to four (capacity), and an
-- arriving member holds its seat while its connection is still being set up. A member with no name
-- of its own is named by its place: you, the host or the guest by number.
function online.room()
  if not flag("online.current.active") then
    return nil
  end
  local members = {}
  local host_name = ""
  for index = 0, whole("online.current.members") - 1 do
    local prefix = "online.member." .. index .. "."
    local is_host = flag(prefix .. "host")
    local is_local = flag(prefix .. "local")
    local name = shown_name(text(prefix .. "name"), text(prefix .. "name_key"))
    if is_host then
      -- The room is named after its host as the list names it: the host's own profile name.
      host_name = name ~= "" and name or (is_local and text("online.profile.name") or "")
    end
    if name == "" then
      if is_local then
        name = t("online.you")
      elseif is_host then
        name = t("online.host_player")
      else
        -- Guests in the order they joined: the host is the first member.
        name = (string.gsub(t("online.guest_player"), "{number}", tostring(index)))
      end
    end
    local icon = text(prefix .. "icon")
    table.insert(members, {
      name = name,
      icon = icon ~= "" and icon or "egg",
      host = is_host,
      ready = flag(prefix .. "ready"),
      ["local"] = is_local,
      arriving = flag(prefix .. "arriving"),
    })
  end
  local phase = text("online.current.phase")
  local mode = text("online.current.mode")
  local error = text("online.current.error")
  return {
    code = text("online.current.code"),
    name = room_title(text("online.current.name"), text("online.current.name_key"), host_name),
    host = flag("online.current.host"),
    listed = flag("online.current.listed"),
    phase = phase,
    phase_key = "online.phase." .. (phase ~= "" and phase or "connecting"),
    mode = mode,
    mode_key = "online.mode." .. mode,
    capacity = math.max(1, whole("online.current.capacity")),
    error = error,
    error_key = online.error_key(error),
    detail = error,
    can_start = flag("online.current.can_start"),
    can_ready = flag("online.current.can_ready"),
    ready = flag("online.current.ready"),
    members = members,
  }
end

-- The rooms screens opened and closed: the game asks for the list on opening.
function online.open()
  ui.send("online_open")
  return true
end

function online.close()
  ui.send("online_close")
  return true
end

-- Asks for the room list again.
function online.refresh()
  ui.send("online_refresh")
  return true
end

-- Which text field takes the keyboard's letters: "filter", "code", "name", or nil for none. Enter
-- in the code field joins its room.
function online.edit(field)
  if field ~= nil and field ~= "filter" and field ~= "code" and field ~= "name" then
    return false, "field must be filter, code, name or nil"
  end
  ui.send("online_field", field or "")
  return true
end

-- Replaces what the filter or the code field holds (a pad's letters, an erased one).
function online.set_filter(value)
  if type(value) ~= "string" then
    return false, "value must be a string"
  end
  ui.send("online_filter", value)
  return true
end

function online.set_code(value)
  if type(value) ~= "string" then
    return false, "value must be a string"
  end
  ui.send("online_code", value)
  return true
end

-- The room steps are the co-op lobby's own commands (apps/game: CoopScreen), which the game hands
-- to the room the player is in: coop_host, coop_join with a code, coop_ready and coop_start.

-- Opens a room this player hosts: public (listed, anyone can join from the list) or private (only
-- by its code).
function online.host(public)
  if type(public) ~= "boolean" then
    return false, "public must be true or false"
  end
  ui.send("coop_host", public)
  return true
end

-- Joins a room of the list by its id.
function online.join(room_id)
  if type(room_id) ~= "string" or room_id == "" then
    return false, "room_id must be a room's id"
  end
  ui.send("online_join", room_id)
  return true
end

-- Joins a room by its code: the one given, or the one typed in the code field.
function online.join_by_code(code)
  if code ~= nil and type(code) ~= "string" then
    return false, "code must be a string or nil"
  end
  if code ~= nil and code ~= "" then
    ui.send("coop_join", code)
  else
    ui.send("online_join_code")
  end
  return true
end

-- Joins the first open room of a mode that can be played, or answers quick = "none".
function online.quick_match()
  ui.send("online_quick")
  return true
end

-- The host picks the room's mode (one of online.modes() that is available).
function online.set_mode(id)
  if type(id) ~= "string" then
    return false, "id must be a mode's id"
  end
  ui.send("online_mode", id)
  return true
end

-- A guest marks itself ready or not. Readying over the co-op link first asks which save it brings.
function online.set_ready(ready)
  if type(ready) ~= "boolean" then
    return false, "ready must be true or false"
  end
  -- The lobby's coop_ready toggles.
  if ready ~= flag("online.current.ready") then
    ui.send("coop_ready")
  end
  return true
end

-- The host starts the room's game (it picks its save first).
function online.start()
  ui.send("coop_start")
  return true
end

-- Leaves the room (the host closes it).
function online.leave()
  ui.send("online_leave")
  return true
end

-- Clears the notice, the quick match's answer and a room that closed.
function online.dismiss()
  ui.send("online_dismiss")
  return true
end

-- Switches to the sample rooms and back, when online.state().sample_offered.
function online.set_sample(on)
  if type(on) ~= "boolean" then
    return false, "on must be true or false"
  end
  ui.send("online_sample", on)
  return true
end

-- online.changed: the list, the room or the screens' state changed.
event_source("online.revision", "online.changed")
