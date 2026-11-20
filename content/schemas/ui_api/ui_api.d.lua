---@meta
-- The UI API 0.20.0 (docs/modding/ui-api.md) for the Lua language server. The game
-- tests write it with DWE_WRITE_API_DOC=1: do not edit it by hand.

ui = {}

--- The value of a setting the game publishes; nil if it does not publish it.
---@param name string
---@return any
function ui.setting(name) end

--- Sends a command to the game, which handles it once.
---@param name string
---@param value any?
function ui.send(name, value) end

--- Tells the game a state; the game reads the last value when it needs it.
---@param name string
---@param value any?
function ui.report(name, value) end

--- Shows a screen; modal takes the navigation. args reaches its on_show.
---@param screen string
---@param modal boolean?
---@param args table?
function ui.show(screen, modal, args) end

--- Hides a screen.
---@param screen string
function ui.hide(screen) end

--- Whether a screen is visible.
---@param screen string
---@return boolean
function ui.is_visible(screen) end

--- The value the skin gives a layout option on this screen; nil without one.
---@param option string
---@return string?
function ui.layout(option) end

--- The name of the control that performs an action on the device in use.
---@param action string
---@return string
function ui.prompt(action) end

--- The device in use: keyboard_mouse or gamepad.
---@return string
function ui.device() end

--- The family of the gamepad in use.
---@return string
function ui.gamepad_family() end

--- The languages the player can choose.
---@return string[]
function ui.languages() end

--- The skins the player can choose.
---@return string[]
function ui.skins() end

--- The text key of a skin's name; empty if it does not exist.
---@param id string
---@return string
function ui.skin_name_key(id) end

--- Plays box_open, box_close or confirm; false for any other name.
---@param name string
---@return boolean
function ui.cue(name) end

--- The text of a key in the player's language, with its placeholders filled in.
---@param key string
---@param args table?
---@return string
function t(key, args) end

--- The name the interface loaded the document with.
---@type string
document_name = nil

hooks = {}

--- Calls handler each time the event arrives; up to 64 per document.
---@param event string
---@param handler fun(event: table, document: Element)
function hooks.on(event, handler) end

--- Stops calling handler for that event.
---@param event string
---@param handler function
function hooks.off(event, handler) end

--- Called by the interface when the document is shown.
---@param document Element
---@param args table
function on_show(document, args) end

--- Called by the interface when the document is hidden.
---@param document Element
function on_hide(document) end

--- Called by the interface when a setting changes, with the document visible.
---@param document Element
---@param name string
function on_setting(document, name) end

--- A navigation action (confirm, cancel, menu...); true if the document handles it.
---@param document Element
---@param action string
---@return boolean?
function on_action(document, action) end

--- Every frame while the document is visible.
---@param document Element
---@param seconds number
function on_tick(document, seconds) end

--- The device in use changed.
---@param document Element
function on_device(document) end

--- The language changed.
---@param document Element
function on_language(document) end

---@class Element
---@field id string Its id (a read-only field).
local Element = {}

--- Whether the element exists.
---@return boolean
function Element:valid() end

--- The element with that id inside this one.
---@param id string
---@return Element
function Element:get(id) end

--- Sets its text.
---@param text string
function Element:set_text(text) end

--- Turns it into the picture of the control that performs the action.
---@param action string
---@param icon boolean?
function Element:set_prompt(action, icon) end

--- Sets a text with its {prompt:action} marks drawn as controls.
---@param text string
---@param original boolean?
function Element:set_prompt_text(text, original) end

--- Its content.
---@return string
function Element:text() end

--- Adds or removes a class.
---@param name string
---@param on boolean
function Element:set_class(name, on) end

--- Whether it has a class.
---@param name string
---@return boolean
function Element:has_class(name) end

--- The value of an attribute; nil without one.
---@param name string
---@return string?
function Element:attribute(name) end

--- Sets an attribute; not an event one (on...).
---@param name string
---@param value string
function Element:set_attribute(name, value) end

--- Sets a style property.
---@param name string
---@param value string
function Element:set_property(name, value) end

--- Removes a property that set_property set.
---@param name string
function Element:remove_property(name) end

--- Its box in dp from the window's corner: left, top, width and height.
---@return number
---@return number
---@return number
---@return number
function Element:rect() end

--- Gives it the focus.
function Element:focus() end

--- Whether it has the focus.
---@return boolean
function Element:has_focus() end

--- Presses it.
function Element:click() end

--- Scrolls a list back to the top.
function Element:scroll_to_top() end

---@class Event
---@field type string The event's type (a read-only field).
---@field wheel number How far the wheel turned, positive downward (a read-only field).
local Event = {}

--- Stops the event from bubbling up.
function Event:stop() end

story = {}

--- What the box waits for from the player: page (its confirmation), choice (an option) or "" (nothing).
---@return string
function story.input() end

--- The box: open, kind, size (columns, rows), top, opening, origin, prompt, input, page, speaker (name, kind), rows (each with runs: text, color), cursor_row and cursor_columns.
---@return table
function story.textbox() end

--- The visible choice: options (text and row of each) and cursor (from 0); nil without a choice.
---@return table?
function story.choice() end

--- Asks to confirm: the page goes on or the choice takes the cursor's option.
---@return boolean
---@return string?
function story.advance() end

--- Asks to pick an option of the choice, from 0.
---@param option integer
---@return boolean
---@return string?
function story.choose(option) end

--- Asks to cancel, like the cancel button.
---@return boolean
---@return string?
function story.cancel() end

dialog = {}

--- The dialog: text_key, file, question, start_on_yes, at_title and options.
---@return table
function dialog.state() end

--- Its options in order (id, label_key, yes): yes and no in a question, accept in a notice.
---@return table
function dialog.options() end

--- Whether the dialog is shown, waiting for its answer.
---@return boolean
function dialog.is_open() end

--- Answers with one of its options, from 0.
---@param option integer
---@return boolean
---@return string?
function dialog.answer(option) end

--- Cancels: no to a question, accept to a notice.
---@return boolean
---@return string?
function dialog.cancel() end

naming = {}

--- Whether the naming box is shown, waiting for a name.
---@return boolean
function naming.is_open() end

--- The box: title_key, length, letters, name, grid (rows, columns) and cursor (row, column, button).
---@return table
function naming.state() end

--- The letter of a grid cell, from 0; " " if it is empty.
---@param row integer
---@param column integer
---@return string
function naming.cell(row, column) end

--- Asks to type grid letters at the end of the name.
---@param letters string
---@return boolean
---@return string?
function naming.write(letters) end

--- Asks to remove the last letter.
---@return boolean
---@return string?
function naming.erase() end

--- Asks to accept the name; the game does not take an empty one.
---@return boolean
---@return string?
function naming.accept() end

clock = {}

--- The time: year, day, hour, minute, night, phase (morning, day, late, night) and running (whether the game's time runs).
---@return table
function clock.now() end

field = {}

--- How dark the map change's fade is, from 0 to 1.
---@return number
function field.fade() end

--- The text key of the map name shown on arrival, or nil.
---@return string?
function field.map_title() end

--- The minimap: shown (the option and the field allow it), covered (a window covers it) and size.
---@return table
function field.minimap() end

--- Where the player is in the window: x and y as a percentage.
---@return table
function field.player() end

--- The shop job's tally while it lasts (tally, goal, better_pay, customer, customers, top, bonus, bonus_icon, bonus_icon_rect, tier, improved), or nil.
---@return table?
function field.shop_job() end

partner = {}

--- The bars (meters: id, label_key, value, min, max, two_tone, mood), the HP and MP (pools) and what it thinks of the map (area: liked, disliked or nil).
---@return table
function partner.status() end

--- The needs in order: id, label_key, active and urgency (1 the most urgent; 0 if it does not have it).
---@return table
function partner.needs() end

--- Where its head is in the window: visible, x, y (percentage) and evolving (the evolution bubble's box, or nil).
---@return table
function partner.bubble() end

--- Care mistakes, battles and hours lived; nil if the player has the status extras off.
---@return table?
function partner.extras() end

--- Asks to praise it.
---@return boolean
function partner.praise() end

--- Asks to scold it.
---@return boolean
function partner.scold() end

--- Asks to equip or remove the move of that slot of the species.
---@param slot integer
---@return boolean
---@return string?
function partner.toggle_technique(slot) end

bag = {}

--- The bag's slots.
---@return integer
function bag.count() end

--- The player's money.
---@return integer
function bag.money() end

--- A piece of data of a slot, or its default value.
---@param index integer
---@param name string
---@return any
function bag.field(index, name) end

--- A slot: empty, name, description, description_species, count, dimmed, usable, discardable, item_color, icon, icon_rect, favorite.
---@param index integer
---@return table
function bag.slot(index) end

--- The answer to the last action: key (the message's key) and item; nil before any.
---@return table?
function bag.result() end

--- Asks to use the item of a slot.
---@param index integer
---@return boolean
---@return string?
function bag.use(index) end

--- Asks to drop the item of a slot.
---@param index integer
---@return boolean
---@return string?
function bag.drop(index) end

--- Asks to swap two slots.
---@param from integer
---@param to integer
---@return boolean
---@return string?
function bag.move(from, to) end

--- Asks to sort the bag in an order.
---@param order integer
---@return boolean
---@return string?
function bag.sort(order) end

player = {}

--- The player: name, level, bits, seconds, raised, medals, items, cards.
---@return table
function player.profile() end

--- The medals the game publishes (not a fixed number): held and name_key.
---@return table
function player.medals() end

--- The city: recruited, total and prosperity.
---@return table
function player.city() end

options = {}

--- The game's options in order: name, kind (toggle, list, range), label_key, help_key, tab, default, original, values, texts, min, max.
---@return table
function options.list() end

--- An option's value now (or its default value); nil if it does not exist.
---@param name string
---@return any
function options.get(name) end

--- Asks to give an option a value; checks the type and the values it accepts.
---@param name string
---@param value any
---@return boolean
---@return string?
function options.set(name, value) end

battle = {}

--- The battle: phase, active, fast, frame_seconds, bag (available, open, item_in_flight), revive_seconds and match_seconds (or nil) and cup (a cup battle).
---@return table
function battle.state() end

--- The fighters, the partner first: name_key, status_key, hp, max_hp, mp, max_mp, fainted and hp_bar.
---@return table
function battle.fighters() end

--- The other players' partners fighting beside this player's in a co-op battle (empty otherwise): name_key, status_key, hp, max_hp, mp, max_mp and fainted.
---@return table
function battle.allies() end

--- The command strip, the finisher first: command, label_key, icon, available, current and mp_cost.
---@return table
function battle.commands() end

--- The finisher: progress, goal, ready, charging (the aura), charge (as a percentage) and gauge (the charge gauge's top-left corner over the partner: x, y as percentages, or nil).
---@return table
function battle.finisher() end

--- Where the strip and the partner's bars are when entering and leaving: top, bottom, blink, hp_x and mp_x (or nil).
---@return table
function battle.strip() end

--- The opening: step, anger (3 marks), flee (8), anger_scale, flash and pieces (the banner's pieces, with key).
---@return table
function battle.intro() end

--- The numbers over the fighters: x, y (percentage), dx, dy, value, kind (damage, gain, poison) and icon.
---@return table
function battle.numbers() end

--- The enemies' health bars: always and bars (enemy, x, y, left, top, fill, red, green, hp).
---@return table
function battle.hp_bars() end

--- The 1P and COM markers: fighter (0 the partner, 1 the rival), x and y (percentage).
---@return table
function battle.markers() end

--- The countdown over the fainted partner (x, y, digit, digit_scale, badge_scale, turn, palette), or nil.
---@return table?
function battle.death_countdown() end

--- The player's shout (text_key, x, y, node_x, node_y, line, hop, scale), or nil.
---@return table?
function battle.shout() end

--- The end box: outcome_key, lines (parts, name_key, subject_key, amount), stats (value, max, gain), bits, money, count_delay_frames and wait_frames.
---@return table
function battle.result() end

--- What a cup battle shows on top: shown, name, name_y, stats, counted, stat, time_up, time_up_label, box, partner, rival, damage_label, dealt, taken and wipe.
---@return table
function battle.match() end

--- Asks the partner to follow a command of the strip (its command); only in the fight phase.
---@param command integer
---@return boolean
---@return string?
function battle.command(command) end

--- A press of the finisher's charge; only in the fight phase.
---@return boolean
---@return string?
function battle.charge() end

--- Asks to open the bag over the battle (the partner_menu screen); only in the fight phase.
---@return boolean
---@return string?
function battle.open_bag() end

--- Asks to close the bag and return to the battle.
---@return boolean
---@return string?
function battle.close_bag() end

--- The end box has finished: the battle closes; only in the end phase.
---@return boolean
---@return string?
function battle.close() end

--- A point or a bit counted in the end box (its sound); only in the end phase.
---@return boolean
---@return string?
function battle.end_count() end

--- All the bits in (the sound that ends the count); only in the end phase.
---@return boolean
---@return string?
function battle.end_counted() end

versus = {}

--- The view, the cursor, the series' battles and, in a battle: round, clock, side (the player's side), wins, stalled, leave_asked, blink and arena (what the arena shows, or nil without one).
---@return table
function versus.state() end

--- The host's setup: series and series_count, arena, arena_row and arenas.
---@return table
function versus.setup() end

--- The connection: stage (creating, waiting, connecting) and code (the room code).
---@return table
function versus.connection() end

--- The code typed (typed) and whether the last one was not valid (bad).
---@return table
function versus.entry() end

--- The saves: player, nickname, partners and usable.
---@return table
function versus.saves() end

--- The partners (name, species_key, stats, picked) and the chosen ones (picks, their names).
---@return table
function versus.lineup() end

--- The two sides, the host first: hp, max_hp, mp, max_mp, segments, finisher_ready, hover, icons (the strip) and command_key (the command pointed at).
---@return table
function versus.fighters() end

--- What has happened, the latest last: key, who, what_key and amount.
---@return table
function versus.log() end

--- The battles played (rounds: won, lost, draw) and the series' winner (winner).
---@return table
function versus.results() end

--- The score screen: round, offset, flash, black, results and sides (wins, losses, faces).
---@return table
function versus.score() end

--- Why it stopped: key (text key) and detail.
---@return table
function versus.error() end

--- A step in the view: up, down, left, right, confirm, cancel, menu, tab_previous, tab_next or finisher (the finisher, with its meter full).
---@param step string
---@return boolean
---@return string?
function versus.step(step) end

--- Goes to a row of the view (or to a command of the player's strip) and confirms it.
---@param row integer
---@return boolean
---@return string?
function versus.pick(row) end

online = {}

--- The screens' state: open, sample, sample_offered, field, filter, code, code_length, quick, notice, notice_key and can_retry.
---@return table
function online.state() end

--- The player's online profile: name, named, icon, wins, losses, draft and relay_only.
---@return table
function online.profile() end

--- The icons a profile can pick: id and label_key.
---@return table
function online.icons() end

--- The room list: state, error, error_key, total and the rooms matching the filter (id, code, name, message, host_name, state, mode, mode_key, players, capacity, ping, ping_level, full, joinable).
---@return table
function online.list() end

--- The game modes: id, label_key, description_key, short_key, available and selected.
---@return table
function online.modes() end

--- The room the player is in, or nil: code, name, host, listed, phase, phase_key, mode, mode_key, capacity, error, error_key, detail, can_start, can_ready, ready and members (name, icon, host, ready, local).
---@return table?
function online.room() end

--- The text key of an error code; unknown codes give online.error.unknown.
---@param code string?
---@return string
function online.error_key(code) end

--- The text key of a notice code.
---@param code string?
---@return string
function online.notice_key(code) end

--- The rooms screens opened.
---@return boolean
function online.open() end

--- The rooms screens closed.
---@return boolean
function online.close() end

--- Asks for the list again.
---@return boolean
function online.refresh() end

--- The text field that takes the keyboard: filter, code, name or nil.
---@param field string?
---@return boolean
---@return string?
function online.edit(field) end

--- Replaces the room search's text.
---@param value string
---@return boolean
---@return string?
function online.set_filter(value) end

--- Replaces the code field's text.
---@param value string
---@return boolean
---@return string?
function online.set_code(value) end

--- Opens a room: public (listed) or private (by code only).
---@param public boolean
---@return boolean
---@return string?
function online.host(public) end

--- Joins a listed room.
---@param room_id string
---@return boolean
---@return string?
function online.join(room_id) end

--- Joins a room by a code, or by the typed one.
---@param code string?
---@return boolean
---@return string?
function online.join_by_code(code) end

--- Joins the first open playable room, or answers quick = none.
---@return boolean
function online.quick_match() end

--- The host picks the room's mode.
---@param id string
---@return boolean
---@return string?
function online.set_mode(id) end

--- The guest marks itself ready or not.
---@param ready boolean
---@return boolean
---@return string?
function online.set_ready(ready) end

--- The host starts the game.
---@return boolean
function online.start() end

--- Leaves the room.
---@return boolean
function online.leave() end

--- Replaces the name being typed on the profile screen.
---@param value string
---@return boolean
---@return string?
function online.set_name_draft(value) end

--- Keeps the typed name as the profile's.
---@return boolean
function online.save_name() end

--- Changes the profile's icon.
---@param id string
---@return boolean
---@return string?
function online.set_icon(id) end

--- Connects the next rooms through the server's relay only, or directly when possible.
---@param on boolean
---@return boolean
---@return string?
function online.set_relay_only(on) end

--- Asks again for the room last asked for, after a failure.
---@return boolean
function online.retry() end

--- Clears the notice, the quick match's answer and a closed room.
---@return boolean
function online.dismiss() end

--- Switches to the sample rooms and back.
---@param on boolean
---@return boolean
---@return string?
function online.set_sample(on) end

---@alias UiEventName string
---| "story.changed" The box changed: it opened, grew, another page, the cursor.
---| "story.text" A new page appears.
---| "story.input" The box started or stopped waiting for the player.
---| "dialog.changed" The game set the text of a dialog.
---| "naming.changed" The name, the cursor or the grid changed.
---| "clock.changed" The field state changed, the time included.
---| "field.fade" The fade changed.
---| "field.map_title" The map name shown changed.
---| "field.minimap" Whether the minimap can be seen, or whether something covers it, changed.
---| "field.shop_job" The shop job changed.
---| "partner.changed" Something about the partner changed; name says which setting.
---| "partner.bubble_moved" Its head moved in the window.
---| "bag.changed" The bag changed.
---| "bag.result" The game answered a bag action.
---| "options.changed" An option changed; name says which and value its value.
---| "battle.started" A battle starts (battle.active becomes true).
---| "battle.ended" A battle ends, after the end box closes (battle.active becomes false).
---| "battle.phase" The phase changed; value says which.
---| "battle.changed" The fighters, the strip, the finisher, the bag or the counts changed.
---| "battle.speed" The battle switched to fast or to normal speed.
---| "battle.strip_moved" The strip or the partner's bars moved, or the icon under the cursor blinked.
---| "battle.intro" The opening changed.
---| "battle.numbers" The numbers over the fighters changed.
---| "battle.overlays" The enemies' health bars, the 1P and COM markers or the fainted partner's countdown changed.
---| "battle.shout" The player's shout changed.
---| "battle.match" What a cup battle shows on top changed.
---| "versus.changed" The versus changed.
---| "online.changed" The list, the room or the screens' state changed.
