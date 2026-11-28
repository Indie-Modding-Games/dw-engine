-- The end of the host's battle on a co-op guest's screen (apps/game/src/coop_battles.hpp). The
-- fight itself shows in the battle document, as the host's own HUD; this box only comes with the
-- end box: the starter closes it, the others wait for the starter (coop_battle.waiting).
-- Everything redraws when coop_battle.revision counts up.

local function number(name)
  local value = ui.setting(name)
  return type(value) == "number" and math.floor(value) or 0
end

local function text(name)
  local value = ui.setting(name)
  return type(value) == "string" and value or ""
end

local function starter_name()
  local name = text("coop_battle.starter.name")
  if name ~= "" then
    return name
  end
  local key = text("coop_battle.starter.name_key")
  if key == "" then
    return ""
  end
  return (string.gsub(t(key), "{number}", tostring(number("coop_battle.starter.number"))))
end

local function phase_text(phase)
  if ui.setting("coop_battle.waiting") == true then
    return t("coop_battle.phase_waiting", { name = starter_name() })
  end
  return t("coop_battle.phase_" .. (phase ~= "" and phase or "intro"))
end

local function closes()
  return text("coop_battle.role") == "starter" and text("coop_battle.phase") == "end"
end

local function draw(document)
  local shown = ui.setting("coop_battle.shown") == true
  document:get("panel"):set_class("hidden", not shown)
  if not shown then
    return
  end
  local role = text("coop_battle.role")
  local commands = role == "starter" or role == "ally"
  document:get("title"):set_text(t("coop_battle.title_" .. (commands and role or "helper")))
  document:get("phase"):set_text(phase_text(text("coop_battle.phase")))
  local close = document:get("close")
  close:set_text(t("coop_battle.close"))
  close:set_class("hidden", not closes())
  if closes() then
    close:focus()
  end
end

function close_end()
  ui.send("coop_battle_close")
end

function on_show(document)
  draw(document)
end

function on_setting(document, name)
  if name == "coop_battle.revision" or name == "coop_battle.shown" then
    draw(document)
  end
end

function on_action(_, action)
  if action == "confirm" and closes() then
    close_end()
  end
  return true
end

function on_language(document)
  draw(document)
end
