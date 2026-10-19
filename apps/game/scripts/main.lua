-- Phase 0 sample: a marker the player moves with any keyboard or gamepad. Edit and save this file while the game
-- runs to see it reload.

marker = marker or { x = 640, y = 360, radius = 24 }

local speed = 320 -- pixels per second

function tick(dt)
    local x, y = input.vector("move_left", "move_right", "move_up", "move_down")
    marker.x = marker.x + x * speed * dt
    marker.y = marker.y + y * speed * dt

    if input.pressed("confirm") then
        marker.radius = marker.radius == 24 and 40 or 24
        print(string.format("confirm on %s", input.device()))
    end
end
