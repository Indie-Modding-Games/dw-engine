-- The box is the story's speech box (tamer.c: renderAwardSomethingTextbox, -130, 42, 262 x 59 from
-- the screen's centre; ../story_textbox/structure.rcss) with up to three rows (drawString at y
-- 0x78, 0x84 and 0x90). The award notice (tamer.c: tamerTickAwardSomething): the speech box with a
-- medal's three lines or the tamer level's one. The game publishes award_notice.lines (text keys
-- separated by commas) and award_notice.color (the original's TEXT_COLORS number) and shows this
-- document; confirming sends award_notice_ok, the only answer the box takes.

local ROWS = 3

local function refresh(document)
  local keys = {}
  for key in string.gmatch(ui.setting("award_notice.lines") or "", "[^,]+") do
    keys[#keys + 1] = key
  end
  local color = "color-" .. tostring(math.floor(tonumber(ui.setting("award_notice.color")) or 1))
  for row = 1, ROWS do
    local line = document:get("line-" .. row)
    line:set_text(keys[row] and t(keys[row]) or "")
    line:set_class("hidden", keys[row] == nil)
    for number = 0, 10 do
      line:set_class("color-" .. number, "color-" .. number == color)
    end
  end
end

function on_show(document)
  refresh(document)
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
end

function on_action(_document, action)
  if action == "confirm" then
    ui.send("award_notice_ok")
    return true
  end
  return action == "cancel" or action == "menu"
end
