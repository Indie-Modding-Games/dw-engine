-- A text squeezed into its room (the engine's long names: docs/modules/apps/game.md, «Nombres
-- largos»). A player's name of up to twelve letters can make a text wider than the room the
-- original left for a six-letter one; such a text is drawn narrower, from its left edge, instead of
-- running past its box. A text that fits is left as it is, so the original's texts never change.
--
-- A document that uses it links this script, calls fit_text.fit(element, parts, room) each time it
-- writes the text, and fit_text.tick() from its on_tick: the sizes are those of a laid out
-- document, so the squeeze is measured over the next few ticks. `element` is what is squeezed,
-- `parts` the elements whose drawn text is measured (the element itself when nil) and `room` the
-- width in dp the text may take (the element's own width when nil, or a function that returns it
-- when the room is that of a box round the text, known only once laid out). fit_text.cut(element,
-- text, room) writes the text and cuts it with "..." instead, for names with no length limit.

fit_text = {}

-- How many ticks a written text is measured again, while the document lays it out. The layout
-- comes with a drawn frame, and a slow machine can run several logic ticks between two of them, so
-- half a second of ticks rather than the three a frame a tick would need.
local measure_ticks = 30
local pending = {}

local function text_width(element, parts)
  local left = element:rect()
  local right = left
  for _, part in ipairs(parts) do
    local x, _, width = part:rect()
    if width > 0 then
      right = math.max(right, x + width)
    end
  end
  return right - left
end

local function squeeze(entry)
  local element = entry.element
  if not element:valid() then
    return
  end
  local room = entry.room
  if type(room) == "function" then
    room = room()
  elseif room == nil then
    local _, _, width = element:rect()
    room = width
  end
  local width = text_width(element, entry.parts)
  if room > 0 and width > room + 0.5 then
    element:set_property("transform-origin", "0% 50%")
    element:set_property("transform", string.format("scale(%.4f, 1)", room / width))
  else
    element:set_property("transform", "none")
  end
end

-- The letters of a text (a letter may take several bytes).
local function letters_of(text)
  local letters = {}
  for letter in string.gmatch(text, "[\1-\127\194-\244][\128-\191]*") do
    letters[#letters + 1] = letter
  end
  return letters
end

-- The texts cut so far, by element id: the same text written again shows cut at once, with no
-- flash of its whole length.
local cuts = {}

local function cut_text(letters, kept)
  return (string.gsub(table.concat(letters, "", 1, kept), "%s+$", "")) .. "..."
end

-- A cut text: fewer letters each tick, in proportion to how much it is over, until it fits with
-- "..." after it.
local function cut(entry)
  local element = entry.element
  if not element:valid() then
    return
  end
  local room = entry.room
  if type(room) == "function" then
    room = room()
  end
  local width = text_width(element, entry.parts)
  if room == nil or room <= 0 then
    return
  end
  if width <= room + 0.5 then
    -- A cut made while the screen still scaled itself can be too short: redo it once.
    local kept = entry.kept
    if kept ~= nil and kept < #entry.letters and not entry.rewritten then
      local letter = width / (kept + 3)
      if width + letter * 3 < room then
        entry.rewritten = true
        entry.ticks = measure_ticks
        entry.kept = nil
        entry.measured = nil
        cuts[element.id] = nil
        element:set_text(entry.text)
      end
    end
    return
  end
  -- The width of the text written last is known only once the document lays it out again: until
  -- the measure changes, it is still the longer text's.
  if entry.measured ~= nil and math.abs(width - entry.measured) < 0.5 then
    return
  end
  entry.measured = width
  local kept = entry.kept or #entry.letters
  local next_kept = math.min(kept - 1, math.floor(kept * room / width))
  entry.kept = math.max(1, next_kept)
  element:set_text(cut_text(entry.letters, entry.kept))
  cuts[element.id] = { text = entry.text, kept = entry.kept }
end

-- Writes `text` and cuts it to `room` (as fit's) with "..." at its end instead of squeezing it: for
-- a name with no length limit (a room's), which would be squeezed past reading.
function fit_text.cut(element, text, room)
  element:set_property("transform", "none")
  local entry = {
    element = element,
    parts = { element },
    room = room,
    ticks = measure_ticks,
    text = text,
    letters = letters_of(text),
  }
  local known = cuts[element.id]
  if known ~= nil and known.text == text then
    entry.kept = known.kept
    element:set_text(cut_text(entry.letters, known.kept))
  else
    cuts[element.id] = nil
    element:set_text(text)
  end
  for index, other in ipairs(pending) do
    if other.element.id == element.id then
      pending[index] = entry
      return
    end
  end
  pending[#pending + 1] = entry
end

function fit_text.fit(element, parts, room)
  local entry =
    { element = element, parts = parts or { element }, room = room, ticks = measure_ticks }
  for index, other in ipairs(pending) do
    if other.element.id == element.id then
      pending[index] = entry
      return
    end
  end
  pending[#pending + 1] = entry
end

function fit_text.tick()
  for index = #pending, 1, -1 do
    local entry = pending[index]
    if entry.letters ~= nil then
      cut(entry)
    else
      squeeze(entry)
    end
    entry.ticks = entry.ticks - 1
    if entry.ticks <= 0 then
      table.remove(pending, index)
    end
  end
end
