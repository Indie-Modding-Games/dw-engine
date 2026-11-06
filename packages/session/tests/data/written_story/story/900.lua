-- dwx lua 3: script 900, from script/dg/900
local story <const> = story
local ENDED <const> = story.ENDED
local PLAYER <const> = story.PLAYER
local PARTNER <const> = story.PARTNER

local routines = {}

-- section 0x0051
function routines.at_0040()
  if story.trigger(71) then
    return ENDED -- end_section
  end
  story.set_dialog_owner(5)
  story.show_textbox("story_900.text.0042")
  return routines.at_0100()
end

-- section 0x0052
function routines.at_0080()
  story.set_trigger(74)
  return routines.at_0100()
end

function routines.at_0100()
  story.set_var(5, 1) -- var, value
  return ENDED -- end_section
end

return {
  script = 900,
  sections = {
    [0x0051] = routines.at_0040,
    [0x0052] = routines.at_0080,
  },
}
