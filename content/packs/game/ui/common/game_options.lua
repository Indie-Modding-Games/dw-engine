-- The game's own options (docs/modulos/apps/game.md, «Opciones en partida»): the list the options
-- page of the game menu shows below the display rows, one entry per option, in this order. A new
-- option only needs its entry here (and the game handling its setting): the page fills a free row
-- with it, so no document needs a row of its own.
--
-- Each entry:
--   id       the row's element id (also what routes select with menu_select)
--   setting  the UI setting the game publishes and the command the row sends to change it
--   label    the text key of the row's name
--   default  the value when the game has not published one yet
--   values   (choices only) the values the row cycles through; without it the option is on or off
--   texts    (choices only) the prefix of each value's text key: texts .. "." .. value
--
-- The menu never changes a setting itself: it sends ui.send(setting, value) and shows what the game
-- publishes back.

game_options = {
  -- Free exploration: the scripts' map changes go anywhere they can, whatever the story (off by
  -- default).
  {
    id = "free-exploration",
    setting = "free_exploration",
    label = "pause_menu.free_exploration",
    default = false,
  },
  -- Smooth motion: the characters and the camera glide between the original's frames (on by
  -- default; off draws them where each frame leaves them, as the original).
  {
    id = "smooth-motion",
    setting = "smooth_motion",
    label = "pause_menu.smooth_motion",
    default = true,
  },
  -- Save notice: a small figure and "Saving" in a corner while the game saves (on by default).
  { id = "save-notice", setting = "save_notice", label = "pause_menu.save_notice", default = true },
  -- Time in digits: the game time as HH:MM under the HUD's clock (on by default).
  {
    id = "digital-time",
    setting = "digital_time",
    label = "pause_menu.digital_time",
    default = true,
  },
  -- Bulk trades: the item trader and the card shop's packs ask how many at once (on by default).
  { id = "bulk-trades", setting = "bulk_trades", label = "pause_menu.bulk_trades", default = true },
  -- Evolution requirements: the chart's tree tells what each target asks (on by default; off
  -- leaves the original chart).
  {
    id = "evolution-requirements",
    setting = "evolution_requirements",
    label = "pause_menu.evolution_requirements",
    default = true,
  },
  -- Translated signs: the signs painted in the maps read in the player's language (on by default).
  {
    id = "translated-signs",
    setting = "translated_signs",
    label = "pause_menu.translated_signs",
    default = true,
  },
  -- Status extras: the partner's tiredness bar and care mistakes on its status page (on by
  -- default; off leaves the original page).
  {
    id = "status-extras",
    setting = "status_extras",
    label = "pause_menu.status_extras",
    default = true,
  },
  -- Canonical stages: the chart names the stages as the series does and draws a Mega box (off by
  -- default; off leaves the original's names and boxes).
  {
    id = "canonical-stages",
    setting = "canonical_stages",
    label = "pause_menu.canonical_stages",
    default = false,
  },
  -- Walk with: the keyboard and the pad (keys, the default) or the mouse, never both.
  {
    id = "walk-control",
    setting = "walk_control",
    label = "pause_menu.walk_control",
    values = { "keys", "mouse" },
    texts = "pause_menu.walk_control",
    default = "keys",
  },
  -- Long names: the naming box takes names of up to twelve letters (on by default; off keeps the
  -- original's six).
  { id = "long-names", setting = "long_names", label = "pause_menu.long_names", default = true },
  -- Recruit bar: while a job or a match decides whether someone joins the city, a bar shows how
  -- close the player is: the canyon shop's takings and customers, the stones game's points and
  -- throws (on by default; off leaves the original's screen).
  {
    id = "shop-job-bar",
    setting = "shop_job_bar",
    label = "pause_menu.shop_job_bar",
    default = true,
  },
  -- Continuous music: a map whose song is the one playing lets it go on (on by default; off plays
  -- every map's song from its start, as the original).
  {
    id = "continuous-music",
    setting = "continuous_music",
    label = "pause_menu.continuous_music",
    default = true,
  },
}

-- The option's current value: what the game published, or its default.
function game_options.value(option)
  local value = ui.setting(option.setting)
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
  if option.values ~= nil then
    return t(option.texts .. "." .. tostring(value))
  end
  return t(value and "common.yes" or "common.no")
end

-- Asks the game for the option's next value, step entries away (wrapping); a toggle flips.
function game_options.change(option, step)
  local value = game_options.value(option)
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
