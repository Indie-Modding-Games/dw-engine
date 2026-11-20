-- The host's battle on a co-op guest's screen (apps/game/src/coop_battles.hpp). The partner of the
-- player who started it fights, and so does this player's own when it was on the same map: this
-- player gives their partner orders (coop_battle.role "starter" or "ally") or, with their partner
-- left out, helps (role "helper"). Everyone cheers (the finisher's charge) and throws items from
-- their own bag. The fighters are coop_battle.fighter.<n>.*, the starter's partner first, then the
-- enemies, then the allies; the orders coop_battle.order.<n>.* and the items
-- coop_battle.item.<n>.*. A partner's Aim button makes it the target of this player's items
-- (coop_battle.target, -1 for none: they fly to this player's own partner, or the fighting one); a
-- fallen partner shows its revive countdown (coop_battle.fighter.<n>.revive_seconds, -1 for none).
-- Everything redraws when coop_battle.revision counts up.
-- The mouse clicks the buttons; the keys and the pad move between them and confirm one.

-- The starter's partner, three enemies and up to three allies.
local MaxFighters = 7
local MaxOrders = 12
local MaxItems = 8

local function number(name)
  local value = ui.setting(name)
  return type(value) == "number" and math.floor(value) or 0
end

local function text(name)
  local value = ui.setting(name)
  return type(value) == "string" and value or ""
end

local function named(key)
  if key == "" then
    return ""
  end
  return t(key)
end

local function draw_fighters(document, aiming)
  local count = number("coop_battle.fighter_count")
  local target = number("coop_battle.target")
  for index = 0, MaxFighters - 1 do
    local row = document:get("fighter-" .. index)
    local shown = index < count
    row:set_class("hidden", not shown)
    if shown then
      local prefix = "coop_battle.fighter." .. index .. "."
      local hp, most = number(prefix .. "hp"), number(prefix .. "max_hp")
      local share = most > 0 and math.max(0, math.min(1, hp / most)) or 0
      local partner = text(prefix .. "side") == "partner"
      row:set_class("partner", partner)
      row:set_class("enemy", not partner)
      row:set_class("mine", ui.setting(prefix .. "mine") == true)
      row:set_class("down", ui.setting(prefix .. "down") == true)
      document:get("fighter-" .. index .. "-name"):set_text(named(text(prefix .. "name_key")))
      document
        :get("fighter-" .. index .. "-fill")
        :set_property("width", string.format("%.1f%%", share * 100))
      document:get("fighter-" .. index .. "-hp"):set_text(string.format("%d/%d", hp, most))
      -- The partners show their MP too; an enemy's stays hidden, as in the battle's own HUD.
      local mp = document:get("fighter-" .. index .. "-mp")
      mp:set_class("hidden", not partner)
      if partner then
        mp:set_text(
          string.format(
            "%s %d/%d",
            t("coop_battle.mp"),
            number(prefix .. "mp"),
            number(prefix .. "max_mp")
          )
        )
      end
      local seconds = number(prefix .. "revive_seconds")
      local revive = document:get("fighter-" .. index .. "-revive")
      revive:set_class("hidden", seconds < 0)
      if seconds >= 0 then
        revive:set_text(t("coop_battle.revive", { seconds = seconds }))
      end
      local aim = document:get("fighter-" .. index .. "-target")
      aim:set_class("hidden", not (partner and aiming))
      aim:set_class("current", target == index)
      aim:set_text(t("coop_battle.target"))
    else
      document:get("fighter-" .. index .. "-target"):set_class("hidden", true)
    end
  end
end

local function draw_orders(document, commands, fighting)
  local count = commands and fighting and number("coop_battle.order_count") or 0
  local current = number("coop_battle.command")
  for index = 0, MaxOrders - 1 do
    local button = document:get("order-" .. index)
    local shown = index < count
    button:set_class("hidden", not shown)
    if shown then
      local prefix = "coop_battle.order." .. index .. "."
      button:set_text(named(text(prefix .. "label_key")))
      button:set_class("current", number(prefix .. "command") == current)
    end
  end
end

local function draw_items(document, fighting)
  local count = number("coop_battle.item_count")
  local usable = fighting and ui.setting("coop_battle.bag") == true
  document:get("items-title"):set_text(t("coop_battle.items"))
  for index = 0, MaxItems - 1 do
    local button = document:get("item-" .. index)
    local shown = index < count
    button:set_class("hidden", not shown)
    if shown then
      local prefix = "coop_battle.item." .. index .. "."
      button:set_text(
        string.format("%s x%d", named(text(prefix .. "name_key")), number(prefix .. "count"))
      )
      button:set_class("unavailable", not usable)
    end
  end
  local none = document:get("no-items")
  none:set_class("hidden", count > 0)
  none:set_text(t("coop_battle.no_items"))
end

local function draw(document)
  local shown = ui.setting("coop_battle.shown") == true
  document:get("panel"):set_class("hidden", not shown)
  if not shown then
    return
  end
  local role = text("coop_battle.role")
  local starter = role == "starter"
  -- The starter and an ally both command their own partner; a helper only cheers and throws.
  local commands = starter or role == "ally"
  local phase = text("coop_battle.phase")
  local fighting = phase == "fight"
  document:get("title"):set_text(t("coop_battle.title_" .. (commands and role or "helper")))
  document:get("phase"):set_text(t("coop_battle.phase_" .. (phase ~= "" and phase or "intro")))
  -- The partners can be aimed at while this player can throw.
  local aiming = fighting
    and ui.setting("coop_battle.bag") == true
    and number("coop_battle.item_count") > 0
  draw_fighters(document, aiming)
  draw_orders(document, commands, fighting)
  draw_items(document, fighting)
  local cheer = document:get("cheer")
  cheer:set_text(t("coop_battle.cheer"))
  cheer:set_class("unavailable", not fighting)
  local close = document:get("close")
  close:set_text(t("coop_battle.close"))
  close:set_class("hidden", not (starter and phase == "end"))
end

-- The buttons the keys move between, in the panel's order: those shown and usable now.
local selected = 1

local function choices(document)
  local list = {}
  local function add(id, run)
    local button = document:get(id)
    if not button:has_class("hidden") and not button:has_class("unavailable") then
      list[#list + 1] = { button = button, run = run }
    end
  end
  for index = 0, MaxOrders - 1 do
    add("order-" .. index, function()
      order(index)
    end)
  end
  add("cheer", function()
    cheer()
  end)
  for index = 0, MaxFighters - 1 do
    add("fighter-" .. index .. "-target", function()
      pick_target(index)
    end)
  end
  for index = 0, MaxItems - 1 do
    add("item-" .. index, function()
      throw_item(index)
    end)
  end
  add("close", function()
    close_end()
  end)
  return list
end

local function place_focus(document)
  local list = choices(document)
  if #list == 0 then
    return
  end
  selected = math.max(1, math.min(#list, selected))
  list[selected].button:focus()
end

function order(index)
  local command = ui.setting("coop_battle.order." .. index .. ".command")
  if type(command) == "number" then
    ui.send("coop_battle_order", command)
  end
end

function cheer()
  ui.send("coop_battle_cheer")
end

function throw_item(index)
  local id = ui.setting("coop_battle.item." .. index .. ".id")
  if type(id) == "string" and id ~= "" and ui.setting("coop_battle.bag") == true then
    ui.send("coop_battle_item", id)
  end
end

function pick_target(index)
  ui.send("coop_battle_target", index)
end

function close_end()
  ui.send("coop_battle_close")
end

function on_show(document)
  draw(document)
  place_focus(document)
end

function on_setting(document, name)
  if name == "coop_battle.revision" or name == "coop_battle.shown" then
    -- Each battle starts on the first button.
    if text("coop_battle.phase") == "intro" then
      selected = 1
    end
    draw(document)
    place_focus(document)
  end
end

function on_action(document, action)
  if action == "up" or action == "left" then
    selected = selected - 1
    place_focus(document)
  elseif action == "down" or action == "right" then
    selected = selected + 1
    place_focus(document)
  elseif action == "confirm" then
    local list = choices(document)
    if list[selected] ~= nil then
      list[selected].run()
    end
  end
  return true
end

function on_language(document)
  draw(document)
end
