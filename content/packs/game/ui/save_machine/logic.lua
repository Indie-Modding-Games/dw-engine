-- The save machine of the field (main_menu.c: openSaveMachine, tickMainMenu 0x3C to 0x46): the
-- partner's registration for versus battles and the save. The game publishes, and bumps
-- save_machine.revision:
--   save_machine.view: register, slots, confirm_register, no_slot, in_training, save_question,
--     save_dialog or closed;
--   save_machine.chosen_slot: the registration slot chosen last, from 0;
--   save_machine.slots: how many slots the list reaches (39);
--   save_machine.name, .species (a text key), .hp, .mp, .offense, .defense and
--     save_machine.technique.<n>.name (a text key, or empty): the partner as it would be
--     registered;
--   save_machine.slot.<n>.taken, .name and .species: the registered partners;
--   save_machine.dialog_text (a text key), .dialog_file, .dialog_question and
--     .dialog_start_on_yes: the save's question or notice in the save_dialog view.
-- It sends save_machine_choose with the row confirmed (yes is 0, no is 1, a slot its number) and
-- save_machine_cancel. As the original's menus (tickMenuInput), the cursor stops at the ends and
-- the notices take any button.

local Rows = 10
local view = ""
-- The answers' cursor (0 yes, 1 no) and the slot list's: the slot under it and the first row shown.
local answer = 0
local slot = 0
local top = 0

local function number(name)
  return math.floor(ui.setting(name) or 0)
end

local function text(name)
  return ui.setting(name) or ""
end

local function key_text(name)
  local key = text(name)
  return key ~= "" and t(key) or ""
end

local function slots()
  return math.max(1, number("save_machine.slots"))
end

local function question()
  return ui.setting("save_machine.dialog_question") == true
end

-- The boxes each view shows (main_menu.c: MENU_VIEWS and renderMainMenu).
local Boxes = {
  register = { "heading", "stats", "register-question", "answers" },
  slots = { "heading", "slots", "slots-prompt" },
  confirm_register = { "heading", "confirm-question", "answers", "confirm-note" },
  no_slot = { "no-slot" },
  in_training = { "in-training" },
  save_question = { "save-question", "answers" },
  save_dialog = { "save-dialog", "answers" },
}
local AllBoxes = {
  "heading",
  "stats",
  "register-question",
  "slots",
  "slots-prompt",
  "confirm-question",
  "confirm-note",
  "no-slot",
  "in-training",
  "save-question",
  "save-dialog",
  "answers",
}

local function answering()
  return view == "register"
    or view == "confirm_register"
    or view == "save_question"
    or (view == "save_dialog" and question())
end

local function notice()
  return view == "no_slot" or view == "in_training" or (view == "save_dialog" and not question())
end

local function fill_stats(document)
  document:get("stat-name"):set_text(text("save_machine.name"))
  document:get("stat-species"):set_text(key_text("save_machine.species"))
  for _, stat in ipairs({ "hp", "mp", "offense", "defense" }) do
    document:get("stat-" .. stat):set_text(tostring(number("save_machine." .. stat)))
  end
  for place = 0, 2 do
    local move = key_text("save_machine.technique." .. place .. ".name")
    document:get("stat-technique-" .. place):set_text(move)
  end
end

local function fill_slots(document)
  for row = 1, Rows do
    local index = top + row - 1
    local taken = ui.setting("save_machine.slot." .. index .. ".taken") == true
    local entry = document:get("slot-" .. row)
    entry:set_class("hidden", index >= slots())
    entry:set_class("empty", not taken)
    local name = taken and text("save_machine.slot." .. index .. ".name")
      or t("save.machine_empty_slot")
    document:get("slot-" .. row .. "-name"):set_text(name)
    local species = key_text("save_machine.slot." .. index .. ".species")
    document:get("slot-" .. row .. "-species"):set_text(species)
  end
end

local function fill_texts(document)
  document:get("confirm-name"):set_text(text("save_machine.name"))
  document:get("in-training-line"):set_text(t("save.machine_in_training", {
    name = text("save_machine.name"),
    species = key_text("save_machine.species"),
  }))
  document:get("dialog-text"):set_text(key_text("save_machine.dialog_text"))
  local file = text("save_machine.dialog_file")
  document:get("dialog-file"):set_text(file)
  document:get("dialog-file"):set_class("hidden", file == "")
end

-- The cursor's outline on the row it is on.
local function focus(document)
  if view == "slots" then
    document:get("slot-" .. (slot - top + 1)):focus()
  elseif answering() then
    document:get(answer == 0 and "yes" or "no"):focus()
  elseif view == "save_dialog" then
    document:get("ok"):focus()
  end
end

-- A view just opened: its cursor where the original's starts (yes, the overwrite question on no,
-- the list on the slot chosen last).
local function enter()
  answer = 0
  local on_yes = ui.setting("save_machine.dialog_start_on_yes") == true
  if view == "save_dialog" and question() and not on_yes then
    answer = 1
  end
  slot = math.min(number("save_machine.chosen_slot"), slots() - 1)
  top = math.max(0, math.min(slot, slots() - Rows))
end

local function refresh(document)
  local current = text("save_machine.view")
  local opened = current ~= view
  view = current
  if opened then
    enter()
  end
  local shown = {}
  for _, box in ipairs(Boxes[view] or {}) do
    shown[box] = true
  end
  for _, box in ipairs(AllBoxes) do
    document:get(box):set_class("hidden", not shown[box])
  end
  for name, _ in pairs(Boxes) do
    document:get("scrim"):set_class("view-" .. name, name == view)
  end
  local yes_no = answering()
  document:get("yes"):set_class("hidden", not yes_no)
  document:get("no"):set_class("hidden", not yes_no)
  document:get("ok"):set_class("hidden", yes_no)
  document:get("prompt-back"):set_class("hidden", notice() or view == "save_question")
  fill_stats(document)
  fill_slots(document)
  fill_texts(document)
  focus(document)
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

function choose_row(_document, row)
  if view == "slots" then
    ui.send("save_machine_choose", top + row)
  else
    ui.send("save_machine_choose", row)
  end
end

-- One row up or down, stopping at the ends; true when the cursor moved.
local function move(document, step)
  if view == "slots" then
    local next = slot + step
    if next < 0 or next >= slots() then
      return false
    end
    slot = next
    if slot < top then
      top = slot
    elseif slot >= top + Rows then
      top = slot - Rows + 1
    end
    fill_slots(document)
    focus(document)
    return true
  end
  if answering() then
    local next = answer + step
    if next < 0 or next > 1 then
      return false
    end
    answer = next
    focus(document)
    return true
  end
  return false
end

function on_show(document)
  view = ""
  refresh(document)
  refresh_prompts(document)
end

-- The game publishes a view's settings in one batch and counts up save_machine.revision last: a
-- view opened halfway would pick its cursor from the last view's question.
function on_setting(document, name)
  if name == "save_machine.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
  refresh_prompts(document)
end

function on_device(document)
  refresh_prompts(document)
end

function on_action(document, action)
  if action == "up" or action == "down" then
    -- A step past the ends is left to the rows, which have no navigation: no move and no sound.
    return move(document, action == "up" and -1 or 1)
  end
  if action == "confirm" then
    if view == "slots" then
      ui.send("save_machine_choose", slot)
    elseif notice() then
      ui.send("save_machine_choose", 0)
    else
      ui.send("save_machine_choose", answer)
    end
    return true
  end
  if action == "cancel" or action == "menu" then
    ui.send("save_machine_cancel", true)
    return true
  end
  return false
end
