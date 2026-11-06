-- A box placed as on the original's screen, kept whole inside the window. The boxes are laid out
-- in dp at the original's place (the speech box from 486dp to 663dp of the 720dp the UI is
-- designed at), and the player's UI scale makes a dp larger: past what the window holds (115 % on
-- a 16:10 window, 130 % on a 16:9 one), the speech box ran past the window's bottom edge and cut
-- its last rows. Such a box is drawn smaller, as much as it needs, and moved in from the edges it
-- crosses; a box that fits is drawn as it is laid out, so at the default scale nothing changes.
--
-- A document that uses it links this script and calls screen_fit.apply(document, box, scale)
-- each time it writes the box's transform and from its on_tick, as the window and the scale can
-- change while the box is up. `scale` is the box's own scale (an opening box's growth), 1 when
-- nil.

screen_fit = {}

-- The room left between the box and the window's edges, in dp.
local margin_dp = 6

-- The scale and the shift in dp that keep a box of the given rectangle inside a window.
function screen_fit.fit(window_width, window_height, left, top, width, height)
  if window_width <= 0 or window_height <= 0 or width <= 0 or height <= 0 then
    return 1, 0, 0
  end
  local room_width = window_width - 2 * margin_dp
  local room_height = window_height - 2 * margin_dp
  local scale = math.min(1, room_width / width, room_height / height)
  -- The transform scales round the box's centre; the centre moves just enough for the scaled box to
  -- clear the margins.
  local function shift(start, size, room)
    local half = size * scale / 2
    local centre = start + size / 2
    local wanted = math.max(margin_dp + half, math.min(centre, margin_dp + room - half))
    return wanted - centre
  end
  return scale, shift(left, width, room_width), shift(top, height, room_height)
end

function screen_fit.apply(document, box, scale)
  local _, _, window_width, window_height = document:rect()
  local fit, dx, dy = screen_fit.fit(window_width, window_height, box:rect())
  local transform
  if fit >= 1 and math.abs(dx) < 0.05 and math.abs(dy) < 0.05 then
    transform = scale ~= nil and string.format("scale(%.3f)", scale) or "none"
  else
    transform = string.format("translate(%.1fdp, %.1fdp) scale(%.4f)", dx, dy, fit * (scale or 1))
  end
  -- Written only when it changes: the call comes every tick.
  if box:attribute("data-transform") ~= transform then
    box:set_attribute("data-transform", transform)
    box:set_property("transform", transform)
  end
end
