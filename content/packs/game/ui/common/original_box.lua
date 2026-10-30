-- The original look's animated windows (ui.c: createAnimatedUIBox, renderUIBoxAnimated,
-- renderUIBoxAnim, removeAnimatedUIBox). A box opens as a grey 1-pixel outline (0x808080) that
-- grows in five frames from a start rectangle (the player, a slot, a cursor) to the box's place,
-- and only then shows the box; it closes the same way back, in five frames, before it goes. Frames
-- are the original's object frames, 20 a second (3 vblanks each, measured with the reference rig:
-- the game menu's outline frames 0..4 at vblanks 22362..22374). Opening sounds box_open
-- (playSound(0, 0)); the first closing frame sounds box_close (playSound(0, 1)). The game ignores
-- the box's input until it is in place (tickUIBox runs only in state 1): busy() says so.
--
-- A document that uses it links this script, has an element `<div id="box-outline"
-- class="box-outline hidden"></div>` straight under its body, and calls original_box.tick from its
-- on_tick and original_box.cancel from its on_hide. Rectangles are { left, top, width, height } in
-- dp from the window's top left corner. Outside the original skin every call finishes at once.

original_box = {}

local frame_seconds = 1 / 20
local frames = 5
local animations = {}

function original_box.enabled()
  return ui.setting("ui_skin") == "original"
end

function original_box.busy()
  return #animations > 0
end

-- An element's rectangle.
function original_box.rect(document, id)
  local left, top, width, height = document:get(id):rect()
  return { left, top, width, height }
end

-- The 10 x 10 pixels round the player (getEntityScreenPos of the player, as createMenuBox and
-- createInventoryView start from), from the game's field.player_x and field.player_y.
function original_box.player_rect(document)
  local _, _, width, height = document:rect()
  local x = (tonumber(ui.setting("field.player_x")) or 50) * width / 100
  local y = (tonumber(ui.setting("field.player_y")) or 50) * height / 100
  return { x - 15, y - 15, 30, 30 }
end

-- renderUIBoxAnim: each edge frame / 4 of the way from the start rectangle to the box.
local function place(outline, from, to, frame)
  local left = from[1] + frame * (to[1] - from[1]) / 4
  local top = from[2] + frame * (to[2] - from[2]) / 4
  local right = from[1] + from[3] + frame * ((to[1] + to[3]) - (from[1] + from[3])) / 4
  local bottom = from[2] + from[4] + frame * ((to[2] + to[4]) - (from[2] + from[4])) / 4
  outline:set_property("left", string.format("%.1fdp", left))
  outline:set_property("top", string.format("%.1fdp", top))
  outline:set_property("width", string.format("%.1fdp", math.max(3, right - left)))
  outline:set_property("height", string.format("%.1fdp", math.max(3, bottom - top)))
end

local function finish(document, animation)
  animation.box:set_class("box-animating", false)
  if animation.closing then
    animation.box:set_class("box-closed", true)
  end
  if #animations == 0 then
    document:get("box-outline"):set_class("hidden", true)
  end
  if animation.done then
    animation.done()
  end
end

local function start(document, box_id, from, to, closing, done)
  local box = document:get(box_id)
  if not original_box.enabled() then
    box:set_class("box-closed", closing)
    if done then
      done()
    end
    return
  end
  box:set_class("box-closed", false)
  box:set_class("box-animating", true)
  local animation = {
    box = box,
    box_id = box_id,
    from = from,
    to = to,
    closing = closing,
    elapsed = 0,
    frame = -1,
    done = done,
  }
  table.insert(animations, animation)
  ui.cue(closing and "box_close" or "box_open")
  original_box.tick(document, 0)
end

-- Opens the box `box_id` from the rectangle `from`; `done` runs once it is in place.
function original_box.open(document, box_id, from, done)
  start(document, box_id, from, nil, false, done)
end

-- Closes the box `box_id` into the rectangle `to`; `done` runs once it is gone. The box keeps the
-- class box-closed (hidden) until it opens again.
function original_box.close(document, box_id, to, done)
  if not original_box.enabled() then
    if done then
      done()
    end
    return
  end
  start(document, box_id, to, original_box.rect(document, box_id), true, done)
end

-- Once per tick: the outline of the oldest animation moves on a frame every 1/20 s.
function original_box.tick(document, seconds)
  local animation = animations[1]
  if animation == nil then
    return
  end
  animation.elapsed = animation.elapsed + seconds
  local step = math.floor(animation.elapsed / frame_seconds + 0.000001)
  if step >= frames then
    table.remove(animations, 1)
    finish(document, animation)
    if animations[1] ~= nil then
      original_box.tick(document, 0)
    end
    return
  end
  if step == animation.frame then
    return
  end
  animation.frame = step
  -- The box's place, read once it is laid out (the frame after it was shown).
  if animation.to == nil and step >= 1 then
    animation.to = original_box.rect(document, animation.box_id)
  end
  local outline = document:get("box-outline")
  outline:set_class("hidden", false)
  local frame = animation.closing and (frames - 1 - step) or step
  place(outline, animation.from, animation.to or animation.from, frame)
end

-- The document is going away: the animations end where they are, without their callbacks.
function original_box.cancel(document)
  for _, animation in ipairs(animations) do
    animation.box:set_class("box-animating", false)
  end
  animations = {}
  document:get("box-outline"):set_class("hidden", true)
end
