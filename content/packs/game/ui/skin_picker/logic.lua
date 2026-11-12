-- The UI skin of the player's first run (docs/architecture/ui.md, «Elegir el aspecto»): before the
-- language box, the title screen shows this box once, while no skin was chosen. One row per skin
-- the packs offer (ui.skins(), the options page's list and order), its name and a one-line hint
-- (the name's key plus ".hint", none when the texts lack it) in the language the game guessed from
-- the system's. Moving the cursor puts the skin on at once (ui_skin_preview, not saved); confirming
-- sends ui_skin, which saves it like the options page's row, and skin_picker_done, and the title
-- goes on. There is no cancel: there is nothing before it to go back to.
-- A test route reaches it only when it asks (skin_first: true).

local Rows = 6

local function skins()
  local offered = ui.skins()
  local shown = {}
  for index = 1, math.min(Rows, #offered) do
    shown[index] = offered[index]
  end
  return shown
end

local function hint_of(skin)
  local key = ui.skin_name_key(skin)
  if key == "" then
    return ""
  end
  local text = t(key .. ".hint")
  return text ~= key .. ".hint" and text or ""
end

local function refresh(document)
  local shown = skins()
  for row = 1, Rows do
    local skin = shown[row]
    local key = skin ~= nil and ui.skin_name_key(skin) or ""
    document:get("skin-name-" .. row):set_text(key ~= "" and t(key) or (skin or ""))
    document:get("skin-hint-" .. row):set_text(skin ~= nil and hint_of(skin) or "")
    document:get("skin-" .. row):set_class("hidden", skin == nil)
  end
  -- The last row wraps to the first and the first to the last.
  document:get("skin-1"):set_property("nav-up", "#skin-" .. math.max(1, #shown))
  document:get("skin-" .. math.max(1, #shown)):set_property("nav-down", "#skin-1")
end

-- The row of the skin shown, or the first.
local function current_row()
  for row, skin in ipairs(skins()) do
    if skin == ui.setting("ui_skin") then
      return row
    end
  end
  return 1
end

function skin_focus(row)
  local skin = skins()[tonumber(row)]
  if skin ~= nil and skin ~= ui.setting("ui_skin") then
    ui.send("ui_skin_preview", skin)
  end
end

function choose(row)
  local skin = skins()[tonumber(row)]
  if skin ~= nil then
    ui.send("ui_skin", skin)
    ui.send("skin_picker_done")
  end
end

local function prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
end

function on_show(document)
  refresh(document)
  prompts(document)
  document:get("skin-" .. current_row()):focus()
end

function on_language(document)
  refresh(document)
end

function on_device(document)
  prompts(document)
end

function on_action(_document, action)
  -- No cancel: the box stays until a skin is confirmed. The menu key too, or the UI would close the
  -- box without skin_picker_done and leave the title empty.
  return action == "cancel" or action == "menu"
end
