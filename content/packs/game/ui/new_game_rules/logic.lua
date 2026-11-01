-- The rules of a new game (docs/modulos/game/evolution.md, «Contenido oculto»): the original's, or
-- the original's with its hidden content (Panjyamon, Gigadramon and MetalEtemon by evolution), and
-- with it the Ultimate to Mega step. Both start off. The title screen shows this document before
-- the new game starts; it sends new_game_rules with the ruleset id the choice names (original,
-- oculto or oculto_mega), or new_game_rules_back. The save records the ruleset. A test route
-- reaches it only when it asks (new_game_rules: true); the others start with the original's rules.

local hidden = false
local mega = false

local function yes_no(on)
  return t(on and "common.yes" or "common.no")
end

local function refresh(document)
  document:get("rules-hidden"):set_text(t("new_game_rules.hidden", { value = yes_no(hidden) }))
  document
    :get("about-hidden")
    :set_text(t(hidden and "new_game_rules.hidden_on" or "new_game_rules.hidden_off"))
  document:get("rules-mega"):set_text(t("new_game_rules.mega", { value = yes_no(mega) }))
  document:get("about-mega"):set_text(t("new_game_rules.mega_about"))
  -- The Mega step is part of the hidden content, when the packs have its ruleset
  -- (new_game_rules.mega, published by the game).
  local offered = hidden and ui.setting("new_game_rules.mega") == true
  document:get("rules-mega"):set_class("hidden", not offered)
  document:get("about-mega"):set_class("hidden", not offered)
end

local function ruleset()
  if not hidden then
    return "original"
  end
  return mega and "oculto_mega" or "oculto"
end

function toggle(document, which)
  if which == "hidden" then
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
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
  document:get("prompt-cancel"):set_text(ui.prompt("cancel"))
end

function on_show(document)
  hidden = false
  mega = false
  refresh(document)
  prompts(document)
  document:get("rules-start"):focus()
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
