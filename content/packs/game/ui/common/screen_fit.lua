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
--
-- A page whose boxes sit beside its main box on the same stage (the partner's menu in the
-- original look: the bag's option boxes beside its panel) calls
-- screen_fit.apply_around(document, element, left, top, width, height) instead: that rectangle in
-- dp (the boxes that must show) decides the fit, and the transform goes on the element that holds
-- them all, round the rectangle's centre, so they stay together.

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

-- Writes a transform (and its origin) on an element, only when it changes: the calls come every
-- tick.
local function write(element, transform, origin)
  local key = transform .. "|" .. (origin or "")
  if element:attribute("data-transform") ~= key then
    element:set_attribute("data-transform", key)
    element:set_property("transform", transform)
    if origin ~= nil then
      element:set_property("transform-origin", origin)
    end
  end
end

local function fits(fit, dx, dy)
  return fit >= 1 and math.abs(dx) < 0.05 and math.abs(dy) < 0.05
end

function screen_fit.apply(document, box, scale)
  local _, _, window_width, window_height = document:rect()
  local fit, dx, dy = screen_fit.fit(window_width, window_height, box:rect())
  if fits(fit, dx, dy) then
    write(box, scale ~= nil and string.format("scale(%.3f)", scale) or "none")
  else
    write(box, string.format("translate(%.1fdp, %.1fdp) scale(%.4f)", dx, dy, fit * (scale or 1)))
  end
end

function screen_fit.apply_around(document, element, left, top, width, height)
  local _, _, window_width, window_height = document:rect()
  local fit, dx, dy = screen_fit.fit(window_width, window_height, left, top, width, height)
  if fits(fit, dx, dy) then
    write(element, "none")
    return
  end
  local element_left, element_top = element:rect()
  write(
    element,
    string.format("translate(%.1fdp, %.1fdp) scale(%.4f)", dx, dy, fit),
    string.format(
      "%.1fdp %.1fdp 0",
      left + width / 2 - element_left,
      top + height / 2 - element_top
    )
  )
end

-- Takes a fit off an element (a page that switched to fitting another one).
function screen_fit.clear(element)
  write(element, "none")
end
