-- The co-op game's own HUD (apps/game/src/coop_hud.hpp). The tag: the other player's name over
-- their head while the field runs (coop_hud.tag.visible), at coop_hud.tag.x and coop_hud.tag.y,
-- in percent of the window; in a room of more than two, the others' tags are coop_hud.tag.2.* and
-- coop_hud.tag.3.*. The notice: a short line at the top (coop_hud.notice.shown) from the
-- text key coop_hud.notice.text, where "{name}" stands for the player it is about. A player is
-- named by their own name (*.name), else by a text key (*.name_key) whose "{number}" takes
-- *.number. Under each name, the trade key's hint while that player can be asked for a trade
-- (coop_hud.tag.trade, coop_hud.tag.<n>.trade), and a line while that player trains at the gym
-- (coop_hud.tag.training, coop_hud.tag.<n>.training). Each part redraws when its revision counts
-- up.

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
  local training = document:get(id .. "-training")
  local trains = ui.setting(prefix .. "training") == true
  training:set_class("hidden", not trains)
  if trains then
    training:set_text(t("online.coop.training"))
  end
  local trade = document:get(id .. "-trade")
  local hint = ui.setting(prefix .. "trade") == true
  trade:set_class("hidden", not hint)
  if hint then
    trade:set_prompt_text(t("coop_trade.hint", { key = "{prompt:trade}" }), false)
  end
end

local function draw_tag(document)
  draw_one_tag(document, "tag", "coop_hud.tag.")
  draw_one_tag(document, "tag-2", "coop_hud.tag.2.")
  draw_one_tag(document, "tag-3", "coop_hud.tag.3.")
end

local TagIds = { "tag", "tag-2", "tag-3" }
local TagGap = 4
local lifts = {}

local function stack_tags(document)
  local shown = {}
  for index, id in ipairs(TagIds) do
    local tag = document:get(id)
    local x, y, width, height = tag:rect()
    if not tag:has_class("hidden") and width > 0 then
      shown[#shown + 1] = {
        id = id,
        index = index,
        tag = tag,
        left = x - width / 2,
        right = x + width / 2,
        bottom = y + (lifts[id] or 0),
        height = height,
      }
    else
      lifts[id] = nil
    end
  end
  table.sort(shown, function(one, other)
    if one.bottom ~= other.bottom then
      return one.bottom > other.bottom
    end
    return one.index < other.index
  end)
  local placed = {}
  for _, entry in ipairs(shown) do
    local bottom = entry.bottom
    local moved = true
    while moved do
      moved = false
      for _, other in ipairs(placed) do
        local across = entry.left < other.right and other.left < entry.right
        if across and bottom - entry.height < other.bottom and other.top < bottom then
          bottom = other.top - TagGap
          moved = true
        end
      end
    end
    placed[#placed + 1] =
      { left = entry.left, right = entry.right, top = bottom - entry.height, bottom = bottom }
    local lift = entry.bottom - bottom
    if math.abs(lift - (lifts[entry.id] or 0)) > 0.5 then
      lifts[entry.id] = lift
      entry.tag:set_property("margin-top", string.format("%.1fdp", -lift))
    end
  end
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

function on_tick(document)
  stack_tags(document)
end
