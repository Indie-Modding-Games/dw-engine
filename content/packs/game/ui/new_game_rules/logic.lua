-- The rules of a new game (docs/modulos/game/evolution.md, «Contenido oculto»): the original's, or
-- the original's with its hidden content (Panjyamon, Gigadramon, MetalEtemon and Myotismon by
-- evolution), and with it the Ultimate to Mega step. Both start off. Every species as a partner
-- (docs/modulos/game/evoluciones-extra.md) starts on and goes with any of them. The title screen
-- shows this document before the new game starts; it sends new_game_rules with the ruleset id the
-- choice names (original, oculto, oculto_mega, or todas, todas_oculto, todas_oculto_mega), or
-- new_game_rules_back. The save records the ruleset.
-- A test route reaches it only when it asks (new_game_rules: true); the others start with the
-- original's rules.

local hidden = false
local mega = false
local all = true

local function yes_no(on)
  return t(on and "common.yes" or "common.no")
end

-- A rule's line in its button: the text before the value, the value (yes or no, in a span of its
-- own that a skin may colour, .rule-value) and the text after it.
local value_mark = "\1"
local function set_rule(document, id, key, on)
  local line = t(key, { value = value_mark })
  local at = string.find(line, value_mark, 1, true)
  local before, after = line, ""
  if at ~= nil then
    before, after = string.sub(line, 1, at - 1), string.sub(line, at + #value_mark)
  end
  document:get(id .. "-label"):set_text(before)
  document:get(id .. "-value"):set_text(at ~= nil and yes_no(on) or "")
  document:get(id .. "-after"):set_text(after)
end

-- Every species as a partner, when the packs have its rulesets (new_game_rules.all, published by
-- the game).
local function all_offered()
  return ui.setting("new_game_rules.all") == true
end

local function refresh(document)
  local all_shown = all_offered()
  set_rule(document, "rules-all", "new_game_rules.all", all)
  document:get("about-all"):set_text(t(all and "new_game_rules.all_on" or "new_game_rules.all_off"))
  document:get("rules-all"):set_class("hidden", not all_shown)
  document:get("about-all"):set_class("hidden", not all_shown)
  set_rule(document, "rules-hidden", "new_game_rules.hidden", hidden)
  document
    :get("about-hidden")
    :set_text(t(hidden and "new_game_rules.hidden_on" or "new_game_rules.hidden_off"))
  set_rule(document, "rules-mega", "new_game_rules.mega", mega)
  document:get("about-mega"):set_text(t("new_game_rules.mega_about"))
  -- The Mega step is part of the hidden content, when the packs have its ruleset
  -- (new_game_rules.mega, published by the game).
  local offered = hidden and ui.setting("new_game_rules.mega") == true
  document:get("rules-mega"):set_class("hidden", not offered)
  document:get("about-mega"):set_class("hidden", not offered)
end

local function ruleset()
  local base = "original"
  if hidden then
    base = mega and "oculto_mega" or "oculto"
  end
  if not (all and all_offered()) then
    return base
  end
  return base == "original" and "todas" or "todas_" .. base
end

function toggle(document, which)
  if which == "all" then
    all = all_offered() and not all
  elseif which == "hidden" then
    hidden = not hidden
    if not hidden then
      mega = false
    end
  else
    mega = hidden and ui.setting("new_game_rules.mega") == true and not mega
  end
  refresh(document)
end

function start()
  ui.send("new_game_rules", ruleset())
end

function back()
  ui.send("new_game_rules_back", true)
end

local function prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

-- The rules' box over the title's picture with the «widescreen pictures» option
-- (../common/title_picture.lua), at the original's place across it.
local PictureBoxes = { { { id = "panel", x = 60, top = 150 } } }

function on_show(document)
  title_picture.apply(document, PictureBoxes)
  hidden = false
  mega = false
  all = true
  refresh(document)
  prompts(document)
  document:get("rules-start"):focus()
end

function on_setting(document, _name)
  title_picture.apply(document, PictureBoxes)
end

function on_tick(document, _seconds)
  title_picture.apply(document, PictureBoxes)
end

function on_language(document)
  refresh(document)
end

function on_device(document)
  prompts(document)
end

function on_action(_document, action)
  -- The menu key goes back as cancel does: left to the UI it would close the box without telling
  -- the game, over a title that is already gone.
  if action == "cancel" or action == "menu" then
    back()
    return true
  end
  return false
end
