-- The title screen's menu, with no game behind it. Like the pause menu it never changes a setting
-- itself: it sends the language, text_font, ui_skin, chibi_style, autosave, minimap, minimap_size,
-- shadows, enhanced_lighting, vivid_colors, hd_scenery, fullscreen and ui_scale commands and shows
-- what the game reports back (ui.setting). The skin entry is common/ui_skin_choice.lua, shared with
-- the pause menu.
--
-- Two layouts of the same entries, as the skin marks #panel (document_classes):
-- - The modern one: play (a new game), continue (greyed and skipped unless the game publishes
--   save.can_continue), the language, the text font, the skin, the chibi style, the autosave,
--   quit.
-- - The original look's (class title-original), the original's title screen (main.c: main):
--   - "boot": the "Press Start" screen (runLandingScreen), when the game says so (title.boot).
--     It fades in from black, its line blinks, and Start fades it out (40 frames each).
--   - "menu": the original's menu (main_menu.c: renderInitialMenu), faded in from black
--     (runMainMenu): new game, continue, delete (erase) and battle mode, with continue and delete
--     greyed without a save, as the original greys them without a memory card; the cursor still
--     stops on them and confirming them does nothing (tickMainMenu, case 0). Then options, this
--     game's addition. The focused row wears the original's cursor, a one-pixel outline round it
--     (renderMenuSelector), which the skin draws.
--   - "options": this game's settings that work on the title screen, a name and its value per
--     row, in the original's boxes. Free exploration is set from the pause menu, during a game, and
--     vsync cannot change while the game runs, so neither is here.
--   - "language": a box with one row per language the packs offer, each named in its own language.
--     It opens from options, and before the "Press Start" screen each time the title starts there
--     (title.language_first), as a remaster's: an addition. There one confirm picks the language
--     and goes on, with a fade, to the "Press Start" screen.
--   - "battle": the original's battle mode question (renderConfirmVSSlot), on no. Battle mode is
--     not made yet: both answers come back to the menu.
--   The cursor does not wrap: it stops at the first and the last entry (tickMenuInput), and each
--   view remembers its entry when it comes back, as the original's cursors keep their place.
--   Cancel goes back one view. The sounds are the UI's cues, the original's (ui/ui_sounds.yaml:
--   tickMenuInput plays 2 to move, 3 to confirm and 4 to cancel).

local view = "menu"
-- The entry each view had focused last.
local remembered = { menu = nil, options = "language" }
-- Where the language box goes back to: options, or on to the "Press Start" screen (boot).
local language_return = "options"
-- The layout seen last, to lay the document out again when a skin switch changes it.
local was_original = nil
-- A focus on CONTINUE GAME instead of NEW GAME when there is a save: an addition (the original
-- starts on NEW GAME, its cursor's first place).
local FocusContinueWhenSaved = true

-- 60 frames a second, the original's.
local Frame = 1 / 60
-- runLandingScreen and runMainMenu fade 40 frames (0x28) from and to black.
local FadeSeconds = 40 * Frame
-- renderPressStartToContinue: the line shows for 30 of every 60 frames; after Start, every other
-- frame.
local BlinkSeconds = 60 * Frame
-- runLandingScreen goes back to the boot movie after 600 frames (0x258) without Start; the game
-- counts that itself, only while this says the screen waits for Start (title_landing). The menu and
-- the language box never go back to the movie, as the original's main menu.
local landing = false

local function set_landing(now)
  if now ~= landing then
    landing = now
    ui.send("title_landing", now)
  end
end

-- The fade: how dark the screen is (1 black), and where it goes.
local darkness = 0.0
local fade_target = 0.0
local blink = 0.0
local leaving = false
local pending_view = nil

local entries = {
  menu = { "play", "continue", "erase", "battle", "options" },
  options = {
    "language",
    "text-font",
    "ui-skin",
    "chibi-style",
    "autosave",
    "minimap",
    "minimap-size",
    "shadows",
    "enhanced-lighting",
    "vivid-colors",
    "hd-scenery",
    "weather",
    "seasons",
    "display-mode",
    "ui-scale",
    "quit",
  },
  battle = { "battle-yes", "battle-no" },
  language = {
    "language-1",
    "language-2",
    "language-3",
    "language-4",
    "language-5",
    "language-6",
    "language-7",
    "language-8",
    "language-done",
  },
}

-- The round minimap: on (the default, also for the old auto) or off.
local minimap_modes = { "on", "off" }
local minimap_sizes = { "small", "medium", "large" }
-- The chibi style (game_options.lua): off or the models from outside the original game (the
-- default; an old "all" shows as that).
local chibi_modes = { "off", "external" }
-- The field's graphics improvements (docs/architecture/render.md): sun (the default), round or off.
local shadow_modes = { "sun", "round", "off" }
local ui_scales = { 0.75, 0.9, 1.0, 1.15, 1.3, 1.5, 1.75, 2.0 }

local function original(document)
  return document:get("panel"):has_class("title-original")
end

local function index_of(list, value)
  for index, item in ipairs(list) do
    if item == value then
      return index
    end
  end
  return nil
end

local function cycle(list, value, step, default)
  local index = index_of(list, value) or default
  return list[((index - 1 + step) % #list) + 1]
end

-- The language after the current one in the list the packs offer, step entries away, wrapping
-- around.
local function next_language(step)
  return cycle(ui.languages(), ui.setting("language"), step, 1)
end

-- The nearest UI scale step to a value, so a scale set elsewhere still cycles from the right place.
local function nearest_scale(value)
  local best = 3
  for index, scale in ipairs(ui_scales) do
    if math.abs(scale - value) < math.abs(ui_scales[best] - value) then
      best = index
    end
  end
  return best
end

-- A language's name in that language (language.native.<code>, the same in every language's
-- texts), else its code.
local function native_name(code)
  local key = "language.native." .. code
  local name = t(key)
  return (name ~= "" and name ~= key) and name or string.upper(code)
end

-- Capitals of a text, the accented letters of the languages offered too (string.upper only knows
-- ASCII).
local Capitals = {
  ["á"] = "Á",
  ["é"] = "É",
  ["í"] = "Í",
  ["ó"] = "Ó",
  ["ú"] = "Ú",
  ["ü"] = "Ü",
  ["ñ"] = "Ñ",
}

local function upper(text)
  return (string.upper(text):gsub("\195[\161\169\173\179\186\188\177]", Capitals))
end

local function refresh(document)
  local orig = original(document)
  local saved = ui.setting("save.can_continue") == true
  local own = orig and "main_menu.original." or "main_menu."
  document:get("play"):set_text(t(own .. "new_game"))
  document:get("continue"):set_text(t(own .. "continue"))
  -- Modern: greyed and skipped (structure.rcss). Original: greyed, but the cursor stops on it.
  document:get("continue"):set_class("unavailable", not saved and not orig)
  document:get("continue"):set_class("dimmed", not saved and orig)
  document:get("erase"):set_class("dimmed", not saved)
  -- The original's values are in capitals, as its texts.
  local function value(id, text)
    document:get(id .. "-value"):set_text(orig and upper(text) or text)
  end
  value("language", t("language.name"))
  local font = ui.setting("text_font") == "original" and "original" or "modern"
  value("text-font", t("main_menu.text_font." .. font))
  value("ui-skin", skin_choice.name())
  local chibi = index_of(chibi_modes, ui.setting("chibi_style")) and ui.setting("chibi_style")
    or "external"
  value("chibi-style", t("pause_menu.chibi_style." .. chibi))
  -- The autosave (game_options.lua), on unless the game says it is off.
  value("autosave", t(ui.setting("autosave") == false and "common.no" or "common.yes"))
  local mode = index_of(minimap_modes, ui.setting("minimap")) and ui.setting("minimap") or "on"
  value(
    "minimap",
    t((orig and "main_menu.original.option.minimap." or "pause_menu.minimap_mode.") .. mode)
  )
  local size = index_of(minimap_sizes, ui.setting("minimap_size")) and ui.setting("minimap_size")
    or "medium"
  value("minimap-size", t("pause_menu.minimap_size." .. size))
  local shadows = index_of(shadow_modes, ui.setting("shadows")) and ui.setting("shadows") or "sun"
  value("shadows", t("pause_menu.shadows." .. shadows))
  local function on_off(setting)
    return t(ui.setting(setting) == false and "common.no" or "common.yes")
  end
  value("enhanced-lighting", on_off("enhanced_lighting"))
  value("vivid-colors", on_off("vivid_colors"))
  -- HD scenery: on unless the game says off.
  value(
    "hd-scenery",
    t("main_menu.original.option.minimap." .. (ui.setting("hd_scenery") == false and "off" or "on"))
  )
  -- The weather, on unless turned off (the game publishes it; an engine improvement).
  local weather = ui.setting("weather") ~= false
  value(
    "weather",
    t(
      (orig and "main_menu.original.option.minimap." or "pause_menu.minimap_mode.")
        .. (weather and "on" or "off")
    )
  )
  -- The seasons, on unless turned off (the game publishes it; an engine improvement).
  local seasons = ui.setting("seasons") ~= false
  value(
    "seasons",
    t(
      (orig and "main_menu.original.option.minimap." or "pause_menu.minimap_mode.")
        .. (seasons and "on" or "off")
    )
  )
  value(
    "display-mode",
    t(
      ui.setting("fullscreen") and "pause_menu.display_mode.fullscreen"
        or "pause_menu.display_mode.window"
    )
  )
  local percent = math.floor((ui.setting("ui_scale") or 1.0) * 100 + 0.5)
  value("ui-scale", t("pause_menu.ui_scale.value", { percent = percent }))
  local languages = ui.languages()
  for row = 1, #entries.language - 1 do
    local button = document:get("language-" .. row)
    local code = languages[row]
    button:set_class("hidden", code == nil or view ~= "language")
    document:get("language-name-" .. row):set_text(code ~= nil and upper(native_name(code)) or "")
    -- The language's flag (language.flag.<code>, an image's asset id), when it has one.
    local flag = code ~= nil and t("language.flag." .. code) or ""
    local has_flag = flag ~= "" and flag ~= "language.flag." .. code
    local image = document:get("language-flag-" .. row)
    image:set_class("hidden", not has_flag)
    if has_flag then
      image:set_attribute("src", "/" .. flag)
    end
  end
end

-- Whether the pointer moved last (the UI tells the keyboard and the mouse apart only as one
-- device).
local pointer = false

-- The "Press Start" line names what goes on with the device in use, in the prompts' names: the
-- confirm key on a keyboard, Start on a gamepad, a click with the mouse. The original names its
-- Start button; this is the PC's.
local function refresh_press(document)
  -- The key or button is its glyph: the line is written in capitals around it.
  local text
  if ui.device() == "gamepad" then
    text = upper(t("main_menu.press_key", { key = "@@" })):gsub("@@", "{prompt:pause}")
  elseif pointer then
    text = upper(t("main_menu.press_click"))
  else
    text = upper(t("main_menu.press_key", { key = "@@" })):gsub("@@", "{prompt:confirm}")
  end
  document:get("press-start-text"):set_prompt_text(text)
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-cancel"):set_prompt("cancel")
  refresh_press(document)
end

-- The pointer moved over the title screen: the "Press Start" line asks for a click.
function pointer_moved(document)
  if not pointer then
    pointer = true
    refresh_press(document)
  end
end

-- Shows what the layout and the view show, and hides the rest.
local function lay_out(document)
  local orig = original(document)
  was_original = orig
  local shown = orig and view or "menu"
  local function show(id, visible)
    document:get(id):set_class("hidden", not visible)
  end
  show("press-start", shown == "boot")
  show("panel", shown == "menu" or shown == "options")
  show("play", shown == "menu")
  show("continue", shown == "menu")
  for _, id in ipairs({ "erase", "battle", "options" }) do
    show(id, orig and shown == "menu")
  end
  for _, id in ipairs({
    "language",
    "text-font",
    "ui-skin",
    "chibi-style",
    "autosave",
    "weather",
    "seasons",
    "quit",
  }) do
    show(id, not orig or shown == "options")
  end
  for _, id in ipairs({
    "minimap",
    "minimap-size",
    "shadows",
    "enhanced-lighting",
    "vivid-colors",
    "hd-scenery",
    "display-mode",
    "ui-scale",
  }) do
    show(id, orig and shown == "options")
  end
  show("prompt-back", orig and shown ~= "menu" and shown ~= "boot")
  show("language-box", shown == "language")
  if shown == "language" then
    -- The box in the middle of the screen: its heading and START (180dp with its padding) and a
    -- row of 60dp per language, 420dp at least (the skin's min-height).
    local height = math.max(420, 180 + 60 * math.min(#ui.languages(), #entries.language - 1))
    document
      :get("language-box")
      :set_property("top", string.format("%ddp", math.floor((720 - height) / 2)))
  end
  show("battle-box", shown == "battle")
  document:get("scrim"):set_class("boot", shown == "boot")
  document:get("scrim"):set_class("view-options", shown == "options")
  document:get("press-start-text"):set_class("blink-off", false)
  refresh(document)
end

local function set_fade(document, value)
  darkness = math.max(0.0, math.min(1.0, value))
  local fade = document:get("fade")
  fade:set_class("hidden", darkness <= 0.0)
  fade:set_property("opacity", string.format("%.3f", darkness))
end

-- The menu's first entry: continue when there is a save to continue (an addition), else new game.
local function first_entry()
  if FocusContinueWhenSaved and ui.setting("save.can_continue") == true then
    return "continue"
  end
  return "play"
end

local function set_view(document, name, focus)
  view = name
  leaving = false
  set_landing(name == "boot")
  blink = 0.0
  lay_out(document)
  document:get(focus):focus()
end

-- Opens a view of the original layout on the entry it had last.
local function enter(document, name)
  if name == "menu" then
    set_view(document, "menu", remembered.menu or first_entry())
  elseif name == "boot" then
    set_view(document, "boot", "press-start")
  elseif name == "battle" then
    -- The question starts on no each time, as the original's.
    set_view(document, "battle", "battle-no")
  elseif name == "language" then
    local current = index_of(ui.languages(), ui.setting("language")) or 1
    set_view(document, "language", "language-" .. current)
  else
    set_view(document, name, remembered[name])
  end
end

-- The menu after a fade from black, as runMainMenu starts.
local function fade_into(document, name)
  set_fade(document, 1.0)
  fade_target = 0.0
  enter(document, name)
end

function leave_boot(_document)
  if view == "boot" and not leaving then
    -- Start ends the countdown (runLandingScreen: goto fade).
    set_landing(false)
    leaving = true
    fade_target = 1.0
    pending_view = "menu"
  end
end

function open_view(document, name)
  remembered.menu = name
  enter(document, name)
end

function close_view(document)
  for id_view, ids in pairs(entries) do
    if id_view == view then
      for _, id in ipairs(ids) do
        if document:get(id):has_focus() then
          remembered[view] = id
        end
      end
    end
  end
  enter(document, "menu")
end

function play()
  remembered.menu = "play"
  ui.hide(document_name)
  ui.send("play")
end

function continue_game()
  remembered.menu = "continue"
  if ui.setting("save.can_continue") == true then
    ui.hide(document_name)
    ui.send("continue")
  end
end

function erase_game()
  remembered.menu = "erase"
  if ui.setting("save.can_continue") == true then
    ui.hide(document_name)
    ui.send("erase")
  end
end

function change_language(step)
  ui.send("language", next_language(step))
end

function change_text_font()
  ui.send("text_font", ui.setting("text_font") == "original" and "modern" or "original")
end

-- What each option does when confirmed or moved left and right.
local options = {
  language = function(step)
    change_language(step)
  end,
  ["text-font"] = function(_step)
    change_text_font()
  end,
  ["ui-skin"] = function(step)
    skin_choice.change(step)
  end,
  ["chibi-style"] = function(step)
    ui.send("chibi_style", cycle(chibi_modes, ui.setting("chibi_style"), step, 2))
  end,
  autosave = function(_step)
    ui.send("autosave", ui.setting("autosave") == false)
  end,
  minimap = function(step)
    ui.send("minimap", cycle(minimap_modes, ui.setting("minimap"), step, 1))
  end,
  ["minimap-size"] = function(step)
    ui.send("minimap_size", cycle(minimap_sizes, ui.setting("minimap_size"), step, 2))
  end,
  shadows = function(step)
    ui.send("shadows", cycle(shadow_modes, ui.setting("shadows"), step, 1))
  end,
  ["enhanced-lighting"] = function(_step)
    ui.send("enhanced_lighting", ui.setting("enhanced_lighting") == false)
  end,
  ["vivid-colors"] = function(_step)
    ui.send("vivid_colors", ui.setting("vivid_colors") == false)
  end,
  ["hd-scenery"] = function(_step)
    ui.send("hd_scenery", ui.setting("hd_scenery") == false)
  end,
  weather = function(_step)
    ui.send("weather", ui.setting("weather") == false)
  end,
  seasons = function(_step)
    ui.send("seasons", ui.setting("seasons") == false)
  end,
  ["display-mode"] = function(_step)
    ui.send("fullscreen", not ui.setting("fullscreen"))
  end,
  ["ui-scale"] = function(step)
    local index = nearest_scale(ui.setting("ui_scale") or 1.0)
    ui.send("ui_scale", ui_scales[math.max(1, math.min(#ui_scales, index + step))])
  end,
}

function change_option(id, step)
  options[id](step)
end

-- The language entry: the language box in the original layout, the next language in the modern.
function language_entry(document)
  if original(document) then
    remembered.options = "language"
    language_return = "options"
    enter(document, "language")
  else
    change_language(1)
  end
end

-- A row of the language box: the language at once, then the cursor on the box's last row; before
-- the "Press Start" screen, on to it.
function pick_language(document, row)
  local code = ui.languages()[row]
  if code ~= nil and not leaving then
    ui.send("language", code)
    if language_return == "boot" then
      close_language(document)
    else
      document:get("language-done"):focus()
    end
  end
end

function close_language(document)
  if language_return == "boot" then
    -- Out to black and into the "Press Start" screen; a second confirm in the fade does nothing.
    if not leaving then
      leaving = true
      fade_target = 1.0
      pending_view = "boot"
    end
    return
  end
  enter(document, language_return)
end

function on_show(document)
  -- The game forgets the screen whenever it shows or hides the title (TitleScreen::showMenu).
  landing = false
  refresh_prompts(document)
  was_original = original(document)
  if original(document) and ui.setting("title.boot") == true then
    remembered.menu = nil
    set_fade(document, 1.0)
    fade_target = 0.0
    if ui.setting("title.language_first") == true then
      language_return = "boot"
      enter(document, "language")
    else
      set_view(document, "boot", "press-start")
    end
  elseif original(document) then
    -- Back from a slot list, on the entry that opened it; from a game, on the first entry.
    set_fade(document, 0.0)
    fade_target = 0.0
    enter(document, "menu")
  else
    set_fade(document, 0.0)
    set_view(document, "menu", "play")
  end
end

function on_setting(document, _name)
  refresh(document)
end

function on_language(document)
  refresh(document)
  refresh_prompts(document)
end

function on_device(document)
  pointer = false
  refresh_prompts(document)
end

-- A skin switch can change the layout: the entry focused stays focused, in the options of the
-- original layout or the panel of the modern one.
local function follow_layout(document)
  local focused = nil
  for _, id in ipairs(entries.options) do
    if document:get(id):has_focus() then
      focused = id
    end
  end
  set_fade(document, 0.0)
  if original(document) and focused ~= nil then
    remembered.menu = "options"
    set_view(document, "options", focused)
  elseif original(document) then
    enter(document, "menu")
  else
    local modern = focused ~= nil and document:get(focused):has_class("hidden") == false
    set_view(document, "menu", modern and focused or "play")
  end
end

function on_tick(document, seconds)
  if original(document) ~= was_original then
    follow_layout(document)
  end
  if not original(document) then
    return
  end
  if darkness ~= fade_target then
    local step = seconds / FadeSeconds
    set_fade(
      document,
      darkness < fade_target and math.min(fade_target, darkness + step)
        or math.max(fade_target, darkness - step)
    )
    if leaving and darkness >= 1.0 then
      fade_into(document, pending_view or "menu")
    end
  end
  if view == "boot" then
    local text = document:get("press-start-text")
    if leaving then
      text:set_class("blink-off", not text:has_class("blink-off"))
    else
      blink = (blink + seconds) % BlinkSeconds
      text:set_class("blink-off", blink >= BlinkSeconds / 2)
    end
  end
end

-- Up and down through the current view's entries, stopping at the ends (tickMenuInput). False when
-- the cursor did not move, so no move sounds.
local function step_entries(document, ids, step)
  local current = nil
  local shown = {}
  for _, id in ipairs(ids) do
    local element = document:get(id)
    if not element:has_class("hidden") then
      table.insert(shown, id)
      if element:has_focus() then
        current = #shown
      end
    end
  end
  if #shown == 0 then
    return false
  end
  local next = current == nil and 1 or math.max(1, math.min(#shown, current + step))
  if next == current then
    return false
  end
  document:get(shown[next]):focus()
  return true
end

-- Navigation steps arrive here first; returning true suppresses the default. In the modern layout
-- cancel and menu do nothing (the player leaves with play or quit) and up and down are the
-- structure's; in the original one cancel goes back a view, and up and down are here.
function on_action(document, action)
  if pointer then
    pointer = false
    refresh_press(document)
  end
  if original(document) then
    if view == "boot" then
      -- Start, confirm or a click goes on (runLandingScreen takes Start; confirm and the pointer
      -- are this game's), and nothing else does.
      if action == "menu" or action == "confirm" then
        leave_boot(document)
        return true
      end
      -- No entry to move to: nothing moves, so nothing sounds.
      return action == "cancel"
    end
    if action == "cancel" or action == "menu" then
      if view == "language" then
        close_language(document)
      elseif view ~= "menu" then
        close_view(document)
      end
      return true
    end
    if action == "up" or action == "down" then
      return step_entries(document, entries[view], action == "up" and -1 or 1)
    end
    if action == "confirm" and view == "menu" then
      local element = nil
      for _, id in ipairs(entries.menu) do
        if document:get(id):has_focus() then
          element = document:get(id)
        end
      end
      -- A greyed entry takes the confirm (its sound plays, as the original's) and does nothing.
      return element ~= nil and element:has_class("dimmed")
    end
  elseif action == "cancel" or action == "menu" then
    return true
  end
  if action == "left" or action == "right" then
    for id, change in pairs(options) do
      if document:get(id):has_focus() then
        change(action == "left" and -1 or 1)
        return true
      end
    end
  end
  return false
end
