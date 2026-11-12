-- The UI API's options (docs/modding/ui-api.md, namespace options): the game's own options, one
-- descriptor each, their values as the game publishes them and the command that asks the game to
-- change one. The options page of the default UI lists them in this order (ui/common/
-- game_options.lua); a new option only needs its entry here and the game handling its setting.
--
-- Each descriptor:
--   name     the option's name: the setting the game publishes and the command that changes it
--   label    the text key of the option's name
--   default  the value when the game has not published one yet, and what «Reset» asks for
--   values   (choices only) the values the option takes; without it the option is on or off
--   texts    (choices only) the prefix of each value's text key: texts .. "." .. value
--   range    (numbers only) { lowest, highest }: the whole numbers it takes
--   tab      the options page's tab: game, graphics, sound or interface
--   help     the text key of the one or two lines that tell what the option does
--   original the value that behaves as the original game; without it the option has no
--            counterpart in the original and is always an improvement.
--
-- Every function returns fresh tables; changing them changes nothing in the game.

options = {}

local descriptors = {
  -- Game tab.
  -- Walk with: the keyboard and the pad (keys, the default) or the mouse, never both.
  {
    name = "walk_control",
    label = "pause_menu.walk_control",
    values = { "keys", "mouse" },
    texts = "pause_menu.walk_control",
    default = "keys",
    tab = "game",
    help = "pause_menu.help.walk_control",
    original = "keys",
  },
  -- Autosave: the game saves by itself to the adventure's own slot when the rules say (on by
  -- default; off, only the player's own saves write).
  {
    name = "autosave",
    label = "pause_menu.autosave",
    default = true,
    tab = "game",
    help = "pause_menu.help.autosave",
    original = false,
  },
  -- Island map: M (or the pad's left shoulder) opens a map of the whole island with where the
  -- player is and where his map leads (on by default; off leaves the key without a use, as the
  -- original has no such map).
  {
    name = "world_map",
    label = "pause_menu.world_map",
    default = true,
    tab = "game",
    help = "pause_menu.help.world_map",
    original = false,
  },
  -- Items on the minimap: a green diamond for each item lying on the map's ground, where it lies
  -- (on by default; off leaves the minimap without them). It lives in this tab, by the island map,
  -- because the interface tab already fills the page at 1280x720.
  {
    name = "minimap_items",
    label = "pause_menu.minimap_items",
    default = true,
    tab = "game",
    help = "pause_menu.help.minimap_items",
    original = false,
  },
  -- Bulk trades: the item trader and the card shop's packs ask how many at once (on by default).
  {
    name = "bulk_trades",
    label = "pause_menu.bulk_trades",
    default = true,
    tab = "game",
    help = "pause_menu.help.bulk_trades",
    original = false,
  },
  -- Long names: the naming box takes names of up to twelve letters (on by default; off keeps the
  -- original's six).
  {
    name = "long_names",
    label = "pause_menu.long_names",
    default = true,
    tab = "game",
    help = "pause_menu.help.long_names",
    original = false,
  },
  -- Evolution requirements: the chart's tree tells what each target asks (on by default; off
  -- leaves the original chart).
  {
    name = "evolution_requirements",
    label = "pause_menu.evolution_requirements",
    default = true,
    tab = "game",
    help = "pause_menu.help.evolution_requirements",
    original = false,
  },
  -- Canonical stages: the chart names the stages as the series does and draws a Mega box (off by
  -- default; off leaves the original's names and boxes).
  {
    name = "canonical_stages",
    label = "pause_menu.canonical_stages",
    default = false,
    tab = "game",
    help = "pause_menu.help.canonical_stages",
    original = false,
  },
  -- Status extras: the partner's tiredness bar and care mistakes on its status page (on by
  -- default; off leaves the original page).
  {
    name = "status_extras",
    label = "pause_menu.status_extras",
    default = true,
    tab = "game",
    help = "pause_menu.help.status_extras",
    original = false,
  },
  -- Rare items: the field's random item spots may lay a rare evolution item (off by default; the
  -- ruleset's rare_items section says which, docs/modulos/game/evolution.md).
  {
    name = "rare_items",
    label = "pause_menu.rare_items",
    default = false,
    tab = "game",
    help = "pause_menu.help.rare_items",
    original = false,
  },
  -- Quick battles: fights at twice the speed, switched with R (the right trigger on a pad) or
  -- always (normal, the original's, by default). Every tick is the original's, so only the wait
  -- changes.
  {
    name = "battle_speed",
    label = "pause_menu.battle_speed",
    values = { "normal", "key", "fast" },
    texts = "pause_menu.battle_speed",
    default = "normal",
    tab = "game",
    help = "pause_menu.help.battle_speed",
    original = "normal",
  },
  -- Enemy life bars: a bar over every enemy still standing for the whole fight, cups too (off by
  -- default: the original's, a wild enemy's a while after it is hit and none in a cup).
  {
    name = "enemy_hp_bars",
    label = "pause_menu.enemy_hp_bars",
    default = false,
    tab = "game",
    help = "pause_menu.help.enemy_hp_bars",
    original = false,
  },
  -- Gym sessions: a course the story starts at a gym runs up to five sessions in a row, each the
  -- original's (1 by default: one session each time, as the original).
  {
    name = "gym_sessions",
    label = "pause_menu.gym_sessions",
    range = { 1, 5 },
    default = 1,
    tab = "game",
    help = "pause_menu.help.gym_sessions",
    original = 1,
  },
  -- Battles without the wait: a fight in the field starts the moment it triggers, without the
  -- original's lead-in (off by default: the original's lead-in plays).
  {
    name = "quick_battle_start",
    label = "pause_menu.quick_battle_start",
    default = false,
    tab = "game",
    help = "pause_menu.help.quick_battle_start",
    original = false,
  },
  -- Difficulty: normal is the original's; easy takes the ruleset's difficulty changes
  -- (rules/<id>/difficulty.yaml), so a mod tunes it.
  {
    name = "difficulty",
    label = "pause_menu.difficulty",
    values = { "normal", "easy" },
    texts = "pause_menu.difficulty",
    default = "normal",
    tab = "game",
    help = "pause_menu.help.difficulty",
    original = "normal",
  },
  -- Skip evolutions: confirm or cancel ends an evolution's scene at once, with the same result
  -- (off by default: the original's scene plays to the end).
  {
    name = "skip_evolution",
    label = "pause_menu.skip_evolution",
    default = false,
    tab = "game",
    help = "pause_menu.help.skip_evolution",
    original = false,
  },
  -- Cancel closes choices: a cancel in a choice the original lets run on into its first option's
  -- code only closes it (off by default: the original's cancel).
  {
    name = "choice_cancel_closes",
    label = "pause_menu.choice_cancel_closes",
    default = false,
    tab = "game",
    help = "pause_menu.help.choice_cancel_closes",
    original = false,
  },
  -- Fair sickness chance: a sickness chance below zero is none, not a sure one (off by
  -- default: the original's quirk).
  {
    name = "fair_sickness_chance",
    label = "pause_menu.fair_sickness_chance",
    default = false,
    tab = "game",
    help = "pause_menu.help.fair_sickness_chance",
    original = false,
  },
  -- Fair evolution averages: each evolution candidate is weighed by its own average, not one
  -- carried over from the candidate before it (off by default: the original's quirk).
  {
    name = "fair_evolution_average",
    label = "pause_menu.fair_evolution_average",
    default = false,
    tab = "game",
    help = "pause_menu.help.fair_evolution_average",
    original = false,
  },
  -- Fair move forgetting: forgetting a move clears only that move, not another one sixteen places
  -- after it too (off by default: the original's quirk).
  {
    name = "fair_move_forgetting",
    label = "pause_menu.fair_move_forgetting",
    default = false,
    tab = "game",
    help = "pause_menu.help.fair_move_forgetting",
    original = false,
  },
  -- Fair card stock: the card shop's daily stock never shows the same card twice (off by
  -- default: the original's quirk).
  {
    name = "fair_card_stock",
    label = "pause_menu.fair_card_stock",
    default = false,
    tab = "game",
    help = "pause_menu.help.fair_card_stock",
    original = false,
  },
  -- Choose the egg after losing all lives: the egg box opens then too, as after a natural death
  -- (off by default: the original draws the egg at random).
  {
    name = "choose_egg_after_lives_lost",
    label = "pause_menu.choose_egg_after_lives_lost",
    default = false,
    tab = "game",
    help = "pause_menu.help.choose_egg_after_lives_lost",
    original = false,
  },
  -- Generous talk range: a confirm reaches characters a little further on every side and further
  -- ahead, and reads a sign's cell in front of the player (off by default: the original's reach).
  {
    name = "generous_talk",
    label = "pause_menu.generous_talk",
    default = false,
    tab = "game",
    help = "pause_menu.help.generous_talk",
    original = false,
  },
  -- Extra toilets: the toilet booths the game pack lists (field/extra_toilets) stand on their
  -- maps (off by default: only the original's toilets).
  {
    name = "extra_toilets",
    label = "pause_menu.extra_toilets",
    default = false,
    tab = "game",
    help = "pause_menu.help.extra_toilets",
    original = false,
  },
  -- Graphics tab.
  -- Smooth motion: the characters and the camera glide between the original's frames (on by
  -- default; off draws them where each frame leaves them, as the original).
  {
    name = "smooth_motion",
    label = "pause_menu.smooth_motion",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.smooth_motion",
    original = false,
  },
  -- HD scenery: the field's prerendered scenery drawn smooth at the window's resolution, with clean
  -- edges (on by default; off draws the original's pixels). Only the look changes: collisions,
  -- triggers and positions stay the original's. The title's options change it too.
  {
    name = "hd_scenery",
    label = "pause_menu.hd_scenery",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.hd_scenery",
    original = false,
  },
  -- Scenery quality: with HD scenery on, the background filtered smooth at the window's resolution
  -- or doubled once per map by the engine's upscaler first (upscaled, the default; cached between
  -- runs). It has no counterpart in the original, so it is always an improvement.
  {
    name = "scenery_quality",
    label = "pause_menu.scenery_quality",
    values = { "upscaled", "smooth" },
    texts = "pause_menu.scenery_quality",
    default = "upscaled",
    tab = "graphics",
    help = "pause_menu.help.scenery_quality",
  },
  -- Antialiasing: the scene drawn with 4x multisampling, smooth model outlines (on by default; off
  -- shows the hard edges the original draws).
  {
    name = "antialiasing",
    label = "pause_menu.antialiasing",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.antialiasing",
    original = false,
  },
  -- Model textures: the models' textures in the field, in battle and in the menus' figures, doubled
  -- by the upscaler and filtered (upscaled, the default), filtered (smooth), or with the original's
  -- square texels (nearest).
  {
    name = "texture_filter",
    label = "pause_menu.texture_filter",
    values = { "upscaled", "smooth", "nearest" },
    texts = "pause_menu.texture_filter",
    default = "upscaled",
    tab = "graphics",
    help = "pause_menu.help.texture_filter",
    original = "nearest",
  },
  -- 3D characters: the characters some maps draw as flat prerendered pictures (the opening and the
  -- ending, the farm, the elder's house, the market stall) are drawn with their species' models
  -- (on by default; off draws the original's pictures). Only the look changes.
  {
    name = "model_characters",
    label = "pause_menu.model_characters",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.model_characters",
    original = false,
  },
  -- Widescreen pictures: in a window wider than 4:3 the title's pictures fill its sides with a
  -- blurred copy of themselves and the movies fill its width, their shape kept (on by default; off
  -- shows them in the original's 4:3 frame on black). Only the look changes.
  {
    name = "widescreen_pictures",
    label = "pause_menu.widescreen_pictures",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.widescreen_pictures",
    original = false,
  },
  -- Enhanced lighting: the time of day tints the scene (warm morning, orange dusk, blue night) and
  -- the lamps, fires and crystals painted in some maps light what is around them (on by default).
  {
    name = "enhanced_lighting",
    label = "pause_menu.enhanced_lighting",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.enhanced_lighting",
    original = false,
  },
  -- Vivid colours: the scene's muted colours gain saturation, a gentle contrast and a soft glow
  -- around the brightest spots; the menus never change (on by default).
  {
    name = "vivid_colors",
    label = "pause_menu.vivid_colors",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.vivid_colors",
    original = false,
  },
  -- Shadows: off, round (the original's round shadow under every character) or sun (the round one
  -- and a soft one the sun casts, turning and stretching with the clock, gone at night and in
  -- caves and houses; the default). The game applies it at once and saves it.
  {
    name = "shadows",
    label = "pause_menu.shadows",
    values = { "sun", "round", "off" },
    texts = "pause_menu.shadows",
    default = "sun",
    tab = "graphics",
    help = "pause_menu.help.shadows",
    original = "round",
  },
  -- Chibi style: the original game's models are chibi already; a model from elsewhere (a mod's, an
  -- imported one) that is not is drawn with the original's proportions, the drawing only: off or
  -- external models (the default).
  {
    name = "chibi_style",
    label = "pause_menu.chibi_style",
    values = { "off", "external" },
    texts = "pause_menu.chibi_style",
    default = "external",
    tab = "graphics",
    help = "pause_menu.help.chibi_style",
    original = "off",
  },
  -- Weather: rain, storms, snow, fog, wind, clouds and sun on the island's outdoor maps (on by
  -- default; off shows every map as the original, which has no weather). It never changes a rule.
  {
    name = "weather",
    label = "pause_menu.weather",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.weather",
    original = false,
  },
  -- Seasons: spring, summer, autumn and winter take turns every few game days, colouring the
  -- outdoor scenery, with petals or falling leaves and their own weather odds (on by default; off
  -- shows every map and rolls the weather exactly as without seasons). It never changes a rule.
  {
    name = "seasons",
    label = "pause_menu.seasons",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.seasons",
    original = false,
  },
  -- Sound tab.
  -- Music volume and effects volume: a step from 0 (silent) to 10 (the original's level, the
  -- default). The game applies them at once to the music (with the movies' sound) and to the
  -- effects and the menus' sounds, and saves them (an engine addition: the original has no sound
  -- option).
  {
    name = "music_volume",
    label = "pause_menu.music_volume",
    range = { 0, 10 },
    default = 10,
    tab = "sound",
    help = "pause_menu.help.music_volume",
  },
  {
    name = "effects_volume",
    label = "pause_menu.effects_volume",
    range = { 0, 10 },
    default = 10,
    tab = "sound",
    help = "pause_menu.help.effects_volume",
  },
  -- Continuous music: a map whose song is the one playing lets it go on (on by default; off plays
  -- every map's song from its start, as the original).
  {
    name = "continuous_music",
    label = "pause_menu.continuous_music",
    default = true,
    tab = "sound",
    help = "pause_menu.help.continuous_music",
    original = false,
  },
  -- Reverb: the sound chip's reverb on the tones that ask for it, as the original sets it at boot
  -- (on by default; off plays every sound dry).
  {
    name = "reverb",
    label = "pause_menu.reverb",
    default = true,
    tab = "sound",
    help = "pause_menu.help.reverb",
    original = true,
  },
  -- Interface tab.
  -- Translated signs: the signs painted in the maps read in the player's language (on by default).
  {
    name = "translated_signs",
    label = "pause_menu.translated_signs",
    default = true,
    tab = "interface",
    help = "pause_menu.help.translated_signs",
    original = false,
  },
  -- Show minimap: the round minimap at the top right, with the map's name and the day and time
  -- (on by default, with either look; off leaves the screen as the original's). The game applies
  -- it at once and saves it with the user's settings; the minimap page of the menu changes it too.
  {
    name = "minimap",
    label = "pause_menu.minimap_mode",
    values = { "on", "off" },
    texts = "pause_menu.minimap_mode",
    default = "on",
    tab = "interface",
    help = "pause_menu.help.minimap",
    original = "off",
  },
  -- Enemies on the minimap: a red dot for each character on the map that can start a battle, where
  -- it stands (on by default; off leaves the minimap with the player and the partner only).
  {
    name = "minimap_enemies",
    label = "pause_menu.minimap_enemies",
    default = true,
    tab = "interface",
    help = "pause_menu.help.minimap_enemies",
    original = false,
  },
  -- Time in digits: the game time as HH:MM under the HUD's clock (on by default).
  {
    name = "digital_time",
    label = "pause_menu.digital_time",
    default = true,
    tab = "interface",
    help = "pause_menu.help.digital_time",
    original = false,
  },
  -- Save notice: a small figure and "Saving" in a corner while the game saves (on by default).
  {
    name = "save_notice",
    label = "pause_menu.save_notice",
    default = true,
    tab = "interface",
    help = "pause_menu.help.save_notice",
    original = false,
  },
  -- Several needs: the bubble over the partner shows up to three of its needs side by side, most
  -- urgent first (on by default; off keeps the original's one at a time, the two most urgent
  -- taking turns).
  {
    name = "need_bubble_stack",
    label = "pause_menu.need_bubble_stack",
    default = true,
    tab = "interface",
    help = "pause_menu.help.need_bubble_stack",
    original = false,
  },
  -- Recruit bar: while a job or a match decides whether someone joins the city, a bar shows how
  -- close the player is: the canyon shop's takings and customers, the stones game's points and
  -- throws (on by default; off leaves the original's screen).
  {
    name = "shop_job_bar",
    label = "pause_menu.shop_job_bar",
    default = true,
    tab = "interface",
    help = "pause_menu.help.shop_job_bar",
    original = false,
  },
  -- Space after names: a name the texts insert gets a space when the original's text runs it into
  -- the next word (on by default; off draws them stuck, as the original).
  {
    name = "name_spacing",
    label = "pause_menu.name_spacing",
    default = true,
    tab = "game",
    help = "pause_menu.help.name_spacing",
    original = false,
  },
  -- Free exploration: the scripts' map changes go anywhere they can, whatever the story (off by
  -- default). Last of its tab, after the gameplay options: the tab opens focused on its first row,
  -- so opening the options and confirming never turns it on by chance.
  {
    name = "free_exploration",
    label = "pause_menu.free_exploration",
    default = false,
    tab = "game",
    help = "pause_menu.help.free_exploration",
    original = false,
  },
  -- Shop extras: the focused item's description under a shop's list, how many of each the bag
  -- holds and the money a trade leaves (off by default: the original's shop shows a description
  -- only when Pause asks for it). The shop document reads the setting itself.
  {
    name = "shop_extras",
    label = "pause_menu.shop_extras",
    default = false,
    tab = "interface",
    help = "pause_menu.help.shop_extras",
    original = false,
  },
  -- Favourite food: the partner menu's status page names the partner's favourite food and the item
  -- lists (the bag, the battle's bag, a shop) mark it in gold with a heart (on by default; off, the
  -- original's look). The documents read the setting themselves.
  {
    name = "favorite_food",
    label = "pause_menu.favorite_food",
    default = true,
    tab = "interface",
    help = "pause_menu.help.favorite_food",
    original = false,
  },
  -- Art fixes: the original look draws right what the original reads from the wrong part of its
  -- textures, the status page's happiness label (off by default, as the original).
  {
    name = "art_fixes",
    label = "pause_menu.art_fixes",
    default = false,
    tab = "interface",
    help = "pause_menu.help.art_fixes",
    original = false,
  },
  -- Developer tools: the debug console, the developer menu and the map overlay. Its default is the
  -- game's (on in a development build, off in a package), which it publishes at start.
  {
    name = "dev_tools",
    label = "pause_menu.dev_tools",
    default = false,
    tab = "interface",
    help = "pause_menu.help.dev_tools",
  },
}

local function copy(list)
  if list == nil then
    return nil
  end
  local copied = {}
  for index, value in ipairs(list) do
    copied[index] = value
  end
  return copied
end

local function descriptor(name)
  for _, option in ipairs(descriptors) do
    if option.name == name then
      return option
    end
  end
  return nil
end

-- How an option takes its value: "toggle" (on or off), "list" (one of values) or "range" (a whole
-- number).
local function kind_of(option)
  if option.range ~= nil then
    return "range"
  end
  return option.values ~= nil and "list" or "toggle"
end

-- Every option, in the page's order: {name, kind, label_key, help_key, tab, default, original,
-- values, texts, min, max}.
function options.list()
  local list = {}
  for index, option in ipairs(descriptors) do
    list[index] = {
      name = option.name,
      kind = kind_of(option),
      label_key = option.label,
      help_key = option.help,
      tab = option.tab,
      default = option.default,
      original = option.original,
      values = copy(option.values),
      texts = option.texts,
      min = option.range and option.range[1] or nil,
      max = option.range and option.range[2] or nil,
    }
  end
  return list
end

-- The option's value now: what the game published, or its default. nil for an option the game does
-- not have.
function options.get(name)
  local option = descriptor(name)
  if option == nil then
    return nil
  end
  local value = ui.setting(option.name)
  if option.range ~= nil then
    if type(value) ~= "number" then
      return option.default
    end
    return math.max(option.range[1], math.min(option.range[2], math.floor(value + 0.5)))
  end
  if option.values ~= nil then
    for _, known in ipairs(option.values) do
      if known == value then
        return value
      end
    end
    return option.default
  end
  if type(value) ~= "boolean" then
    return option.default == true
  end
  return value
end

-- Asks the game to give an option a value. Returns true when the request is queued, or false and
-- why; the game answers with options.changed.
function options.set(name, value)
  local option = descriptor(name)
  if option == nil then
    return false, "no option " .. tostring(name)
  end
  local kind = kind_of(option)
  if kind == "range" then
    if
      type(value) ~= "number"
      or value ~= math.floor(value)
      or value < option.range[1]
      or value > option.range[2]
    then
      return false,
        string.format(
          "%s takes a whole number from %d to %d",
          name,
          option.range[1],
          option.range[2]
        )
    end
  elseif kind == "list" then
    local known = false
    for _, candidate in ipairs(option.values) do
      known = known or candidate == value
    end
    if not known then
      return false, name .. " takes one of its values"
    end
  elseif type(value) ~= "boolean" then
    return false, name .. " takes true or false"
  end
  ui.send(option.name, value)
  return true
end

-- options.changed: an option changed (name and value): the game's options and the ones the
-- options page draws itself (display, language, look).
local page_options =
  { "fullscreen", "language", "minimap_size", "text_font", "ui_scale", "ui_skin", "vsync" }
for _, option in ipairs(descriptors) do
  event_source(option.name, "options.changed")
end
for _, name in ipairs(page_options) do
  event_source(name, "options.changed")
end
