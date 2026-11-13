-- The save dialog, at the title screen and in the game, drawn from the UI API's dialog namespace
-- (docs/modding/ui-api.md): dialog.state() is a question (yes or no) or a notice (accept) about the
-- save slots, with the save file it is about and whether the title screen shows it (which a skin
-- can style apart: the scrim's at-title class); dialog.answer(option) answers it. Cancel answers
-- no.

local function refresh(document)
  local state = dialog.state()
  document:get("text"):set_text(t(state.text_key))
  document:get("file"):set_text(state.file)
  document:get("file"):set_class("hidden", state.file == "")
  document:get("yes"):set_class("hidden", not state.question)
  document:get("no"):set_class("hidden", not state.question)
  document:get("ok"):set_class("hidden", state.question)
  local scrim = document:get("scrim")
  scrim:set_class("at-title", state.at_title)
  scrim:set_class("question", state.question)
  scrim:set_class("notice", not state.question)
end

-- The buttons' answers: the option of dialog.options() with that id.
function answer(id)
  for index, option in ipairs(dialog.options()) do
    if option.id == id then
      dialog.answer(index - 1)
      return
    end
  end
end

function on_show(document)
  refresh(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  -- A question starts on no, so that confirming in a hurry does not overwrite a save, unless the
  -- game says it starts on yes (the new game's question, as the original).
  local state = dialog.state()
  local first = state.start_on_yes and "yes" or "no"
  document:get(state.question and first or "ok"):focus()
end

hooks.on("dialog.changed", function(_, document)
  if ui.is_visible(document_name) then
    refresh(document)
  end
end)

function on_language(document)
  refresh(document)
end

function on_device(document)
  document:get("prompt-confirm"):set_prompt("confirm")
end

function on_action(_document, action)
  if action == "cancel" or action == "menu" then
    dialog.cancel()
    return true
  end
  return false
end
