-- One slot of the bag, as the original's item list draws it (inventory.c: renderInventoryTop): the
-- item's 16 x 16 icon 3 pixels in from the row's edge and 1 down (utils2.c: renderItemSprite,
-- setItemTexture), its name in its item colour's ink and its count. The game publishes the bag as
-- the settings bag.slot.count and bag.slot.<index>.* (packages/screens: bag_view.hpp, itemIconOf),
-- for the field's bag and the battle's alike.
--
-- A document that lists the bag links this script and gives each slot an element `<id>` holding an
-- `<img id="<id>-icon">` and the spans `<id>-name` and `<id>-count` (the icon is optional).
-- bag_slot.draw fills them and sets the slot's classes: `empty`, `dimmed` (an item the bag never
-- uses, item colour 255), and `item-field`, `item-battle`, `item-shared` (item colour 0, 1, 2:
-- TEXT_COLORS 5, 6, 7 in the original), and `favorite` for the partner's favourite food while the
-- favorite_food option is on (an engine improvement; the skins draw it in gold with a heart).

bag_slot = {}

function bag_slot.setting(index, field, fallback)
  local value = ui.setting("bag.slot." .. index .. "." .. field)
  if value == nil then
    return fallback
  end
  return value
end

function bag_slot.count()
  return math.floor(ui.setting("bag.slot.count") or 0)
end

function bag_slot.filled(index)
  return index < bag_slot.count() and not bag_slot.setting(index, "empty", true)
end

-- The key's text, or "" for no key.
local function text(key)
  if key == nil or key == "" then
    return ""
  end
  return t(key)
end

-- A description's text: its {species} placeholder shows the name of the species whose name key
-- comes with it (an item a pack adds names its evolution's species so, not in its text).
function bag_slot.described(key, species_key)
  if key == nil or key == "" then
    return ""
  end
  if species_key == nil or species_key == "" then
    return t(key)
  end
  return t(key, { species = t(species_key) })
end

-- The focused slot's description, "" for an empty slot: the original draws nothing in the
-- description box then (inventory.c: renderInventoryBottom).
function bag_slot.description(index)
  if index == nil or not bag_slot.filled(index) then
    return ""
  end
  return bag_slot.described(
    bag_slot.setting(index, "description", ""),
    bag_slot.setting(index, "description_species", "")
  )
end

-- Fills the slot `id` with the bag's slot `index`; an empty slot shows `empty_text` (the original
-- draws nothing there).
function bag_slot.draw(document, id, index, empty_text)
  local slot = document:get(id)
  local filled = bag_slot.filled(index)
  local name = filled and text(bag_slot.setting(index, "name", "")) or (empty_text or "")
  document:get(id .. "-name"):set_text(name)
  document
    :get(id .. "-count")
    :set_text(filled and tostring(math.floor(bag_slot.setting(index, "count", 0))) or "")
  slot:set_class("empty", not filled)
  slot:set_class("dimmed", filled and bag_slot.setting(index, "dimmed", false))
  local color = bag_slot.setting(index, "item_color", 255)
  slot:set_class("item-field", filled and color == 0)
  slot:set_class("item-battle", filled and color == 1)
  slot:set_class("item-shared", filled and color == 2)
  slot:set_class(
    "favorite",
    filled and ui.setting("favorite_food") ~= false and bag_slot.setting(index, "favorite", false)
  )
  local icon = document:get(id .. "-icon")
  if icon ~= nil then
    local source = filled and bag_slot.setting(index, "icon", "") or ""
    icon:set_class("hidden", source == "")
    if source ~= "" then
      icon:set_attribute("src", source)
      icon:set_attribute("rect", bag_slot.setting(index, "icon_rect", ""))
    end
  end
end
