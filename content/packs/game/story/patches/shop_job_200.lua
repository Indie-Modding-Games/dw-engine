-- The improved shop job's customer (game/shop_job; docs/reference/shop-job.md): the
-- original's walk in, lines and walk out, with the engine's haggle screen (routine 259) in place of
-- the price menus, in the job's own section (0x300, which script 49 starts). Engine routines: 258
-- arrival (its item), 259 counter turn; each answers in variable 110. The original mode plays the
-- script's own section 0x33.
local story <const> = story
local PLAYER <const> = story.PLAYER

local routines = {}

-- The customer comes in and asks for the item the line gives it.
function routines.customer()
  story.add_to_var(123, 1) -- var, value
  story.load_species_model(68)
  story.spawn_npc(68, 2, 0) -- species, slot, autotalk
  story.delay(20)
  story.entity_walk_to(7, -501, -508, 0, 0) -- entity, x, z, animation, unknown07
  story.wait_for_entity(7)
  story.entity_look_at_entity(7, PLAYER) -- entity, target_entity
  story.call_routine(258)
  if story.var(110) == 0 then
    return routines.medicine()
  end
  if story.var(110) == 1 then
    return routines.potty()
  end
  return routines.meat()
end

-- The counter for medicine: one turn after another until the customer buys or leaves, then the
-- original's lines for how it ended. The arms meet again, so the story's walk (dwx script-warps)
-- follows one path through the customer.
function routines.medicine()
  story.set_dialog_owner(7)
  story.show_textbox("story_200.text.212e")
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_200.text.217c")
  while true do
    story.call_routine(259)
    if story.var(110) == 1 then
      story.set_var(247, 7) -- var, value
      story.call_routine(28)
      story.set_dialog_owner(7)
      story.show_textbox("story_200.text.310c")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_200.text.313c")
    elseif story.var(110) == 2 then
      story.set_dialog_owner(PLAYER)
      story.show_textbox("shop_job.story.talk")
      story.set_dialog_owner(7)
      story.show_textbox("shop_job.story.listens")
    else
      break
    end
  end
  if story.var(110) == 0 then
    -- Sold: the engine already added the profit to the tally; the line for the price level (117).
    story.set_var(247, 7) -- var, value
    story.call_routine(28)
    story.set_dialog_owner(7)
    story.show_textbox("story_200.text.2d08")
    story.play_sound(8, 6) -- bank, sound
    story.delay(10)
    story.set_textbox_size(0, 12, 3) -- entity, columns, rows
    if story.var(117) == 1 then
      story.show_textbox("story_200.text.2d60")
    end
    if story.var(117) == 2 then
      story.show_textbox("story_200.text.2dc6")
    end
    if story.var(117) == 3 then
      story.show_textbox("story_200.text.2e2c")
    end
    if story.var(117) == 4 then
      story.show_textbox("story_200.text.2e8e")
    end
    if story.var(117) == 5 then
      story.show_textbox("story_200.text.2ef2")
    end
    if story.var(117) == 6 then
      story.show_textbox("story_200.text.2f56")
    end
    if story.var(117) == 7 then
      story.show_textbox("story_200.text.2fbc")
    end
    if story.var(117) == 8 then
      story.show_textbox("story_200.text.301e")
    end
    if story.var(117) == 9 then
      story.show_textbox("story_200.text.3076")
    end
    story.set_var(119, 0) -- var, value
    story.play_sound(0, 23) -- bank, sound
    story.delay(20)
    routines.leave()
  elseif story.var(110) == 3 then
    routines.walk_out()
  else
    -- Sent away: the player says so and the customer leaves.
    story.set_dialog_owner(PLAYER)
    story.show_textbox("shop_job.story.dismiss")
    story.set_var(119, 0) -- var, value
    routines.leave()
  end
  return story.jump_to_file(49, 0x300)
end

-- The counter for potty: one turn after another until the customer buys or leaves, then the
-- original's lines for how it ended. The arms meet again, so the story's walk (dwx script-warps)
-- follows one path through the customer.
function routines.potty()
  story.set_dialog_owner(7)
  story.show_textbox("story_200.text.0fd4")
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_200.text.102e")
  while true do
    story.call_routine(259)
    if story.var(110) == 1 then
      story.set_var(247, 7) -- var, value
      story.call_routine(28)
      story.set_dialog_owner(7)
      story.show_textbox("story_200.text.20bc")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_200.text.20fa")
    elseif story.var(110) == 2 then
      story.set_dialog_owner(PLAYER)
      story.show_textbox("shop_job.story.talk")
      story.set_dialog_owner(7)
      story.show_textbox("shop_job.story.listens")
    else
      break
    end
  end
  if story.var(110) == 0 then
    -- Sold: the engine already added the profit to the tally; the line for the price level (117).
    story.set_var(247, 7) -- var, value
    story.call_routine(28)
    story.set_dialog_owner(7)
    story.show_textbox("story_200.text.1cb8")
    story.play_sound(8, 6) -- bank, sound
    story.delay(10)
    story.set_textbox_size(0, 12, 3) -- entity, columns, rows
    if story.var(117) == 1 then
      story.show_textbox("story_200.text.1d04")
    end
    if story.var(117) == 2 then
      story.show_textbox("story_200.text.1d72")
    end
    if story.var(117) == 3 then
      story.show_textbox("story_200.text.1ddc")
    end
    if story.var(117) == 4 then
      story.show_textbox("story_200.text.1e4c")
    end
    if story.var(117) == 5 then
      story.show_textbox("story_200.text.1eba")
    end
    if story.var(117) == 6 then
      story.show_textbox("story_200.text.1f2a")
    end
    if story.var(117) == 7 then
      story.show_textbox("story_200.text.1f82")
    end
    if story.var(117) == 8 then
      story.show_textbox("story_200.text.1fd8")
    end
    if story.var(117) == 9 then
      story.show_textbox("story_200.text.2026")
    end
    story.set_var(119, 0) -- var, value
    story.play_sound(0, 23) -- bank, sound
    story.delay(20)
    routines.leave()
  elseif story.var(110) == 3 then
    routines.walk_out()
  else
    -- Sent away: the player says so and the customer leaves.
    story.set_dialog_owner(PLAYER)
    story.show_textbox("shop_job.story.dismiss")
    story.set_var(119, 0) -- var, value
    routines.leave()
  end
  return story.jump_to_file(49, 0x300)
end

-- The counter for meat: one turn after another until the customer buys or leaves, then the
-- original's lines for how it ended. The arms meet again, so the story's walk (dwx script-warps)
-- follows one path through the customer.
function routines.meat()
  story.set_dialog_owner(7)
  story.show_textbox("story_200.text.0056")
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_200.text.0098")
  while true do
    story.call_routine(259)
    if story.var(110) == 1 then
      story.set_var(247, 7) -- var, value
      story.call_routine(28)
      story.set_dialog_owner(7)
      story.show_textbox("story_200.text.0f64")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_200.text.0f94")
    elseif story.var(110) == 2 then
      story.set_dialog_owner(PLAYER)
      story.show_textbox("shop_job.story.talk")
      story.set_dialog_owner(7)
      story.show_textbox("shop_job.story.listens")
    else
      break
    end
  end
  if story.var(110) == 0 then
    -- Sold: the engine already added the profit to the tally; the line for the price level (117).
    story.set_var(247, 7) -- var, value
    story.call_routine(28)
    story.set_dialog_owner(7)
    story.show_textbox("story_200.text.0ad8")
    story.play_sound(8, 6) -- bank, sound
    story.delay(10)
    story.set_textbox_size(0, 12, 3) -- entity, columns, rows
    if story.var(117) == 1 then
      story.show_textbox("story_200.text.0b1e")
    end
    if story.var(117) == 2 then
      story.show_textbox("story_200.text.0b74")
    end
    if story.var(117) == 3 then
      story.show_textbox("story_200.text.0bd6")
    end
    if story.var(117) == 4 then
      story.show_textbox("story_200.text.0c2e")
    end
    if story.var(117) == 5 then
      story.show_textbox("story_200.text.0c90")
    end
    if story.var(117) == 6 then
      story.show_textbox("story_200.text.0ce8")
    end
    if story.var(117) == 7 then
      story.show_textbox("story_200.text.0d42")
    end
    if story.var(117) == 8 then
      story.show_textbox("story_200.text.0d94")
    end
    if story.var(117) == 9 then
      story.show_textbox("story_200.text.0dde")
    end
    story.set_var(119, 0) -- var, value
    story.play_sound(0, 23) -- bank, sound
    story.delay(20)
    routines.leave()
  elseif story.var(110) == 3 then
    routines.walk_out()
  else
    -- Sent away: the player says so and the customer leaves.
    story.set_dialog_owner(PLAYER)
    story.show_textbox("shop_job.story.dismiss")
    story.set_var(119, 0) -- var, value
    routines.leave()
  end
  return story.jump_to_file(49, 0x300)
end

-- Out of patience: the original's walk out, on one slot of the line (the original took two).
function routines.walk_out()
  story.set_dialog_owner(7)
  story.show_textbox("story_200.text.0ea0")
  story.set_var(119, 0) -- var, value
  story.play_sound(8, 7) -- bank, sound
  story.delay(10)
  story.set_var(247, 7) -- var, value
  story.set_var(248, 5) -- var, value
  story.call_routine(28)
  story.entity_walk_to(7, -109, -853, 0, 0) -- entity, x, z, animation, unknown07
  story.wait_for_entity(7)
  story.unload_entity(7)
  story.unload_species_model(68)
  story.delay(40)
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_200.text.0f1c")
  story.fadeout_hud(0)
end

-- The customer walks out of the shop; the job goes on in script 49 (section 0x300).
function routines.leave()
  story.set_var(247, 7) -- var, value
  story.set_var(248, 5) -- var, value
  story.call_routine(28)
  story.entity_walk_to(7, -109, -853, 0, 0) -- entity, x, z, animation, unknown07
  story.wait_for_entity(7)
  story.unload_entity(7)
  story.unload_species_model(68)
  story.delay(20)
end

return {
  script = 200,
  sections = {
    -- The improved job's own section (script 49 sends each customer here); the original's 0x33 is
    -- untouched.
    [0x300] = function()
      return routines.customer()
    end,
  },
}
