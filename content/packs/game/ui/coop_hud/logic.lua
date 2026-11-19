-- The co-op game's own HUD (apps/game/src/coop_hud.hpp). The tag: the other player's name over
-- their head while the field runs (coop_hud.tag.visible), at coop_hud.tag.x and coop_hud.tag.y,
-- in percent of the window. The notice: a short line at the top (coop_hud.notice.shown) from the
-- text key coop_hud.notice.text, where "{name}" stands for the player it is about. A player is
-- named by their own name (*.name), else by a text key (*.name_key) whose "{number}" takes
-- *.number. Each part redraws when its revision counts up.

local function named(name, key, number)
  if type(name) == "string" and name ~= "" then
    return name
  end
  if type(key) ~= "string" or key == "" then
    return ""
  end
  local count = type(number) == "number" and math.floor(number) or 0
  return (string.gsub(t(key), "{number}", tostring(count)))
end

local function draw_tag(document)
  local tag = document:get("tag")
  local visible = ui.setting("coop_hud.tag.visible") == true
  tag:set_class("hidden", not visible)
  if not visible then
    return
  end
  local x, y = ui.setting("coop_hud.tag.x"), ui.setting("coop_hud.tag.y")
  tag:set_property("left", string.format("%.2f%%", type(x) == "number" and x or 0))
  tag:set_property("top", string.format("%.2f%%", type(y) == "number" and y or 0))
  document:get("tag-name"):set_text(
    named(
      ui.setting("coop_hud.tag.name"),
      ui.setting("coop_hud.tag.name_key"),
      ui.setting("coop_hud.tag.number")
    )
  )
end

local function draw_notice(document)
  local shown = ui.setting("coop_hud.notice.shown") == true
  document:get("notice"):set_class("hidden", not shown)
  local key = ui.setting("coop_hud.notice.text")
  if not shown or type(key) ~= "string" or key == "" then
    return
  end
  local who = named(
    ui.setting("coop_hud.notice.name"),
    ui.setting("coop_hud.notice.name_key"),
    ui.setting("coop_hud.notice.number")
  )
  -- A function as the replacement keeps a "%" in a player's name as it is.
  local text = string.gsub(t(key), "{name}", function()
    return who
  end)
  document:get("notice-text"):set_text(text)
end

function on_show(document)
  draw_tag(document)
  draw_notice(document)
end

function on_setting(document, name)
  if name == "coop_hud.tag.revision" then
    draw_tag(document)
  elseif name == "coop_hud.notice.revision" then
    draw_notice(document)
  end
end

function on_language(document)
  draw_tag(document)
  draw_notice(document)
end
