-- Whamon in the water at the Secret Beach Cave (ogre10) before the Ogremon event, an engine
-- improvement, on by default (the option whamon_in_water; docs/modules/apps/game.md, «Whamon in
-- the water»). The original leaves Whamon (entity 5) where the map spawns it, far back at
-- (752, 4022) and out of sight, until trigger 224 (the Ogremon event) is set: only then does the
-- map entry (section 0xFE) slide it into the water at (1457, 2916), and the ride's arrival sections
-- (0x3C, 0x3D) bring it there when the player comes on its back. Before 224 the player can still
-- talk to it from the confirm cell by the shore (section 0x51, "Going back to the city?") while it
-- is not on screen. With the option on, a map entry before 224 that no arrival section follows
-- turns Whamon to the shore and slides it into the water off the confirm cell, as the original's
-- entry slides it after 224. The engine answers routine 261 for improvement 2 (Whamon in the
-- water) in variable 254, 1 on and 0 off, and routine 262 with the arrival section that runs after
-- the entry there (0xFF none). Off, after 224 or on a ride in, the section is the original's.
local story <const> = story

-- The story improvement this patch asks the engine for (session: StoryImprovement::WhamonInWater).
local WhamonInWater <const> = 2
local NoArrival <const> = 0xFF
local Whamon <const> = 5
-- Off the confirm cell (885, 1561), clear of the green machine (objects 29 to 56 around
-- (160, 2200)) and of the shore's walkable cells. A slide may stop up to one step past its point
-- (tamer.c: tickEntityMoveTo; after 224 the original's (1457, 2916) in 2 frames leaves Whamon at
-- (1809, 2363)), so this one takes 8 frames: from the spawn (752, 4022) Whamon ends at the point or
-- one step of (93, -152) past it, both in the water.
local WaterX <const> = 1500
local WaterZ <const> = 2800
local SlideFrames <const> = 8
-- Its head to the shore.
local FacingShore <const> = 3584

-- Written as `dwx script-warps` reads it (one if a test, no function of its own), so the walks
-- that derive field/script_warps and field/npc_spawns from the packs follow it.
return {
  script = 136,
  sections = {
    [0xfe] = function(original)
      if not story.trigger(224) then
        story.set_var(254, WhamonInWater) -- var, value
        story.call_routine(261)
        if story.var(254) == 1 then
          story.call_routine(262)
          if story.var(254) == NoArrival then
            -- The turn first: a scripted move takes the character's slot, so a turn ordered while
            -- the slide runs would end it.
            story.entity_set_rotation(Whamon, FacingShore) -- entity, rotation
            story.delay(1)
            story.entity_move_to(Whamon, WaterX, WaterZ, SlideFrames, 0) -- entity, x, z, frames
            story.wait_for_entity(Whamon)
          end
        end
      end
      return original()
    end,
  },
}
