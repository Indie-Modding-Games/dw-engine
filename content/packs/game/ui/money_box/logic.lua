-- The money box the story opens over its talk (script_common.c: createShopBitsBox, tickShopBitsBox,
-- renderShopBitsBox). The game (apps/game/src/money_box_screen.cpp) publishes money_box.bits_key
-- (the keeper's "Bits" line) and money_box.money (the money when the story wrote it), and counts
-- up money_box.revision; the box takes no input.

-- renderShopBitsBox: the line's word on the first row, the money right-aligned on the second.
local function refresh(document)
  local money = math.floor(tonumber(ui.setting("money_box.money")) or 0)
  local key = ui.setting("money_box.bits_key") or ""
  local bits = key ~= "" and t(key, { bits = money }) or tostring(money)
  local label, value = string.match(bits, "^(.-)%s*(%S+)$")
  document:get("bits-label"):set_text(label or "")
  document:get("bits-value"):set_text(value or bits)
end

function on_show(document)
  refresh(document)
end

function on_language(document)
  refresh(document)
end

function on_setting(document, name)
  if name == "money_box.revision" then
    refresh(document)
  end
end
