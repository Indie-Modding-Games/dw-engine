-- The numbers over the partner on the field (apps/game/src/field_numbers.hpp). The game publishes
-- the ones the last field frame drew in field.numbers, seven values each as the battle's
-- (packages/screens: damage_numbers.hpp): the anchor in percent of the window, the first digit's
-- offset from it in the original's pixels, the value, the colour and the icon; and bumps
-- field.numbers.revision. Each digit is 8 pixels square, 7 apart (utils2.c: drawEntityText).

local max_numbers = 8
local number_digits = 4
local number_values = 7
local number_icons = { "hp", "mp", "offense", "defense", "speed" }

-- The properties last set on each element, by "id property", so an unchanged one is not set again.
local applied = {}

local function set_property(document, id, property, value)
  local key = id .. " " .. property
  if applied[key] ~= value then
    applied[key] = value
    document:get(id):set_property(property, value)
  end
end

local function set_class(document, id, class, on)
  local key = id .. " ." .. class
  if applied[key] ~= on then
    applied[key] = on
    document:get(id):set_class(class, on)
  end
end

local function draw(document)
  local values = {}
  for value in string.gmatch(ui.setting("field.numbers") or "", "%S+") do
    values[#values + 1] = tonumber(value)
  end
  local count = math.min(max_numbers, math.floor(#values / number_values))
  for index = 0, max_numbers - 1 do
    local id = "dn-" .. index
    set_class(document, id, "hidden", index >= count)
    if index < count then
      local base = index * number_values
      set_property(document, id, "left", values[base + 1] .. "%")
      set_property(document, id, "top", values[base + 2] .. "%")
      set_property(
        document,
        id,
        "transform",
        "translate(" .. (values[base + 3] * 3) .. "dp, " .. (values[base + 4] * 3) .. "dp)"
      )
      set_class(document, id, "poison", values[base + 6] == 12)
      set_class(document, id, "gain", values[base + 6] == 11)
      local icon = number_icons[math.floor(values[base + 7])]
      local icon_id = "di-" .. index
      set_class(document, icon_id, "hidden", icon == nil)
      for _, name in ipairs(number_icons) do
        set_class(document, icon_id, "i-" .. name, icon == name)
      end
      if icon ~= nil and applied[icon_id .. " text"] ~= icon then
        applied[icon_id .. " text"] = icon
        document:get(icon_id):set_text(t("battle.number_icon." .. icon))
      end
      local digits = tostring(math.floor(values[base + 5]))
      for place = 0, number_digits - 1 do
        local digit_id = "dd-" .. index .. "-" .. place
        local digit = string.sub(digits, place + 1, place + 1)
        set_class(document, digit_id, "hidden", digit == "")
        for glyph = 0, 9 do
          set_class(document, digit_id, "d-" .. glyph, digit == tostring(glyph))
        end
        if digit ~= "" and applied[digit_id .. " text"] ~= digit then
          applied[digit_id .. " text"] = digit
          document:get(digit_id):set_text(digit)
        end
      end
    end
  end
end

function on_show(document)
  -- A document loaded again (hot reload) starts with none of what was set.
  applied = {}
  draw(document)
end

-- The icons' words are in the player's language: write them again.
function on_language(document)
  applied = {}
  draw(document)
end

function on_setting(document, name)
  if name == "field.numbers.revision" then
    draw(document)
  end
end
