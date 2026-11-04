-- The stones game's HUD. The game publishes, as numbers, curling_hud.score_<team>_tens and _units
-- (the glyphs of each score, team 0 the player's), curling_hud.second (1 against the second
-- opponent), curling_hud.ready (1 while the start sign shows), curling_hud.gauge_team (-1 hidden,
-- else the thrower) and curling_hud.chevrons, curling_hud.hand_team (-1 hidden) and
-- curling_hud.hand_row, curling_hud.prompt (-1 closed, 0 play, 1 rules highlighted) and, as text
-- keys, the open hint page's lines curling_hud.hint_name and curling_hud.hint_1 to 3 (empty when
-- closed). It shows this document without taking the input. Positions are the original's pixels
-- from the screen's centre, 3dp each (structure.rcss).

local dp = 3
local texels = "/map/frzl17/texture4/000?palette="
local hint_lines = { "hint_name", "hint_1", "hint_2", "hint_3" }
-- KAR_D_8005B4A0: each glyph's texels (0 to 9, then the minus sign).
local glyphs = {
  [0] = { 0x40, 0x18 },
  { 0x50, 0x18 },
  { 0x60, 0x18 },
  { 0x40, 0x28 },
  { 0x50, 0x28 },
  { 0x60, 0x28 },
  { 0x48, 0x38 },
  { 0x58, 0x38 },
  { 0x48, 0x48 },
  { 0x58, 0x48 },
  { 0x00, 0x40 },
}
-- KAR_renderPowerMeter: the frame's x by thrower and its palette (CLUT x 0x70 or 0x80).
local gauge_x = { [0] = -0x32, [1] = 0x32 }
local gauge_palette = { [0] = 7, [1] = 8 }
-- KAR_renderStoneCursor: the hand's x by rack, its y from the row.
local hand_x = { [0] = -0x78, [1] = 0x64 }

local function number(name, fallback)
  return math.floor(ui.setting("curling_hud." .. name) or fallback)
end

local function place(element, x, y)
  element:set_property("margin-left", string.format("%ddp", x * dp))
  element:set_property("margin-top", string.format("%ddp", y * dp))
end

-- The first opponent's name: the speaker of its hint pages (kar.c: KAR_D_8005AF58).
local opponent_name = "kar.penguinmon_dialogue.0000"

local function draw_recruit(document)
  local shown = ui.setting("curling_recruit.shown") == true
  document:get("recruit"):set_class("hidden", not shown)
  if not shown then
    return
  end
  local player = math.floor(ui.setting("curling_recruit.player") or 0)
  local opponent = math.floor(ui.setting("curling_recruit.opponent") or 0)
  local name = t(opponent_name)
  document:get("recruit-score"):set_text(t("curling.recruit.score", {
    player = player,
    opponent = opponent,
    name = name,
  }))
  document:get("recruit-throws"):set_text(t("curling.recruit.throws", {
    player = math.floor(ui.setting("curling_recruit.player_throws") or 0),
    opponent = math.floor(ui.setting("curling_recruit.opponent_throws") or 0),
  }))
  local standing
  if player > opponent then
    standing = t("curling.recruit.ahead", { name = name })
  elseif player < opponent then
    standing = t("curling.recruit.behind", { gap = opponent - player })
  else
    standing = t("curling.recruit.level")
  end
  local recruit = document:get("recruit")
  recruit:set_class("ahead", player > opponent)
  document:get("recruit-standing"):set_text(standing)
  local perfect = ui.setting("curling_recruit.perfect") == true
  document:get("recruit-perfect"):set_text(perfect and t("curling.recruit.perfect") or "")
end

local function refresh(document)
  for team = 0, 1 do
    for _, digit in ipairs({ "tens", "units" }) do
      local glyph = glyphs[number(string.format("score_%d_%s", team, digit), 0)] or glyphs[0]
      document
        :get(string.format("score-%d-%s", team, digit))
        :set_attribute("rect", string.format("%d %d 16 16", glyph[1], glyph[2]))
    end
  end
  local second = number("second", 0) == 1
  document:get("opponent-face"):set_class("hidden", second)
  document:get("opponent-face-2"):set_class("hidden", not second)
  document:get("ready"):set_class("hidden", number("ready", 0) ~= 1)

  local thrower = number("gauge_team", -1)
  document:get("gauge"):set_class("hidden", thrower < 0)
  if thrower >= 0 then
    local x = gauge_x[thrower] or gauge_x[0]
    local frame = document:get("gauge-frame")
    frame:set_attribute("src", texels .. (gauge_palette[thrower] or gauge_palette[0]))
    frame:set_property("margin-left", string.format("%ddp", x * dp))
    local chevrons = number("chevrons", 0)
    for i = 1, 10 do
      local chevron = document:get("chevron-" .. i)
      chevron:set_class("hidden", i > chevrons)
      chevron:set_property("margin-left", string.format("%ddp", (x + i * 3 - 3) * dp))
    end
  end

  local rack = number("hand_team", -1)
  local hand = document:get("hand")
  hand:set_class("hidden", rack < 0)
  if rack >= 0 then
    place(hand, hand_x[rack] or hand_x[0], number("hand_row", 0) * 30 - 45)
  end

  local hinted = (ui.setting("curling_hud.hint_name") or "") ~= ""
  for _, id in ipairs(hint_lines) do
    local key = ui.setting("curling_hud." .. id) or ""
    -- The original's help names its pad's buttons: they show as the device in use's
    -- (set_prompt_text with original texts), the translation's {prompt:...} markers too.
    document:get(id):set_prompt_text(key ~= "" and t(key) or "", true)
  end
  local prompt = number("prompt", -1)
  document:get("rows"):set_class("hidden", not hinted)
  document:get("question"):set_class("hidden", prompt < 0)
  for option, id in ipairs({ "play", "rules" }) do
    local selected = prompt == option - 1
    local row = document:get(id)
    row:set_class("selected", selected)
    row:set_class("barred", selected)
  end
  document:get("box"):set_class("hidden", not hinted and prompt < 0)
  draw_recruit(document)
end

function on_show(document)
  refresh(document)
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
end

function on_device(document)
  refresh(document)
end

function on_action(_document, _action)
  return false
end
