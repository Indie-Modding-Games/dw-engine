-- The improved shop job's customer (game/shop_job; docs/reference/trabajo-tienda.md): the
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
  story.load_species_model(80)
  story.spawn_npc(80, 1, 0) -- species, slot, autotalk
  story.delay(20)
  story.entity_walk_to(6, -501, -508, 0, 0) -- entity, x, z, animation, unknown07
  story.wait_for_entity(6)
  story.entity_look_at_entity(6, PLAYER) -- entity, target_entity
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
  story.set_dialog_owner(6)
  story.show_textbox("story_199.text.20fc")
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_199.text.213a")
  while true do
    story.call_routine(259)
    if story.var(110) == 1 then
      story.set_var(247, 6) -- var, value
      story.call_routine(28)
      story.set_dialog_owner(6)
      story.show_textbox("story_199.text.30d6")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_199.text.3106")
    elseif story.var(110) == 2 then
      story.set_dialog_owner(PLAYER)
      story.show_textbox("shop_job.story.talk")
      story.set_dialog_owner(6)
      story.show_textbox("shop_job.story.listens")
    else
      break
    end
  end
  if story.var(110) == 0 then
    -- Sold: the engine already added the profit to the tally; the line for the price level (117).
    story.set_var(247, 6) -- var, value
    story.call_routine(28)
    story.set_dialog_owner(6)
    story.show_textbox("story_199.text.2cdc")
    story.play_sound(8, 6) -- bank, sound
    story.delay(10)
    story.set_textbox_size(0, 12, 3) -- entity, columns, rows
    if story.var(117) == 1 then
      story.show_textbox("story_199.text.2d2a")
    end
    if story.var(117) == 2 then
      story.show_textbox("story_199.text.2d90")
    end
    if story.var(117) == 3 then
      story.show_textbox("story_199.text.2df6")
    end
    if story.var(117) == 4 then
      story.show_textbox("story_199.text.2e58")
    end
    if story.var(117) == 5 then
      story.show_textbox("story_199.text.2ebc")
    end
    if story.var(117) == 6 then
      story.show_textbox("story_199.text.2f20")
    end
    if story.var(117) == 7 then
      story.show_textbox("story_199.text.2f86")
    end
    if story.var(117) == 8 then
      story.show_textbox("story_199.text.2fe8")
    end
    if story.var(117) == 9 then
      story.show_textbox("story_199.text.3040")
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
  story.set_dialog_owner(6)
  story.show_textbox("story_199.text.0fa8")
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_199.text.0fee")
  while true do
    story.call_routine(259)
    if story.var(110) == 1 then
      story.set_var(247, 6) -- var, value
      story.call_routine(28)
      story.set_dialog_owner(6)
      story.show_textbox("story_199.text.208c")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_199.text.20c4")
    elseif story.var(110) == 2 then
      story.set_dialog_owner(PLAYER)
      story.show_textbox("shop_job.story.talk")
      story.set_dialog_owner(6)
      story.show_textbox("shop_job.story.listens")
    else
      break
    end
  end
  if story.var(110) == 0 then
    -- Sold: the engine already added the profit to the tally; the line for the price level (117).
    story.set_var(247, 6) -- var, value
    story.call_routine(28)
    story.set_dialog_owner(6)
    story.show_textbox("story_199.text.1c82")
    story.play_sound(8, 6) -- bank, sound
    story.delay(10)
    story.set_textbox_size(0, 12, 3) -- entity, columns, rows
    if story.var(117) == 1 then
      story.show_textbox("story_199.text.1cd4")
    end
    if story.var(117) == 2 then
      story.show_textbox("story_199.text.1d42")
    end
    if story.var(117) == 3 then
      story.show_textbox("story_199.text.1dac")
    end
    if story.var(117) == 4 then
      story.show_textbox("story_199.text.1e1c")
    end
    if story.var(117) == 5 then
      story.show_textbox("story_199.text.1e8a")
    end
    if story.var(117) == 6 then
      story.show_textbox("story_199.text.1efa")
    end
    if story.var(117) == 7 then
      story.show_textbox("story_199.text.1f52")
    end
    if story.var(117) == 8 then
      story.show_textbox("story_199.text.1fa8")
    end
    if story.var(117) == 9 then
      story.show_textbox("story_199.text.1ff6")
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
  story.set_dialog_owner(6)
  story.show_textbox("story_199.text.0056")
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_199.text.008c")
  while true do
    story.call_routine(259)
    if story.var(110) == 1 then
      story.set_var(247, 6) -- var, value
      story.call_routine(28)
      story.set_dialog_owner(6)
      story.show_textbox("story_199.text.0f44")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_199.text.0f74")
    elseif story.var(110) == 2 then
      story.set_dialog_owner(PLAYER)
      story.show_textbox("shop_job.story.talk")
      story.set_dialog_owner(6)
      story.show_textbox("shop_job.story.listens")
    else
      break
    end
  end
  if story.var(110) == 0 then
    -- Sold: the engine already added the profit to the tally; the line for the price level (117).
    story.set_var(247, 6) -- var, value
    story.call_routine(28)
    story.set_dialog_owner(6)
    story.show_textbox("story_199.text.0aca")
    story.play_sound(8, 6) -- bank, sound
    story.delay(10)
    story.set_textbox_size(0, 12, 3) -- entity, columns, rows
    if story.var(117) == 1 then
      story.show_textbox("story_199.text.0b12")
    end
    if story.var(117) == 2 then
      story.show_textbox("story_199.text.0b68")
    end
    if story.var(117) == 3 then
      story.show_textbox("story_199.text.0bca")
    end
    if story.var(117) == 4 then
      story.show_textbox("story_199.text.0c22")
    end
    if story.var(117) == 5 then
      story.show_textbox("story_199.text.0c84")
    end
    if story.var(117) == 6 then
      story.show_textbox("story_199.text.0cdc")
    end
    if story.var(117) == 7 then
      story.show_textbox("story_199.text.0d36")
    end
    if story.var(117) == 8 then
      story.show_textbox("story_199.text.0d88")
    end
    if story.var(117) == 9 then
      story.show_textbox("story_199.text.0dd2")
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
  story.set_dialog_owner(6)
  story.show_textbox("story_199.text.0e94")
  story.set_var(119, 0) -- var, value
  story.play_sound(8, 7) -- bank, sound
  story.delay(10)
  story.set_var(247, 6) -- var, value
  story.set_var(248, 5) -- var, value
  story.call_routine(28)
  story.entity_walk_to(6, -109, -853, 0, 0) -- entity, x, z, animation, unknown07
  story.wait_for_entity(6)
  story.unload_entity(6)
  story.unload_species_model(80)
  story.delay(40)
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_199.text.0ef6")
  story.fadeout_hud(0)
end

-- The customer walks out of the shop; the job goes on in script 49 (section 0x300).
function routines.leave()
  story.set_var(247, 6) -- var, value
  story.set_var(248, 5) -- var, value
  story.call_routine(28)
  story.entity_walk_to(6, -109, -853, 0, 0) -- entity, x, z, animation, unknown07
  story.wait_for_entity(6)
  story.unload_entity(6)
  story.unload_species_model(80)
  story.delay(20)
end

return {
  script = 199,
  sections = {
    -- The improved job's own section (script 49 sends each customer here); the original's 0x33 is
    -- untouched.
    [0x300] = function()
      return routines.customer()
    end,
  },
}
