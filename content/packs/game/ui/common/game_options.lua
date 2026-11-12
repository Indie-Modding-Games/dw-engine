-- The game's own options as rows of the options page (docs/modulos/apps/game.md, «Opciones en
-- partida»): the UI API's options (options.list(), ui/api/options.lua) in their order, each with
-- the element id of its row while its tab is shown (also what routes select with menu_select). The
-- page fills a free row of each option's tab, so no document needs a row of its own.
--
-- Each entry: id, setting (the option's name), label, default, values, texts, range, tab, help and
-- original, as the descriptors of ui/api/options.lua.
--
-- The menu never changes a setting itself: it asks with options.set and shows what the game
-- publishes back.

-- The rows whose id is not the option's name with dashes.
local row_ids = { minimap = "show-minimap" }

game_options = {}
for _, option in ipairs(options.list()) do
  game_options[#game_options + 1] = {
    id = row_ids[option.name] or string.gsub(option.name, "_", "-"),
    setting = option.name,
    label = option.label_key,
    default = option.default,
    values = option.values,
    texts = option.texts,
    range = option.min ~= nil and { option.min, option.max } or nil,
    tab = option.tab,
    help = option.help_key,
    original = option.original,
  }
end

-- The value an entry a script adds to the list itself (not an option of the UI API) has now, with
-- the same rules as options.get: what the game published, or the entry's default.
local function own_value(option)
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

-- The option's current value: what the game published, or its default.
function game_options.value(option)
  local value = options.get(option.setting)
  if value == nil then
    return own_value(option)
  end
  return value
end

-- Asks the game for a value: through the UI API, or straight for an entry a script added.
local function ask(option, value)
  if not options.set(option.setting, value) and options.get(option.setting) == nil then
    ui.send(option.setting, value)
  end
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
      ask(option, next)
    end
    return
  end
  if option.values == nil then
    ask(option, not value)
    return
  end
  local index = 1
  for at, known in ipairs(option.values) do
    if known == value then
      index = at
    end
  end
  ask(option, option.values[((index - 1 + step) % #option.values) + 1])
end

-- How the page draws the option: "toggle" (on or off), "list" (values) or "range" (a number).
function game_options.kind(option)
  if option.range ~= nil then
    return "range"
  end
  return option.values ~= nil and "list" or "toggle"
end
