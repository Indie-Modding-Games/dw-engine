-- The UI API's dialog (docs/modding/ui-api.md, namespace dialog): the save dialog, at the title
-- screen and in the game (apps/game: save_flow.cpp, title_screen.cpp, sleep_screen.cpp,
-- play_session.cpp), a question (yes or no) or a notice (accept) about the save slots, and its
-- answer.
--
-- Every function returns a fresh table; changing it changes nothing in the game.

dialog = {}

-- The dialog: text_key (its text's key), file (the save file it is about, or ""), question (yes or
-- no; else a notice), start_on_yes (the game's question the original asks starting on yes; the
-- others start on no, so that a hurried confirm overwrites nothing), at_title (shown by the title
-- screen) and options (dialog.options()).
function dialog.state()
  local file = ui.setting("dialog.file")
  local text = ui.setting("dialog.text")
  return {
    text_key = type(text) == "string" and text or "",
    file = type(file) == "string" and file or "",
    question = ui.setting("dialog.question") == true,
    start_on_yes = ui.setting("dialog.start_on_yes") == true,
    at_title = ui.setting("dialog.at_title") == true,
    options = dialog.options(),
  }
end

-- Its options in order, each {id, label_key, yes}: yes and no for a question, accept for a notice.
-- yes says whether picking it answers yes.
function dialog.options()
  if ui.setting("dialog.question") == true then
    return {
      { id = "yes", label_key = "common.yes", yes = true },
      { id = "no", label_key = "common.no", yes = false },
    }
  end
  return { { id = "ok", label_key = "common.accept", yes = false } }
end

-- Whether the dialog is on screen, waiting for its answer.
function dialog.is_open()
  return ui.is_visible("dialog")
end

-- Answers with one of dialog.options(), from 0.
function dialog.answer(option)
  if not dialog.is_open() then
    return false, "no dialog waits for an answer"
  end
  local options = dialog.options()
  local whole = type(option) == "number" and option == math.floor(option)
  if not whole or option < 0 or option >= #options then
    return false, string.format("option must be a whole number from 0 to %d", #options - 1)
  end
  ui.send("dialog_answer", options[option + 1].yes)
  return true
end

-- Cancel: no to a question, accept to a notice.
function dialog.cancel()
  if not dialog.is_open() then
    return false, "no dialog waits for an answer"
  end
  ui.send("dialog_answer", false)
  return true
end

-- dialog.changed: the game set a dialog's text and options (before it shows it, or while it does).
event_source("dialog.revision", "dialog.changed")
