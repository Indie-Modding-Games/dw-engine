-- The last lines said in the story, from the UI API: each new page (story.text) is read with
-- story.textbox() and kept, the newest at the bottom; the log shows while a textbox is open
-- (story.changed). It reads the story and asks nothing of it, so it needs no command: the game's
-- own box still shows each page and takes the player's buttons.

local kept = 4
local lines = {}

-- The page as one line: its rows after the speaker's name, joined.
local function page_text(box)
  local first = box.speaker.kind ~= "none" and 2 or 1
  local parts = {}
  for index = first, #box.rows do
    local row = ""
    for _, run in ipairs(box.rows[index].runs) do
      row = row .. run.text
    end
    if row ~= "" then
      parts[#parts + 1] = row
    end
  end
  return table.concat(parts, " ")
end

local function draw(document)
  local box = story.textbox()
  document:get("log"):set_class("hidden", not box.open or #lines == 0)
  for index = 1, kept do
    local line = lines[index]
    document:get("line-" .. index):set_class("hidden", line == nil)
    document:get("speaker-" .. index):set_text(line and line.speaker or "")
    document:get("speaker-" .. index):set_class("hidden", line == nil or line.speaker == "")
    -- The button characters and {prompt:...} markers as the device's glyphs, as the box shows them.
    document:get("text-" .. index):set_prompt_text(line and line.text or "", true)
  end
end

local function keep_page(document)
  local box = story.textbox()
  local text = box.open and page_text(box) or ""
  if text ~= "" then
    lines[#lines + 1] = { speaker = box.speaker.name, text = text }
    if #lines > kept then
      table.remove(lines, 1)
    end
  end
  draw(document)
end

function on_show(document)
  draw(document)
end

-- A plugin's document is always shown: each event redraws it.
hooks.on("story.text", function(_, document)
  keep_page(document)
end)
hooks.on("story.changed", function(_, document)
  draw(document)
end)
