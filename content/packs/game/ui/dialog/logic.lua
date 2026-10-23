-- The save dialog, at the title screen and in the game: a question (yes or no) or a notice
-- (accept) about the save slots. The game publishes dialog.text (a text key), dialog.file (the save
-- file it is about, or "") and dialog.question, and shows this document; it sends dialog_answer,
-- true only for yes. Cancel answers no.

local function question()
  return ui.setting("dialog.question") == true
end

local function refresh(document)
  document:get("text"):set_text(t(ui.setting("dialog.text") or ""))
  local file = ui.setting("dialog.file") or ""
  document:get("file"):set_text(file)
  document:get("file"):set_class("hidden", file == "")
  document:get("yes"):set_class("hidden", not question())
  document:get("no"):set_class("hidden", not question())
  document:get("ok"):set_class("hidden", question())
end

function answer(yes)
  ui.send("dialog_answer", yes)
end

function on_show(document)
  refresh(document)
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
  -- A question starts on no, so that confirming in a hurry does not overwrite a save.
  document:get(question() and "no" or "ok"):focus()
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
end

function on_device(document)
  document:get("prompt-confirm"):set_text(ui.prompt("confirm"))
end

function on_action(_document, action)
  if action == "cancel" or action == "menu" then
    answer(false)
    return true
  end
  return false
end
