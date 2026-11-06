-- story/patches/mapper_900.lua: written by dwe-mapper
local story <const> = story
local ENDED <const> = story.ENDED
local PLAYER <const> = story.PLAYER
local PARTNER <const> = story.PARTNER

local routines = {}

function routines.at_0100()
  story.set_var(5, 2) -- var, value
  return ENDED -- end_section
end

return {
  script = 900,
  sections = {
    [0x0051] = function(original)
      if story.trigger(72) then
        return ENDED -- end_section
      end
      story.set_dialog_owner(5)
      story.show_textbox("story_900.text.0200")
      story.set_trigger(73)
      return routines.at_0100()
    end,
  },
}
