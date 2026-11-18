-- Angemon's event on the Ice Sanctuary's ground floor (icsa01), an engine improvement, on by
-- default (the option angemon_event_zone; docs/modules/apps/game.md, «Angemon's event zone»). The
-- original plays the event (section 0x05) only as the arrival section of the stairs up from the
-- trapdoor passage (script 94, section 0x33: warp_to(97, 1, 5) once trigger 322, the light, is
-- set): a player who comes back to the ground floor by a teleport or through the front door never
-- sees it until they go down the trapdoor and up again. With the option on, the band across the
-- floor's corridor (cells of value 52, section 0x34) also plays it while the light is freed (322)
-- and Angemon has not joined yet (220). The engine answers routine 261 for improvement 1 (Angemon's
-- event zone) in variable 254: 1 on, 0 off. Off, the section is the original's.
local story <const> = story

-- The story improvement this patch asks the engine for (session: StoryImprovement::AngemonZone).
local AngemonZone <const> = 1

return {
  script = 93,
  sections = {
    [0x34] = function(original)
      story.set_var(254, AngemonZone) -- var, value
      story.call_routine(261)
      if story.var(254) == 1 and story.trigger(322) and not story.trigger(220) then
        return story.jump_to_file(93, 0x05)
      end
      return original()
    end,
  },
}
