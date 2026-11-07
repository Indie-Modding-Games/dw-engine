-- The game's own options (docs/modulos/apps/game.md, «Opciones en partida»): one entry per option.
-- The options page of the game menu shows each in the row list of its tab, after that tab's display
-- rows and in this order. A new option only needs its entry here (and the game handling its
-- setting): the page fills a free row of its tab with it, so no document needs a row of its own.
--
-- Each entry:
--   id       the row's element id while its tab is shown (also what routes select with menu_select)
--   setting  the UI setting the game publishes and the command the row sends to change it
--   label    the text key of the row's name
--   default  the value when the game has not published one yet, and what «Reset» sends
--   values   (choices only) the values the row steps through; without it the option is on or off
--   texts    (choices only) the prefix of each value's text key: texts .. "." .. value
--   range    (numbers only) { lowest, highest }: the row steps through the whole numbers between
--            them and shows the number and a bar; left stops at the lowest and right at the
--            highest, while confirming (or clicking the row) past the highest starts again from the
--            lowest, so one button reaches every step and never jumps from the lowest to the
--            highest
--   tab      the page's tab: game, graphics, sound or interface (game when missing or unknown)
--   help     the text key of the one or two lines the page shows under the list for the row
--   original the value that behaves as the original game; the page tags the row «As the original»
--            while it has that value and «Improvement» otherwise. Without it the option has no
--            counterpart in the original and is always an improvement.
--
-- The menu never changes a setting itself: it sends ui.send(setting, value) and shows what the game
-- publishes back.

game_options = {
  -- Game tab.
  -- Free exploration: the scripts' map changes go anywhere they can, whatever the story (off by
  -- default).
  {
    id = "free-exploration",
    setting = "free_exploration",
    label = "pause_menu.free_exploration",
    default = false,
    tab = "game",
    help = "pause_menu.help.free_exploration",
    original = false,
  },
  -- Walk with: the keyboard and the pad (keys, the default) or the mouse, never both.
  {
    id = "walk-control",
    setting = "walk_control",
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
    id = "autosave",
    setting = "autosave",
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
    id = "world-map",
    setting = "world_map",
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
    id = "minimap-items",
    setting = "minimap_items",
    label = "pause_menu.minimap_items",
    default = true,
    tab = "game",
    help = "pause_menu.help.minimap_items",
    original = false,
  },
  -- Bulk trades: the item trader and the card shop's packs ask how many at once (on by default).
  {
    id = "bulk-trades",
    setting = "bulk_trades",
    label = "pause_menu.bulk_trades",
    default = true,
    tab = "game",
    help = "pause_menu.help.bulk_trades",
    original = false,
  },
  -- Long names: the naming box takes names of up to twelve letters (on by default; off keeps the
  -- original's six).
  {
    id = "long-names",
    setting = "long_names",
    label = "pause_menu.long_names",
    default = true,
    tab = "game",
    help = "pause_menu.help.long_names",
    original = false,
  },
  -- Evolution requirements: the chart's tree tells what each target asks (on by default; off
  -- leaves the original chart).
  {
    id = "evolution-requirements",
    setting = "evolution_requirements",
    label = "pause_menu.evolution_requirements",
    default = true,
    tab = "game",
    help = "pause_menu.help.evolution_requirements",
    original = false,
  },
  -- Canonical stages: the chart names the stages as the series does and draws a Mega box (off by
  -- default; off leaves the original's names and boxes).
  {
    id = "canonical-stages",
    setting = "canonical_stages",
    label = "pause_menu.canonical_stages",
    default = false,
    tab = "game",
    help = "pause_menu.help.canonical_stages",
    original = false,
  },
  -- Status extras: the partner's tiredness bar and care mistakes on its status page (on by
  -- default; off leaves the original page).
  {
    id = "status-extras",
    setting = "status_extras",
    label = "pause_menu.status_extras",
    default = true,
    tab = "game",
    help = "pause_menu.help.status_extras",
    original = false,
  },
  -- Rare items: the field's random item spots may lay a rare evolution item (off by default; the
  -- ruleset's rare_items section says which, docs/modulos/game/evolution.md).
  {
    id = "rare-items",
    setting = "rare_items",
    label = "pause_menu.rare_items",
    default = false,
    tab = "game",
    help = "pause_menu.help.rare_items",
    original = false,
  },
  -- Graphics tab.
  -- Smooth motion: the characters and the camera glide between the original's frames (on by
  -- default; off draws them where each frame leaves them, as the original).
  {
    id = "smooth-motion",
    setting = "smooth_motion",
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
    id = "hd-scenery",
    setting = "hd_scenery",
    label = "pause_menu.hd_scenery",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.hd_scenery",
    original = false,
  },
  -- 3D characters: the characters some maps draw as flat prerendered pictures (the opening and the
  -- ending, the farm, the elder's house, the market stall) are drawn with their species' models
  -- (on by default; off draws the original's pictures). Only the look changes.
  {
    id = "model-characters",
    setting = "model_characters",
    label = "pause_menu.model_characters",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.model_characters",
    original = false,
  },
  -- Enhanced lighting: the time of day tints the scene (warm morning, orange dusk, blue night) and
  -- the lamps, fires and crystals painted in some maps light what is around them (on by default).
  {
    id = "enhanced-lighting",
    setting = "enhanced_lighting",
    label = "pause_menu.enhanced_lighting",
    default = true,
    tab = "graphics",
    help = "pause_menu.help.enhanced_lighting",
    original = false,
  },
  -- Vivid colours: the scene's muted colours gain saturation, a gentle contrast and a soft glow
  -- around the brightest spots; the menus never change (on by default).
  {
    id = "vivid-colors",
    setting = "vivid_colors",
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
    id = "shadows",
    setting = "shadows",
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
    id = "chibi-style",
    setting = "chibi_style",
    label = "pause_menu.chibi_style",
    values = { "off", "external" },
    texts = "pause_menu.chibi_style",
    default = "external",
    tab = "graphics",
    help = "pause_menu.help.chibi_style",
    original = "off",
  },
  -- Weather: rain, snow, clouds and sun on the island's outdoor maps (on by default; off shows
  -- every map as the original, which has no weather). It never changes a rule.
  {
    id = "weather",
    setting = "weather",
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
    id = "seasons",
    setting = "seasons",
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
    id = "music-volume",
    setting = "music_volume",
    label = "pause_menu.music_volume",
    range = { 0, 10 },
    default = 10,
    tab = "sound",
    help = "pause_menu.help.music_volume",
  },
  {
    id = "effects-volume",
    setting = "effects_volume",
    label = "pause_menu.effects_volume",
    range = { 0, 10 },
    default = 10,
    tab = "sound",
    help = "pause_menu.help.effects_volume",
  },
  -- Continuous music: a map whose song is the one playing lets it go on (on by default; off plays
  -- every map's song from its start, as the original).
  {
    id = "continuous-music",
    setting = "continuous_music",
    label = "pause_menu.continuous_music",
    default = true,
    tab = "sound",
    help = "pause_menu.help.continuous_music",
    original = false,
  },
  -- Interface tab.
  -- Translated signs: the signs painted in the maps read in the player's language (on by default).
  {
    id = "translated-signs",
    setting = "translated_signs",
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
    id = "show-minimap",
    setting = "minimap",
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
    id = "minimap-enemies",
    setting = "minimap_enemies",
    label = "pause_menu.minimap_enemies",
    default = true,
    tab = "interface",
    help = "pause_menu.help.minimap_enemies",
    original = false,
  },
  -- Time in digits: the game time as HH:MM under the HUD's clock (on by default).
  {
    id = "digital-time",
    setting = "digital_time",
    label = "pause_menu.digital_time",
    default = true,
    tab = "interface",
    help = "pause_menu.help.digital_time",
    original = false,
  },
  -- Save notice: a small figure and "Saving" in a corner while the game saves (on by default).
  {
    id = "save-notice",
    setting = "save_notice",
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
    id = "need-bubble-stack",
    setting = "need_bubble_stack",
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
    id = "shop-job-bar",
    setting = "shop_job_bar",
    label = "pause_menu.shop_job_bar",
    default = true,
    tab = "interface",
    help = "pause_menu.help.shop_job_bar",
    original = false,
  },
  -- Space after names: a name the texts insert gets a space when the original's text runs it into
  -- the next word (on by default; off draws them stuck, as the original).
  {
    id = "name-spacing",
    setting = "name_spacing",
    label = "pause_menu.name_spacing",
    default = true,
    tab = "game",
    help = "pause_menu.help.name_spacing",
    original = false,
  },
}

-- The option's current value: what the game published, or its default.
function game_options.value(option)
  local value = ui.setting(option.setting)
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

-- The text of the option's current value, in the player's language.
function game_options.text(option)
  local value = game_options.value(option)
  if option.range ~= nil then
    return string.format("%d", value)
  end
  if option.values ~= nil then
    return t(option.texts .. "." .. tostring(value))
  end
  return t(value and "common.yes" or "common.no")
end

-- Asks the game for the option's value step entries away: a toggle flips; a choice moves through
-- its values both ways, wrapping; a number moves by step and stops at its lowest, and at its
-- highest it stops when clamp is set (left and right) or starts again from its lowest (confirming).
function game_options.change(option, step, clamp)
  local value = game_options.value(option)
  if option.range ~= nil then
    local lowest, highest = option.range[1], option.range[2]
    local next = value + step
    if next > highest then
      next = (value == highest and not clamp) and lowest or highest
    end
    next = math.max(lowest, next)
    if next ~= value then
      ui.send(option.setting, next)
    end
    return
  end
  if option.values == nil then
    ui.send(option.setting, not value)
    return
  end
  local index = 1
  for at, known in ipairs(option.values) do
    if known == value then
      index = at
    end
  end
  ui.send(option.setting, option.values[((index - 1 + step) % #option.values) + 1])
end

-- How the page draws the option: "toggle" (on or off), "list" (values) or "range" (a number).
function game_options.kind(option)
  if option.range ~= nil then
    return "range"
  end
  return option.values ~= nil and "list" or "toggle"
end
