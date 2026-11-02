-- A text squeezed into its room (the engine's long names: docs/modulos/apps/game.md, «Nombres
-- largos»). A player's name of up to twelve letters can make a text wider than the room the
-- original left for a six-letter one; such a text is drawn narrower, from its left edge, instead of
-- running past its box. A text that fits is left as it is, so the original's texts never change.
--
-- A document that uses it links this script, calls fit_text.fit(element, parts, room) each time it
-- writes the text, and fit_text.tick() from its on_tick: the sizes are those of a laid out
-- document, so the squeeze is measured over the next few ticks. `element` is what is squeezed,
-- `parts` the elements whose drawn text is measured (the element itself when nil) and `room` the
-- width in dp the text may take (the element's own width when nil).

fit_text = {}

-- How many ticks a written text is measured again, while the document lays it out.
local measure_ticks = 3
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
  if room == nil then
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
    squeeze(entry)
    entry.ticks = entry.ticks - 1
    if entry.ticks <= 0 then
      table.remove(pending, index)
    end
  end
end
