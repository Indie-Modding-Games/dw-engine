-- The box is the story's speech box (tamer.c: renderAwardSomethingTextbox, -130, 42, 262 x 59 from
-- the screen's centre; ../story_textbox/structure.rcss) with up to three rows (drawString at y
-- 0x78, 0x84 and 0x90). The award notice (tamer.c: tamerTickAwardSomething): the speech box with a
-- medal's three lines or the player level's one, or the partner's name and its sickness line
-- (partner_impl.c: tickSicknessMechanics). The game publishes award_notice.lines (text keys
-- separated by commas), award_notice.color (the original's TEXT_COLORS number) and
-- award_notice.name (a name the first row starts with, in color 10, or empty) and shows this
-- document; confirming sends award_notice_ok, the only answer the box takes.

local ROWS = 3

local function refresh(document)
  local keys = {}
  for key in string.gmatch(ui.setting("award_notice.lines") or "", "[^,]+") do
    keys[#keys + 1] = key
  end
  local color = "color-" .. tostring(math.floor(tonumber(ui.setting("award_notice.color")) or 1))
  -- The original draws the line right after the name's last glyph; a space keeps them apart in
  -- every language.
  local name = ui.setting("award_notice.name") or ""
  local name_span = document:get("name")
  name_span:set_text(name ~= "" and (name .. " ") or "")
  name_span:set_class("hidden", name == "")
  for row = 1, ROWS do
    local line = document:get("line-" .. row)
    document:get("text-" .. row):set_text(keys[row] and t(keys[row]) or "")
    line:set_class("hidden", keys[row] == nil)
    for number = 0, 10 do
      line:set_class("color-" .. number, "color-" .. number == color)
    end
  end
end

-- The confirm button in the box's corner (script_textbox.c: renderUIBox), as the device in use's
-- glyph.
local function refresh_prompt(document)
  document:get("confirm-icon"):set_prompt("confirm", true)
end

function on_show(document)
  refresh_prompt(document)
  refresh(document)
end

function on_device(document)
  refresh_prompt(document)
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh_prompt(document)
  refresh(document)
end

function on_action(_document, action)
  if action == "confirm" then
    ui.send("award_notice_ok")
    return true
  end
  return action == "cancel" or action == "menu"
end
