-- The improved shop job (game/shop_job; docs/reference/trabajo-tienda.md). Section 0x51 (the
-- counter) asks the engine for the mode (routine 256, answer in variable 110): the original mode
-- plays the script's own section, and nothing else of the job is patched. The improved mode plays
-- the original's talk and then the job's own section (ImprovedSection, 0x300, here and in scripts
-- 199 to 202): the next customer from the job's line (routine 257) in place of the random draw, and
-- the job's end closing the line and giving the bonus item (routine 260) over the original's pay.
-- Routine 56 has the tally's high byte in 243 and routine 260 answers in 110: 1 the bonus went to
-- the bag, 2 to the item keeper (the bag had no room), 3 nowhere (neither had room). Keeping the
-- two modes apart from the counter on lets dwx script-warps follow each on its own.
local story <const> = story
local ENDED <const> = story.ENDED
local PLAYER <const> = story.PLAYER
local PARTNER <const> = story.PARTNER

local routines = {}

-- The original's counter talk (section 0x51); "I'll try" starts the improved job.
function routines.start()
  if story.trigger(247) then
    return ENDED -- end_section
  else
    story.entity_look_at_entity(PLAYER, 5) -- entity, target_entity
    if story.trigger(136) then
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.065e")
    else
      story.set_trigger(136)
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.004a")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_049.text.00c2")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.0120")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_049.text.015e")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.017a")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_049.text.01e0")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.01f4")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_049.text.0236")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.0288")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_049.text.02ba")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.02d8")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_049.text.0310")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.032c")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_049.text.03c8")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.03fa")
    end
    story.set_dialog_owner(PLAYER)
    do
      local choice <const> = story.set_selection("story_049.text.0474", 2)
      if choice == 0 then
        story.fadeout_hud(0)
        story.entity_walk_to(5, 133, 292, 0, 0) -- entity, x, z, animation, unknown07
        story.entity_walk_to(PLAYER, -980, -432, 0, 0) -- entity, x, z, animation, unknown07
        story.wait_for_entity(PLAYER)
        story.wait_for_entity(5)
        story.set_var(247, 9) -- var, value
        story.set_var(248, 12) -- var, value
        story.call_routine(21)
        story.set_obj_visibility(33, 1) -- object, visible
        story.set_obj_visibility(34, 1) -- object, visible
        story.set_obj_visibility(35, 1) -- object, visible
        story.set_obj_visibility(9, 0) -- object, visible
        story.set_obj_visibility(10, 0) -- object, visible
        story.set_obj_visibility(11, 0) -- object, visible
        story.set_obj_visibility(12, 0) -- object, visible
        story.set_obj_visibility(13, 0) -- object, visible
        story.set_obj_visibility(14, 0) -- object, visible
        story.set_obj_visibility(15, 0) -- object, visible
        story.set_obj_visibility(16, 0) -- object, visible
        story.set_obj_visibility(17, 0) -- object, visible
        story.set_obj_visibility(18, 0) -- object, visible
        story.set_obj_visibility(19, 0) -- object, visible
        story.set_obj_visibility(20, 0) -- object, visible
        story.entity_look_at_entity(5, PLAYER) -- entity, target_entity
        story.entity_walk_to(PLAYER, -980, 292, 0, 0) -- entity, x, z, animation, unknown07
        story.entity_walk_to(PARTNER, -980, -432, 0, 0) -- entity, x, z, animation, unknown07
        story.wait_for_entity(PLAYER)
        story.wait_for_entity(PARTNER)
        story.entity_look_at_entity(5, PLAYER) -- entity, target_entity
        story.entity_walk_to(PLAYER, -341, 292, 0, 0) -- entity, x, z, animation, unknown07
        story.entity_walk_to(PARTNER, -980, 292, 0, 0) -- entity, x, z, animation, unknown07
        story.wait_for_entity(PLAYER)
        story.wait_for_entity(PARTNER)
        story.set_var(247, 21) -- var, value
        story.set_var(248, 12) -- var, value
        story.call_routine(21)
        story.set_obj_visibility(9, 1) -- object, visible
        story.set_obj_visibility(10, 1) -- object, visible
        story.set_obj_visibility(11, 1) -- object, visible
        story.set_obj_visibility(12, 1) -- object, visible
        story.set_obj_visibility(13, 1) -- object, visible
        story.set_obj_visibility(14, 1) -- object, visible
        story.set_obj_visibility(15, 1) -- object, visible
        story.set_obj_visibility(16, 1) -- object, visible
        story.set_obj_visibility(17, 1) -- object, visible
        story.set_obj_visibility(18, 1) -- object, visible
        story.set_obj_visibility(19, 1) -- object, visible
        story.set_obj_visibility(20, 1) -- object, visible
        story.set_obj_visibility(21, 0) -- object, visible
        story.set_obj_visibility(22, 0) -- object, visible
        story.set_obj_visibility(23, 0) -- object, visible
        story.set_obj_visibility(24, 0) -- object, visible
        story.set_obj_visibility(25, 0) -- object, visible
        story.set_obj_visibility(26, 0) -- object, visible
        story.set_obj_visibility(27, 0) -- object, visible
        story.set_obj_visibility(28, 0) -- object, visible
        story.set_obj_visibility(29, 0) -- object, visible
        story.set_obj_visibility(30, 0) -- object, visible
        story.set_obj_visibility(31, 0) -- object, visible
        story.set_obj_visibility(32, 0) -- object, visible
        story.delay(60)
        story.entity_set_rotation(PLAYER, 0) -- entity, rotation
        story.entity_look_at_entity(PARTNER, PLAYER) -- entity, target_entity
        story.wait_for_entity(PLAYER)
        story.set_obj_visibility(21, 1) -- object, visible
        story.set_obj_visibility(22, 1) -- object, visible
        story.set_obj_visibility(23, 1) -- object, visible
        story.set_obj_visibility(24, 1) -- object, visible
        story.set_obj_visibility(25, 1) -- object, visible
        story.set_obj_visibility(26, 1) -- object, visible
        story.set_obj_visibility(27, 1) -- object, visible
        story.set_obj_visibility(28, 1) -- object, visible
        story.set_obj_visibility(29, 1) -- object, visible
        story.set_obj_visibility(30, 1) -- object, visible
        story.set_obj_visibility(31, 1) -- object, visible
        story.set_obj_visibility(32, 1) -- object, visible
        story.set_obj_visibility(33, 0) -- object, visible
        story.set_obj_visibility(34, 0) -- object, visible
        story.set_obj_visibility(35, 0) -- object, visible
        story.call_routine(30)
        story.set_var(123, 0) -- var, value
        story.unset_trigger(101)
        return routines.loop()
      elseif choice == 1 then
        return routines.declined()
      else
        return routines.declined()
      end
    end
  end
end

-- "No thanks": the original's answer.
function routines.declined()
  story.set_dialog_owner(5)
  story.show_textbox("story_049.text.05fa")
  return ENDED -- end_section
end

function routines.loop()
  if story.var(123) >= 20 then
    return routines.finish()
  else
    if story.var(123) >= 10 and not story.trigger(101) then
      story.set_trigger(101)
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.06b2")
      story.entity_look_at_entity(5, PLAYER) -- entity, target_entity
      story.entity_look_at_entity(PLAYER, 5) -- entity, target_entity
      story.delay(10)
      return routines.halfway()
    else
      return routines.next()
    end
  end
end

-- The next customer of the line; past it, the job ends.
function routines.next()
  story.call_routine(257)
  if story.var(110) == 199 then
    return story.jump_to_file(199, 0x300)
  end
  if story.var(110) == 200 then
    return story.jump_to_file(200, 0x300)
  end
  if story.var(110) == 201 then
    return story.jump_to_file(201, 0x300)
  end
  if story.var(110) == 202 then
    return story.jump_to_file(202, 0x300)
  end
  return routines.finish()
end

-- Halfway: the owner tells how the job goes.
function routines.halfway()
  story.call_routine(56)
  if story.var(243) <= 2 then
    story.set_dialog_owner(5)
    story.show_textbox("story_049.text.0750")
    story.entity_set_rotation(PLAYER, 80) -- entity, rotation
    story.fadeout_hud(0)
  else
    if story.var(243) <= 5 then
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.07e2")
      story.entity_set_rotation(PLAYER, 80) -- entity, rotation
      story.fadeout_hud(0)
    else
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.084a")
      story.entity_set_rotation(PLAYER, 80) -- entity, rotation
      story.fadeout_hud(0)
    end
  end
  return routines.next()
end

-- The job's end: the original's pay by the tally, then the bonus item.
function routines.finish()
  story.delay(40)
  story.set_dialog_owner(5)
  story.show_textbox("story_049.text.08d8")
  story.call_routine(56)
  story.call_routine(260)
  if story.var(243) <= 6 then
    story.set_dialog_owner(5)
    story.show_textbox("story_049.text.0944")
    story.add_money(500)
    story.set_dialog_owner(255)
    story.show_textbox("story_049.text.09d4")
    if story.var(110) == 1 then
      story.set_dialog_owner(255)
      story.show_textbox("shop_job.story.bonus")
    end
    if story.var(110) == 2 then
      story.set_dialog_owner(255)
      story.show_textbox("shop_job.story.bonus_kept")
    end
    if story.var(110) == 3 then
      story.set_dialog_owner(255)
      story.show_textbox("shop_job.story.bonus_bag_full")
    end
    story.store_date(105)
    story.add_minutes_to_date_at(105, 480) -- var, minutes
    story.advance_to_date_at(105)
    return story.warp_to(44, 2, 255)
  else
    if story.var(243) <= 11 then
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.0a22")
      story.add_money(1500)
      story.set_dialog_owner(255)
      story.show_textbox("story_049.text.0a86")
      if story.var(110) == 1 then
        story.set_dialog_owner(255)
        story.show_textbox("shop_job.story.bonus")
      end
      if story.var(110) == 2 then
        story.set_dialog_owner(255)
        story.show_textbox("shop_job.story.bonus_kept")
      end
      if story.var(110) == 3 then
        story.set_dialog_owner(255)
        story.show_textbox("shop_job.story.bonus_bag_full")
      end
      story.store_date(105)
      story.add_minutes_to_date_at(105, 480) -- var, minutes
      story.advance_to_date_at(105)
      return story.warp_to(44, 2, 255)
    else
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.0ace")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_049.text.0b60")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.0b9e")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.0c18")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.0c68")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_049.text.0cd4")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.0d06")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_049.text.0d56")
      story.set_dialog_owner(5)
      story.show_textbox("story_049.text.0dd2")
      story.play_sound(8, 10) -- bank, sound
      story.set_dialog_owner(255)
      story.show_textbox("story_049.text.0e42")
      story.show_textbox("story_049.text.0e7e")
      story.add_money(2000)
      story.add_to_var(1, 2) -- var, value
      story.set_trigger(247)
      if story.var(110) == 1 then
        story.set_dialog_owner(255)
        story.show_textbox("shop_job.story.bonus")
      end
      if story.var(110) == 2 then
        story.set_dialog_owner(255)
        story.show_textbox("shop_job.story.bonus_kept")
      end
      if story.var(110) == 3 then
        story.set_dialog_owner(255)
        story.show_textbox("shop_job.story.bonus_bag_full")
      end
      story.store_date(105)
      story.add_minutes_to_date_at(105, 480) -- var, minutes
      story.advance_to_date_at(105)
      return story.warp_to(44, 2, 255)
    end
  end
end

return {
  script = 49,
  sections = {
    [0x51] = function(original)
      story.call_routine(256)
      if story.var(110) == 0 then
        return original()
      end
      return routines.start()
    end,
    -- The improved job's own section: the customers come back here.
    [0x300] = function()
      return routines.loop()
    end,
  },
}
