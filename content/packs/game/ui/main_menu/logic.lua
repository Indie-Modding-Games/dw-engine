-- The title screen's menu, with no game behind it. Its Options entry opens the pause menu's options
-- page over the title (docs/modulos/apps/game.md, «Opciones en partida»): the same tabs, rows and
-- help as during a game, which TitleScreen shows on title_options and closes with
-- title_options_back; this menu keeps the title's picture behind it and hides its own box
-- meanwhile ("options" view). The language box sends the language command and shows what the game
-- reports back (ui.setting).
--
-- Two layouts of the same entries, as the skin marks #panel (document_classes):
-- - The modern one: play (a new game), continue (greyed and skipped unless the game publishes
--   save.can_continue), online versus, options, quit.
-- - The original look's (class title-original), the original's title screen (main.c: main):
--   - "boot": the "Press Start" screen (runLandingScreen), when the game says so (title.boot).
--     It fades in from black, its line blinks, and Start fades it out (40 frames each).
--   - "menu": the original's menu (main_menu.c: renderInitialMenu), faded in from black
--     (runMainMenu): new game, continue, delete (erase) and battle mode, with continue and delete
--     greyed without a save, as the original greys them without a memory card; the cursor still
--     stops on them and confirming them does nothing (tickMainMenu, case 0). Then options, this
--     game's addition. The focused row wears the original's cursor, a one-pixel outline round it
--     (renderMenuSelector), which the skin draws.
--     Quit is the options page's (its game actions keep only quit over the title).
--   - "language": a box with one row per language the packs offer, each named in its own language,
--     before the "Press Start" screen each time the title starts there (title.language_first), as a
--     remaster's: an addition. One confirm picks the language and goes on, with a fade, to the
--     "Press Start" screen. The options page changes the language too.
--   - "battle": the original's battle mode question (renderConfirmVSSlot), on no. Yes opens the
--     online versus (the game's versus document, an addition: the original's battle mode is two
--     players on one console); no comes back to the menu.
--   The cursor does not wrap: it stops at the first and the last entry (tickMenuInput), and each
--   view remembers its entry when it comes back, as the original's cursors keep their place.
--   Cancel goes back one view. The sounds are the UI's cues, the original's (ui/ui_sounds.yaml:
--   tickMenuInput plays 2 to move, 3 to confirm and 4 to cancel).

local view = "menu"
-- The entry each view had focused last.
local remembered = { menu = nil }
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
  menu = { "play", "continue", "erase", "versus", "battle", "options", "quit" },
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

local function original(document)
  return document:get("panel"):has_class("title-original")
end

-- The boxes over the title's picture with the «widescreen pictures» option (title_picture.lua), at
-- the original's places across it: the menu, the language box, the battle mode question, and the
-- "Press Start" line pinned under TITLE256's logo, across the picture: at row 162 (the original's
-- 168 would put it against a 16:9 window's bottom edge, which shows rows 0 to 179).
local PictureBoxes = {
  { { id = "panel", x = 72 } },
  { { id = "language-box", x = 54 } },
  { { id = "battle-box", x = 60 } },
  { { id = "press-start-text", x = 0, row = 162, width = 320 } },
}

local function index_of(list, value)
  for index, item in ipairs(list) do
    if item == value then
      return index
    end
  end
  return nil
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
  document:get("options"):set_text(t(own .. "options"))
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
  refresh_press(document)
end

-- The pointer moved over the title screen: the "Press Start" line asks for a click.
function pointer_moved(document)
  if not pointer then
    pointer = true
    refresh_press(document)
  end
end

-- Shows what the layout and the view show, and hides the rest. The modern layout has only the
-- menu, and the options page over it.
local function lay_out(document)
  local orig = original(document)
  was_original = orig
  local shown = (orig or view == "options") and view or "menu"
  local function show(id, visible)
    document:get(id):set_class("hidden", not visible)
  end
  show("press-start", shown == "boot")
  show("panel", shown == "menu")
  for _, id in ipairs({ "erase", "battle" }) do
    show(id, orig)
  end
  -- Quit is the options page's in the original layout, whose menu has the original's entries and
  -- options.
  show("quit", not orig)
  show("versus", not orig)
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
  title_picture.apply(document, PictureBoxes)
  document:get("press-start-text"):set_class("blink-off", false)
  refresh(document)
end

local function set_fade(document, value)
  darkness = math.max(0.0, math.min(1.0, value))
  local fade = document:get("fade")
  fade:set_class("hidden", darkness <= 0.0)
  fade:set_property("opacity", string.format("%.3f", darkness))
end

-- The menu's first entry: continue when there is a save to continue (an addition, the original
-- layout's), else new game.
local function first_entry(document)
  if original(document) and FocusContinueWhenSaved and ui.setting("save.can_continue") == true then
    return "continue"
  end
  return "play"
end

-- The entry the menu comes back on: the one that left it (a slot list, the options), while the
-- layout shows it and the cursor may stop on it; else the first.
local function menu_focus(document)
  local id = remembered.menu
  if id == nil then
    return first_entry(document)
  end
  local element = document:get(id)
  if element:has_class("hidden") or element:has_class("unavailable") then
    return first_entry(document)
  end
  return id
end

local function set_view(document, name, focus)
  view = name
  leaving = false
  set_landing(name == "boot")
  blink = 0.0
  lay_out(document)
  if focus ~= nil then
    document:get(focus):focus()
  end
end

-- Opens a view on the entry it had last.
local function enter(document, name)
  if name == "menu" then
    set_view(document, "menu", nil)
    document:get(menu_focus(document)):focus()
  elseif name == "boot" then
    set_view(document, "boot", "press-start")
  elseif name == "battle" then
    -- The question starts on no each time, as the original's.
    set_view(document, "battle", "battle-no")
  elseif name == "language" then
    local current = index_of(ui.languages(), ui.setting("language")) or 1
    set_view(document, "language", "language-" .. current)
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
    -- Start ends the countdown (runLandingScreen: jumps to the fade).
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
  enter(document, "menu")
end

-- The options page over the title: this menu hides its box and TitleScreen shows the page; when it
-- closes the menu shows again, on Options.
function open_options(document)
  remembered.menu = "options"
  set_view(document, "options", nil)
  ui.send("title_options")
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

-- The online versus: the game hides this menu once the versus screen opens (TitleScreen::hide),
-- and shows it again, on this entry, when the player leaves it.
function open_versus()
  remembered.menu = was_original and "battle" or "versus"
  ui.send("versus")
end

function erase_game()
  remembered.menu = "erase"
  if ui.setting("save.can_continue") == true then
    ui.hide(document_name)
    ui.send("erase")
  end
end

-- A row of the language box: the language at once, then out to black and into the "Press Start"
-- screen; a second confirm in the fade does nothing.
function pick_language(document, row)
  local code = ui.languages()[row]
  if code ~= nil and not leaving then
    ui.send("language", code)
    close_language(document)
  end
end

function close_language(_document)
  if not leaving then
    leaving = true
    fade_target = 1.0
    pending_view = "boot"
  end
end

function on_show(document)
  -- The game forgets the screen whenever it shows or hides the title (TitleScreen::showMenu).
  landing = false
  title_picture.apply(document, PictureBoxes)
  refresh_prompts(document)
  was_original = original(document)
  local boot = ui.setting("title.boot") == true
  if boot then
    -- A new start of the title: the menu starts on its first entry.
    remembered.menu = nil
  end
  if original(document) and boot then
    set_fade(document, 1.0)
    fade_target = 0.0
    if ui.setting("title.language_first") == true then
      enter(document, "language")
    else
      set_view(document, "boot", "press-start")
    end
  else
    -- Back from a slot list or the options, on the entry that opened them.
    set_fade(document, 0.0)
    fade_target = 0.0
    enter(document, "menu")
  end
end

function on_setting(document, _name)
  title_picture.apply(document, PictureBoxes)
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

-- A skin switch can change the layout (the options page's skin row): the menu lays itself out
-- again, still hidden behind the options while they are open, else on the menu.
local function follow_layout(document)
  set_fade(document, 0.0)
  fade_target = 0.0
  if view == "options" then
    set_view(document, "options", nil)
  else
    enter(document, "menu")
  end
end

function on_tick(document, seconds)
  title_picture.apply(document, PictureBoxes)
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
  if view == "options" then
    -- The options page takes the input while it is open; should this menu get it, it shows again.
    enter(document, "menu")
    return true
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
  return false
end
