-- The menu the story opens to pick an item from the bag (script_common.c: openDiscardItem). The
-- game drives it (apps/game/src/discard_screen.cpp) and publishes it as settings, bumping
-- discard.revision once per change: discard.phase (list, confirm, description), .total, .top and
-- .cursor of the list, .choice (0 yes, 1 no), .confirm_key (the "is it OK?" box: the question,
-- then the two answers on one line), .cancellable (whether cancel closes the menu),
-- .description_key, and discard.row.<n>.name/count/choosable/icon/icon_rect for the six rows
-- shown. Every button goes to the game as discard_action.

local Rows = 6
-- renderItemMenuScrollBar: the track is 90 original pixels (3dp each).
local ThumbTrack = 270

local function number(name)
  return math.floor(tonumber(ui.setting(name)) or 0)
end

local function text(name)
  local key = ui.setting(name) or ""
  if key == "" then
    return ""
  end
  return t(key)
end

local function lines_of(value)
  local lines = {}
  for line in string.gmatch(value, "[^\n]+") do
    lines[#lines + 1] = line
  end
  return lines
end

local function draw_list(document)
  local total = number("discard.total")
  local top = number("discard.top")
  for row = 0, Rows - 1 do
    local prefix = "discard.row." .. row .. "."
    local name = ui.setting(prefix .. "name") or ""
    local shown = name ~= ""
    document:get("name-" .. row):set_text(shown and t(name) or "")
    document:get("count-" .. row):set_text(shown and tostring(number(prefix .. "count")) or "")
    -- calculateItemMenuStrings: an item that cannot be picked is red (colour 3).
    document:get("row-" .. row):set_class("stuck", shown and not ui.setting(prefix .. "choosable"))
    local icon = document:get("icon-" .. row)
    local source = shown and (ui.setting(prefix .. "icon") or "") or ""
    icon:set_class("hidden", source == "")
    if source ~= "" then
      icon:set_attribute("src", source)
      icon:set_attribute("rect", ui.setting(prefix .. "icon_rect") or "")
    end
  end
  document
    :get("cursor")
    :set_property("top", string.format("%ddp", 48 + 54 * number("discard.cursor")))
  local height = ThumbTrack
  local offset = 0
  if total > Rows then
    height = ThumbTrack * Rows / total
    offset = ThumbTrack * top / total
  end
  local thumb = document:get("thumb")
  thumb:set_property("height", string.format("%.2fdp", height))
  thumb:set_property("top", string.format("%.2fdp", offset))
end

local function draw_confirm(document)
  local lines = lines_of(text("discard.confirm_key"))
  document:get("question"):set_text(lines[1] or "")
  local answers = {}
  for answer in string.gmatch(lines[2] or "", "%S+") do
    answers[#answers + 1] = answer
  end
  document:get("yes"):set_text(answers[1] or t("common.yes"))
  document:get("no"):set_text(answers[2] or t("common.no"))
  local choice = number("discard.choice")
  document:get("yes"):set_class("selected", choice == 0)
  document:get("no"):set_class("selected", choice == 1)
end

local function refresh(document)
  local phase = ui.setting("discard.phase") or "closed"
  local frame = document:get("frame")
  for _, name in ipairs({ "list", "confirm", "description" }) do
    frame:set_class("phase-" .. name, phase == name)
  end
  document:get("prompt-back"):set_class("hidden", not ui.setting("discard.cancellable"))
  draw_list(document)
  draw_confirm(document)
  document:get("description-text"):set_text(text("discard.description_key"))
end

-- The prompts name the keys of the device in use.
local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
end

function on_show(document)
  refresh(document)
  refresh_prompts(document)
end

function on_setting(document, name)
  if name == "discard.revision" then
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

-- Every button is the game's: the document neither moves a focus nor closes by itself.
function on_action(_document, action)
  ui.send("discard_action", action)
  return true
end
