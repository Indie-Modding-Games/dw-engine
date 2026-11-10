-- A bar's value as the original's HUD digits (vs_hud.c: VS_renderNumber, battle_hud.c:
-- BTL_renderNumber, both called with 4 places): right-aligned, no leading zeros
-- (convertValueToDigits). The versus HUD and the partner's bars in battle load this script.
--
-- A document gives each place an element `<prefix><place>` (place 0 on the left) with the classes
-- `hud-digit place-<place> hidden`; hud_digits.draw sets `hidden` on an empty place and `d-0` to
-- `d-9` on the others, through the document's own set_class(document, id, class, on), which
-- skips what is already set. A skin with the original's digit sprites shows them
-- (skins/original/theme.rcss); common/base.rcss hides them for the others.

hud_digits = { places = 4 }

function hud_digits.draw(document, prefix, value, set_class)
  local largest = math.tointeger(10 ^ hud_digits.places) - 1
  local shown = math.max(0, math.min(math.floor(value), largest))
  local digits = string.format("%" .. hud_digits.places .. "d", shown)
  for place = 0, hud_digits.places - 1 do
    local id = prefix .. place
    local glyph = string.sub(digits, place + 1, place + 1)
    set_class(document, id, "hidden", glyph == " ")
    for digit = 0, 9 do
      set_class(document, id, "d-" .. digit, glyph == tostring(digit))
    end
  end
end
