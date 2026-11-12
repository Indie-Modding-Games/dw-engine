-- The UI API's player (docs/modding/ui-api.md, namespace player): the player's profile as the game
-- publishes it while the pause menu shows it.
--
-- Every function returns a fresh table; changing it changes nothing in the game.

player = {}

local function whole(name)
  local value = ui.setting(name)
  if type(value) ~= "number" then
    return 0
  end
  return math.floor(value)
end

-- A number the game publishes, or nil while it publishes none.
local function maybe(name)
  local value = ui.setting(name)
  return type(value) == "number" and value or nil
end

-- A list of whole numbers the game publishes as text ("1,0,2"), from 0.
local function numbers(name)
  local list = {}
  local text = ui.setting(name)
  local position = 0
  for value in string.gmatch(type(text) == "string" and text or "", "%d+") do
    list[position] = tonumber(value)
    position = position + 1
  end
  return list
end

-- The player's name, level (the player's own), bits, the seconds played, the partners raised and
-- the medals won (each nil while the game publishes none), and, as lists from 0, the items held and
-- the copies of each card.
function player.profile()
  local name = ui.setting("player.name")
  return {
    name = type(name) == "string" and name or "",
    level = maybe("player.level"),
    bits = maybe("player.bits"),
    seconds = maybe("player.seconds"),
    raised = maybe("player.raised"),
    medals = maybe("player.medals"),
    items = numbers("player.items.held"),
    cards = numbers("player.cards.copies"),
  }
end

-- The medals, as many as the game publishes (not a fixed number): each {held, name_key}.
function player.medals()
  local list = {}
  local held = ui.setting("player.medals.held")
  local index = 0
  for flag in string.gmatch(type(held) == "string" and held or "", "%d+") do
    local name = ui.setting("player.medal." .. index .. ".name")
    list[index + 1] = { held = flag ~= "0", name_key = type(name) == "string" and name or "" }
    index = index + 1
  end
  return list
end

-- The town's state: the residents there are, its prosperity, and which residents were recruited
-- and who they are (the game's lists, as text).
function player.city()
  local recruited = ui.setting("player.city.recruited")
  local species = ui.setting("player.city.species")
  return {
    recruited = type(recruited) == "string" and recruited or "",
    species = type(species) == "string" and species or "",
    total = whole("player.city.total"),
    prosperity = whole("player.city.prosperity"),
  }
end
