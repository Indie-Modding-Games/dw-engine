-- Every fighter's HP while a battle runs: the partner first, then the enemies, from the UI API
-- (battle.state(), battle.fighters()). It reads the battle and asks nothing of it, so it needs no
-- command, and it redraws on the battle's events. It steps aside while the bag is open over the
-- fight, whose window takes the bottom of the screen in some skins.

local rows = 4

local function draw(document)
  local state = battle.state()
  local shown = state.active
    and (state.phase == "intro" or state.phase == "fight")
    and not state.bag.open
  document:get("panel"):set_class("hidden", not shown)
  if not shown then
    return
  end
  local fighters = battle.fighters()
  for index = 1, rows do
    local fighter = fighters[index]
    local row = document:get("row-" .. index)
    row:set_class("hidden", fighter == nil)
    if fighter ~= nil then
      local name = fighter.name_key ~= "" and t(fighter.name_key) or ""
      document:get("name-" .. index):set_text(name)
      document:get("hp-" .. index):set_text(t("battle.number_icon.hp") .. " " .. fighter.hp)
      local width = fighter.max_hp > 0 and math.floor(fighter.hp * 100 / fighter.max_hp) or 0
      document:get("fill-" .. index):set_property("width", math.max(0, width) .. "%")
      row:set_class("fainted", fighter.fainted)
    end
  end
end

function on_show(document)
  draw(document)
end

-- A plugin's document is always shown: each event redraws it.
for _, event in ipairs({ "battle.started", "battle.ended", "battle.phase", "battle.changed" }) do
  hooks.on(event, function(_, document)
    draw(document)
  end)
end
