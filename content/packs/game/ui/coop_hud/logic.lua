-- The co-op game's own HUD (apps/game/src/coop_hud.hpp). The tag: the other player's name over
-- their head while the field runs (coop_hud.tag.visible), at coop_hud.tag.x and coop_hud.tag.y,
-- in percent of the window; in a room of more than two, the others' tags are coop_hud.tag.2.* and
-- coop_hud.tag.3.*. The notice: a short line at the top (coop_hud.notice.shown) from the
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

-- One tag: "tag" with coop_hud.tag.*, or "tag-<n>" with coop_hud.tag.<n>.* (a room of up to four
-- has a tag over each of the other three players).
local function draw_one_tag(document, id, prefix)
  local tag = document:get(id)
  local visible = ui.setting(prefix .. "visible") == true
  tag:set_class("hidden", not visible)
  if not visible then
    return
  end
  local x, y = ui.setting(prefix .. "x"), ui.setting(prefix .. "y")
  tag:set_property("left", string.format("%.2f%%", type(x) == "number" and x or 0))
  tag:set_property("top", string.format("%.2f%%", type(y) == "number" and y or 0))
  document:get(id .. "-name"):set_text(
    named(
      ui.setting(prefix .. "name"),
      ui.setting(prefix .. "name_key"),
      ui.setting(prefix .. "number")
    )
  )
end

local function draw_tag(document)
  draw_one_tag(document, "tag", "coop_hud.tag.")
  draw_one_tag(document, "tag-2", "coop_hud.tag.2.")
  draw_one_tag(document, "tag-3", "coop_hud.tag.3.")
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
