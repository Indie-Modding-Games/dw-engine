-- The game mode of a new game (docs/modulos/game/modes.md): the packs' modes, the original's story
-- and those mods add. The title screen shows this document before the new game's rules only when
-- the packs have more than one mode; it publishes new_game_mode.count and new_game_mode.<n>.id,
-- name and description (localization keys) from 0, at most six, and this document sends
-- new_game_mode with the chosen mode's id, or new_game_mode_back. The focused mode's description
-- shows under the list.

local Rows = 6
local focused = 1

local function count()
  return math.min(Rows, math.floor(ui.setting("new_game_mode.count") or 0))
end

local function field(entry, name)
  return ui.setting("new_game_mode." .. entry .. "." .. name) or ""
end

local function describe(document, row)
  local key = row and field(row - 1, "description") or ""
  document:get("about"):set_text(key ~= "" and t(key) or "")
end

local function refresh(document)
  local shown = count()
  for row = 1, Rows do
    local button = document:get("mode-" .. row)
    if row <= shown then
      button:set_text(t(field(row - 1, "name")))
    end
    button:set_class("hidden", row > shown)
  end
  -- The last mode wraps to Back and Back to the first.
  document:get("back"):set_property("nav-up", "#mode-" .. math.max(1, shown))
  describe(document, focused)
end

function mode_focus(document, row)
  focused = row
  describe(document, row)
end

function choose(row)
  if row <= count() then
    ui.send("new_game_mode", field(row - 1, "id"))
  end
end

function back()
  ui.send("new_game_mode_back", true)
end

local function prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

function on_show(document)
  focused = 1
  refresh(document)
  prompts(document)
  document:get("mode-1"):focus()
end

function on_setting(document, name)
  if name == "new_game_mode.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
end

function on_device(document)
  prompts(document)
end

function on_action(_document, action)
  if action == "cancel" then
    back()
    return true
  end
  return false
end
