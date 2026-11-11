-- The «widescreen pictures» option (the widescreen_pictures setting; docs/modulos/apps/game.md,
-- «Imágenes a pantalla ancha»): in the original look, the title's pictures fill the window instead
-- of a 4:3 frame on black. A picture is scaled as one until it covers the window's width, and the
-- rows it then has too many are cut: TITLE2's from the top (only grid there, its logo is at the
-- bottom), TITLE256's from the bottom (black under its logo). It is never scaled past showing
-- MinRows rows (a 16:9 window's cut): a wider window sees each side of the picture carry on as a
-- mirror image of its edge (the skin's wide sheets, skin.yaml mirror_sides), the mirror fading
-- darker towards the window's edge. The boxes over it keep the original's places in the picture
-- across, their own heights, and move left (and up, the ones that may) off the logo when they
-- would cover it.
--
-- A document whose skin draws such a picture has, first in #scrim, the elements #picture,
-- #picture-fade-left and #picture-fade-right; it links this script and calls
-- title_picture.apply(document, groups) from its on_show, its on_setting and its on_tick (the
-- window can change any time). `groups` lists its boxes, each group moved as one. An entry names
-- its element (`id`) and its left in the picture's pixels (`x`, the original's place); `top`, its
-- top in dp, lets the group move up too (without it, the top is the document's); `row` and
-- `width`, in the picture's pixels, pin a line of text to the picture (the "Press Start" line),
-- which no logo moves. It puts the class `wide` on #scrim
-- while the option is on (the default, also before the game publishes it) and the skin is the
-- original look's (#panel.skin-original); off, the boxes take their style sheet's places again.

title_picture = {}

title_picture.setting = "widescreen_pictures"

-- The fewest of the picture's rows a window shows: 180 of 240, a 16:9 window's.
title_picture.min_rows = 180

-- The two pictures in the game's pixels: the size the cut is worked out from, the rows drawn,
-- the columns each wide sheet mirrors on either side (skin.yaml: ow-title-wide, ow-boot-wide), the
-- edge whose rows stay, and the rectangles {left, top, right, bottom} of the logo the boxes keep
-- off.
title_picture.menu = {
  width = 320,
  height = 240,
  rows = 240,
  mirror_left = 100,
  mirror_right = 26,
  keep = "bottom",
  logo = { { 211, 165, 263, 178 }, { 174, 178, 293, 228 } },
}
title_picture.boot = {
  width = 320,
  height = 240,
  rows = 206,
  mirror_left = 52,
  mirror_right = 56,
  keep = "top",
  logo = { { 52, 37, 264, 150 } },
}

-- The room kept between a box and the logo, and between a box and the window's edges, in dp.
local logo_gap = 16
local edge_gap = 24

-- Where a picture goes in a window of this size in dp: its scale in dp per pixel and the left and
-- top of its own (unmirrored) 320 columns.
function title_picture.layout(picture, window_width, window_height)
  local scale = math.max(
    window_height / picture.height,
    math.min(window_width / picture.width, window_height / title_picture.min_rows)
  )
  local spare = window_width - picture.width * scale
  local left
  if spare >= 0 then
    -- Wider than the picture: its sides carry on, the right one (short of the logo) at most as
    -- far as its mirror goes, the left one the rest; past both mirrors, black.
    local right = math.min(spare / 2, picture.mirror_right * scale)
    local mirrored = math.min(spare - right, picture.mirror_left * scale)
    left = mirrored + (spare - mirrored - right) / 2
  else
    -- Narrower than 4:3: the sides are cut, in the middle as far as the logo stays whole.
    local first, last = picture.width, 0
    for _, area in ipairs(picture.logo) do
      first = math.min(first, area[1])
      last = math.max(last, area[3])
    end
    left = math.max(math.min(spare / 2, window_width - last * scale), -first * scale)
    left = math.max(spare, math.min(0, left))
  end
  local top = 0
  if picture.keep == "bottom" then
    top = window_height - picture.height * scale
  end
  return scale, left, top
end

-- Writes an element's properties, only when they change: the calls come every tick.
local function write(element, properties)
  local key = ""
  for _, property in ipairs(properties) do
    key = key .. property[1] .. "=" .. property[2] .. ";"
  end
  if element:attribute("data-title-picture") ~= key then
    element:set_attribute("data-title-picture", key)
    for _, property in ipairs(properties) do
      if property[2] == "" then
        element:remove_property(property[1])
      else
        element:set_property(property[1], property[2])
      end
    end
  end
end

local function dp(value)
  return string.format("%.1fdp", value)
end

local function overlaps(a, b)
  return a.left < b.right and b.left < a.right and a.top < b.bottom and b.top < a.bottom
end

-- Places a group of boxes at their places in the picture, moved left, then up, off the logo's
-- rectangles, never past the window's edges.
local function place_group(document, group, logo, scale, left, top, window_width)
  local box = { left = math.huge, top = math.huge, right = -math.huge, bottom = -math.huge }
  local movable = true
  local entries = {}
  for _, entry in ipairs(group) do
    local element = document:get(entry.id)
    if element ~= nil then
      local _, own_top, width, height = element:rect()
      local at = { entry = entry, element = element, left = left + entry.x * scale, top = own_top }
      if entry.row ~= nil then
        at.top = top + entry.row * scale
        width = entry.width * scale
      elseif entry.top ~= nil then
        at.top = entry.top
      end
      table.insert(entries, at)
      movable = movable and entry.top ~= nil
      if width > 0 and height > 0 and entry.row == nil then
        box.left = math.min(box.left, at.left)
        box.top = math.min(box.top, at.top)
        box.right = math.max(box.right, at.left + width)
        box.bottom = math.max(box.bottom, at.top + height)
      end
    end
  end
  local dx, dy = 0, 0
  if box.right > box.left then
    dx = math.min(0, window_width - edge_gap - box.right)
    local function moved()
      return {
        left = box.left + dx,
        top = box.top + dy,
        right = box.right + dx,
        bottom = box.bottom + dy,
      }
    end
    for _, area in ipairs(logo) do
      if overlaps(moved(), area) then
        dx = math.min(dx, area.left - box.right)
      end
    end
    dx = math.max(dx, math.min(0, edge_gap - box.left))
    if movable then
      for _, area in ipairs(logo) do
        if overlaps(moved(), area) then
          dy = math.min(dy, area.top - box.bottom)
        end
      end
      dy = math.max(dy, math.min(0, edge_gap - box.top))
    end
  end
  for _, at in ipairs(entries) do
    local properties = { { "left", dp(at.left + dx) }, { "margin-left", "0dp" } }
    if at.entry.row ~= nil then
      table.insert(properties, { "top", dp(at.top) })
      table.insert(properties, { "width", dp(at.entry.width * scale) })
    elseif at.entry.top ~= nil then
      table.insert(properties, { "top", dp(at.top + dy) })
    end
    write(at.element, properties)
  end
end

-- Takes the boxes back to their style sheet's places.
local function clear_groups(document, groups)
  for _, group in ipairs(groups) do
    for _, entry in ipairs(group) do
      local element = document:get(entry.id)
      if element ~= nil then
        local properties = { { "left", "" }, { "margin-left", "" } }
        if entry.row ~= nil then
          table.insert(properties, { "top", "" })
          table.insert(properties, { "width", "" })
        elseif entry.top ~= nil then
          table.insert(properties, { "top", "" })
        end
        write(element, properties)
      end
    end
  end
end

function title_picture.apply(document, groups)
  local scrim = document:get("scrim")
  local panel = document:get("panel")
  if scrim == nil or panel == nil then
    return
  end
  local wide = ui.setting(title_picture.setting) ~= false and panel:has_class("skin-original")
  scrim:set_class("wide", wide)
  if not wide then
    -- A skin switch can leave the previous wide-picture dimensions inline on this shared
    -- document. Clear them so the new skin's own #picture layout takes effect.
    local shown = document:get("picture")
    if shown ~= nil then
      write(shown, { { "left", "" }, { "top", "" }, { "width", "" }, { "height", "" } })
    end
    local fade_left = document:get("picture-fade-left")
    if fade_left ~= nil then
      write(fade_left, { { "width", "" } })
    end
    local fade_right = document:get("picture-fade-right")
    if fade_right ~= nil then
      write(fade_right, { { "left", "" }, { "width", "" } })
    end
    clear_groups(document, groups)
    return
  end
  local picture = title_picture.menu
  if scrim:has_class("boot") then
    picture = title_picture.boot
  end
  local _, _, window_width, window_height = document:rect()
  local scale, left, top = title_picture.layout(picture, window_width, window_height)
  local shown = document:get("picture")
  if shown ~= nil then
    write(shown, {
      { "left", dp(left - picture.mirror_left * scale) },
      { "top", dp(top) },
      { "width", dp((picture.width + picture.mirror_left + picture.mirror_right) * scale) },
      { "height", dp(picture.rows * scale) },
    })
  end
  local right = left + picture.width * scale
  local fade_left = document:get("picture-fade-left")
  if fade_left ~= nil then
    write(fade_left, { { "width", dp(math.max(0, left)) } })
  end
  local fade_right = document:get("picture-fade-right")
  if fade_right ~= nil then
    write(fade_right, { { "left", dp(right) }, { "width", dp(math.max(0, window_width - right)) } })
  end
  local logo = {}
  for _, area in ipairs(picture.logo) do
    table.insert(logo, {
      left = left + area[1] * scale - logo_gap,
      top = top + area[2] * scale - logo_gap,
      right = left + area[3] * scale + logo_gap,
      bottom = top + area[4] * scale + logo_gap,
    })
  end
  for _, group in ipairs(groups) do
    place_group(document, group, logo, scale, left, top, window_width)
  end
end
