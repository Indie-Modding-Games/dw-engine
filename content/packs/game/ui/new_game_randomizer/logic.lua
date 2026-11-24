-- The randomizer's setup (docs/modules/game/randomizer.md): after a new game's mode and rules, for
-- a mode that offers it. The game publishes new_game_randomizer.evolutions, learn (original, 25,
-- 50, 75 or 100), starter, enemies, items, drops, seed, code, active, editing, draft and invalid,
-- and counts up new_game_randomizer.revision on each change; this document sends
-- new_game_randomizer with what the player did: toggle:<option>, learn, preset:off, preset:chaos,
-- edit, erase, commit, paste, reseed, start or back. While the seed takes letters the keyboard
-- types them, cancel erases the last one or, with none left, keeps the seed as it was.

local Options = { "evolutions", "starter", "enemies", "items", "drops" }
local focused = "preset-off"

local function setting(name)
  return ui.setting("new_game_randomizer." .. name)
end

local function editing()
  return setting("editing") == true
end

function act(action)
  ui.send("new_game_randomizer", action)
end

function edit_seed()
  act(editing() and "commit" or "edit")
end

-- A row's line: the text before the value, the value in a span a skin may colour (.rule-value)
-- and the text after it.
local value_mark = "\1"
local function set_row(document, id, key, value)
  local line = t(key, { value = value_mark })
  local at = string.find(line, value_mark, 1, true)
  local before, after = line, ""
  if at ~= nil then
    before, after = string.sub(line, 1, at - 1), string.sub(line, at + #value_mark)
  end
  document:get(id .. "-label"):set_text(before)
  document:get(id .. "-value"):set_text(at ~= nil and value or "")
  document:get(id .. "-after"):set_text(after)
end

local function about_key(row)
  if row == "seed" and setting("invalid") == true then
    return "new_game_randomizer.seed_invalid"
  end
  if row == "seed" and editing() then
    return "new_game_randomizer.seed_typing"
  end
  if row == "back" then
    return nil
  end
  return "new_game_randomizer.about_" .. string.gsub(row, "-", "_")
end

local function describe(document)
  local key = about_key(focused)
  document:get("about"):set_text(key ~= nil and t(key) or "")
end

local function chaos()
  for _, name in ipairs(Options) do
    if setting(name) ~= true then
      return false
    end
  end
  return setting("learn") == "100"
end

local function refresh(document)
  for _, name in ipairs(Options) do
    set_row(
      document,
      name,
      "new_game_randomizer." .. name,
      t(setting(name) == true and "common.yes" or "common.no")
    )
  end
  set_row(
    document,
    "learn",
    "new_game_randomizer.learn",
    t("new_game_randomizer.learn_" .. (setting("learn") or "original"))
  )
  local seed = editing() and ((setting("draft") or "") .. "_") or (setting("seed") or "")
  set_row(document, "seed", "new_game_randomizer.seed", seed)
  document:get("seed"):set_class("editing", editing())
  document:get("preset-off"):set_class("selected", setting("active") ~= true)
  document:get("preset-chaos"):set_class("selected", chaos())
  local code = setting("active") == true and "new_game_randomizer.code"
    or "new_game_randomizer.code_off"
  document:get("code"):set_text(t(code, { code = setting("code") or "" }))
  describe(document)
end

function row_focus(document, row)
  focused = row
  describe(document)
end

local function prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

function on_show(document)
  focused = "preset-off"
  refresh(document)
  prompts(document)
  document:get("preset-off"):focus()
end

function on_setting(document, name)
  if name == "new_game_randomizer.revision" then
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
  -- While the seed takes letters, the keyboard's letters make steps too (F is confirm): only
  -- cancel erases; a gamepad's confirm keeps the letters, the keyboard's enter does in the game.
  if editing() then
    if action == "cancel" or action == "menu" then
      act("erase")
    elseif action == "confirm" and ui.device() == "gamepad" then
      act("commit")
    end
    return true
  end
  -- The menu key goes back as cancel does: left to the UI it would close the box without telling
  -- the game, over a title that is already gone.
  if action == "cancel" or action == "menu" then
    act("back")
    return true
  end
  return false
end
