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
  story.load_species_model(99)
  story.spawn_npc(99, 3, 0) -- species, slot, autotalk
  story.delay(20)
  story.entity_walk_to(8, -501, -508, 0, 0) -- entity, x, z, animation, unknown07
  story.wait_for_entity(8)
  story.entity_look_at_entity(8, PLAYER) -- entity, target_entity
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
  story.set_dialog_owner(8)
  story.show_textbox("story_201.text.2154")
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_201.text.2194")
  while true do
    story.call_routine(259)
    if story.var(110) == 1 then
      story.set_var(247, 8) -- var, value
      story.call_routine(28)
      story.set_dialog_owner(8)
      story.show_textbox("story_201.text.3142")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_201.text.3178")
    elseif story.var(110) == 2 then
      story.set_dialog_owner(PLAYER)
      story.show_textbox("shop_job.story.talk")
      story.set_dialog_owner(8)
      story.show_textbox("shop_job.story.listens")
    else
      break
    end
  end
  if story.var(110) == 0 then
    -- Sold: the engine already added the profit to the tally; the line for the price level (117).
    story.set_var(247, 8) -- var, value
    story.call_routine(28)
    story.set_dialog_owner(8)
    story.show_textbox("story_201.text.2d44")
    story.play_sound(8, 6) -- bank, sound
    story.delay(10)
    story.set_textbox_size(0, 12, 3) -- entity, columns, rows
    if story.var(117) == 1 then
      story.show_textbox("story_201.text.2d96")
    end
    if story.var(117) == 2 then
      story.show_textbox("story_201.text.2dfc")
    end
    if story.var(117) == 3 then
      story.show_textbox("story_201.text.2e62")
    end
    if story.var(117) == 4 then
      story.show_textbox("story_201.text.2ec4")
    end
    if story.var(117) == 5 then
      story.show_textbox("story_201.text.2f28")
    end
    if story.var(117) == 6 then
      story.show_textbox("story_201.text.2f8c")
    end
    if story.var(117) == 7 then
      story.show_textbox("story_201.text.2ff2")
    end
    if story.var(117) == 8 then
      story.show_textbox("story_201.text.3054")
    end
    if story.var(117) == 9 then
      story.show_textbox("story_201.text.30ac")
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
  story.set_dialog_owner(8)
  story.show_textbox("story_201.text.0fce")
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_201.text.102a")
  while true do
    story.call_routine(259)
    if story.var(110) == 1 then
      story.set_var(247, 8) -- var, value
      story.call_routine(28)
      story.set_dialog_owner(8)
      story.show_textbox("story_201.text.20ec")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_201.text.2126")
    elseif story.var(110) == 2 then
      story.set_dialog_owner(PLAYER)
      story.show_textbox("shop_job.story.talk")
      story.set_dialog_owner(8)
      story.show_textbox("shop_job.story.listens")
    else
      break
    end
  end
  if story.var(110) == 0 then
    -- Sold: the engine already added the profit to the tally; the line for the price level (117).
    story.set_var(247, 8) -- var, value
    story.call_routine(28)
    story.set_dialog_owner(8)
    story.show_textbox("story_201.text.1ce0")
    story.play_sound(8, 6) -- bank, sound
    story.delay(10)
    story.set_textbox_size(0, 12, 3) -- entity, columns, rows
    if story.var(117) == 1 then
      story.show_textbox("story_201.text.1d34")
    end
    if story.var(117) == 2 then
      story.show_textbox("story_201.text.1da2")
    end
    if story.var(117) == 3 then
      story.show_textbox("story_201.text.1e0c")
    end
    if story.var(117) == 4 then
      story.show_textbox("story_201.text.1e7c")
    end
    if story.var(117) == 5 then
      story.show_textbox("story_201.text.1eea")
    end
    if story.var(117) == 6 then
      story.show_textbox("story_201.text.1f5a")
    end
    if story.var(117) == 7 then
      story.show_textbox("story_201.text.1fb2")
    end
    if story.var(117) == 8 then
      story.show_textbox("story_201.text.2008")
    end
    if story.var(117) == 9 then
      story.show_textbox("story_201.text.2056")
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
  story.set_dialog_owner(8)
  story.show_textbox("story_201.text.0056")
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_201.text.0090")
  while true do
    story.call_routine(259)
    if story.var(110) == 1 then
      story.set_var(247, 8) -- var, value
      story.call_routine(28)
      story.set_dialog_owner(8)
      story.show_textbox("story_201.text.0f56")
      story.set_dialog_owner(PLAYER)
      story.show_textbox("story_201.text.0f96")
    elseif story.var(110) == 2 then
      story.set_dialog_owner(PLAYER)
      story.show_textbox("shop_job.story.talk")
      story.set_dialog_owner(8)
      story.show_textbox("shop_job.story.listens")
    else
      break
    end
  end
  if story.var(110) == 0 then
    -- Sold: the engine already added the profit to the tally; the line for the price level (117).
    story.set_var(247, 8) -- var, value
    story.call_routine(28)
    story.set_dialog_owner(8)
    story.show_textbox("story_201.text.0aca")
    story.play_sound(8, 6) -- bank, sound
    story.delay(10)
    story.set_textbox_size(0, 12, 3) -- entity, columns, rows
    if story.var(117) == 1 then
      story.show_textbox("story_201.text.0b18")
    end
    if story.var(117) == 2 then
      story.show_textbox("story_201.text.0b6e")
    end
    if story.var(117) == 3 then
      story.show_textbox("story_201.text.0bd0")
    end
    if story.var(117) == 4 then
      story.show_textbox("story_201.text.0c28")
    end
    if story.var(117) == 5 then
      story.show_textbox("story_201.text.0c8a")
    end
    if story.var(117) == 6 then
      story.show_textbox("story_201.text.0ce2")
    end
    if story.var(117) == 7 then
      story.show_textbox("story_201.text.0d3c")
    end
    if story.var(117) == 8 then
      story.show_textbox("story_201.text.0d8e")
    end
    if story.var(117) == 9 then
      story.show_textbox("story_201.text.0dd8")
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
  story.set_dialog_owner(8)
  story.show_textbox("story_201.text.0e9a")
  story.set_var(119, 0) -- var, value
  story.play_sound(8, 7) -- bank, sound
  story.delay(10)
  story.set_var(247, 8) -- var, value
  story.set_var(248, 5) -- var, value
  story.call_routine(28)
  story.entity_walk_to(8, -109, -853, 0, 0) -- entity, x, z, animation, unknown07
  story.wait_for_entity(8)
  story.unload_entity(8)
  story.unload_species_model(99)
  story.delay(40)
  story.set_dialog_owner(PLAYER)
  story.show_textbox("story_201.text.0f06")
  story.fadeout_hud(0)
end

-- The customer walks out of the shop; the job goes on in script 49 (section 0x300).
function routines.leave()
  story.set_var(247, 8) -- var, value
  story.set_var(248, 5) -- var, value
  story.call_routine(28)
  story.entity_walk_to(8, -109, -853, 0, 0) -- entity, x, z, animation, unknown07
  story.wait_for_entity(8)
  story.unload_entity(8)
  story.unload_species_model(99)
  story.delay(20)
end

return {
  script = 201,
  sections = {
    -- The improved job's own section (script 49 sends each customer here); the original's 0x33 is
    -- untouched.
    [0x300] = function()
      return routines.customer()
    end,
  },
}
