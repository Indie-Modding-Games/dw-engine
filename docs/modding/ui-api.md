# Interface API

What the game offers to the interface documents (`ui/<screen>/`) and what it accepts from them, screen by screen. A
skin or a mod that remakes a screen works against this list: what is here is stable within a major version, what is
not here can change without notice. How the documents are loaded and drawn is in [UI](../architecture/ui.md); the
style of each screen, in [skins](skins.md).

## Settings, commands, states, options and arguments

- A **setting** is a value that the game publishes and the document reads with `ui.setting("name")`. When it changes,
  the document receives `on_setting(document, name)`.
- A **command** is what the document sends with `ui.send("name", value)` and the game handles: open a menu, use an
  item, save. A command the game does not know is left in the log as a warning, not lost silently.
- A **state** is what the document reports with `ui.report("name", value)` and the game reads when it needs it: for
  example whether a page is showing a 3D model right now. The last value holds; it is not a request that the game
  handles once like a command.
- An **option** is a player setting (those of "Options"): the document reads it as a setting and changes it by sending
  its name with the new value.
- An **argument** is a value with which another document shows this one: `ui.show("name", modal, {key = value})`
  passes it and the document reads it in `on_show(document, args)`. `args` is always a table (empty if there are no
  arguments) and only carries yes/no values, numbers and texts. That way a menu tells the next one where to start
  without going through a game setting.

In a name, `<n>` is a number from 0 and `<id>` an identifier (lowercase, digits and `_`). So `hud.meter.<n>.value`
covers `hud.meter.0.value`, `hud.meter.1.value`...

## Engine functions and editor help

- A **function** is part of the engine's own API, the same in all documents: the `ui` table, `hooks`, `t`,
  `document_name`, the handlers the document defines (`on_show`, `on_tick`...) and the methods of its elements
  (`Element:set_class`) and events. Without a signature it is a read-only field. A test checks that what is declared
  is exactly what the engine gives.
- `content/schemas/ui_api/ui_api.d.lua` has those functions and those of the API namespaces with their parameters, for
  the Lua server (LuaLS): add the folder to `workspace.library` in your `.luarc.json` and the editor completes names
  and types. The tests generate it together with this page.

## API namespaces: queries, actions and events

Besides the loose settings, the interface has named namespaces (`clock`, `field`, `partner`, `bag`, `player`,
`options`, `battle`, `versus`, `story`, `dialog`, `naming`) that any document uses the same way, whether it is from the
built-in interface or another:

- A **query** reads the game and returns a new table: `partner.status()`, `bag.slot(3)`, `clock.now()`. Changing that
  table changes nothing in the game.
- An **action** asks the game for something: `bag.use(3)`, `options.set("digital_time", false)`. It checks its
  arguments and returns `true`, or `false` and why; the game checks it again and answers with an event.
- An **event** arrives at the functions the document registers with `hooks.on("bag.changed", function)`. The function
  receives a table with `version` (that of the event's table), `name` (the setting that changed) and `value`, and the
  document. `hooks.off` removes it; a document registers at most 64. Events also arrive at hidden documents: a screen
  that only draws while it is visible checks with `ui.is_visible(document_name)`.

The namespaces live in `ui/api/<namespace>.lua` of the game pack and are loaded once, before any document. What the
"Improvements" that are turned off hide is also hidden here: `partner.extras()` gives `nil` if the player has turned
off the status extras.

## Layout

A screen that is laid out in two ways asks which one the skin wants with `ui.layout("option")`, which gives the value
the skin sets for that option on that screen or `nil` (`skin.yaml`, `layout:`; see [skins](skins.md)). Scripts do not
ask for the skin's id (`ui.setting("ui_skin")`) or its classes to decide the arrangement.

## Versions

The interface API carries its own version, apart from the [Lua API](lua-api.md): a minor version adds names, a major
one removes some or changes what they mean. While it is 0.x it describes what the screens use today and may move
between game versions.

| Version | Changes |
|---|---|
| 0.16.0 | The improved shop job: the bar gains `shop_job.top`, `shop_job.bonus`, `shop_job.bonus_icon`, `shop_job.bonus_icon_rect`, `shop_job.tier` and `shop_job.improved` (also in `field.shop_job()`), and the `shop_job_rules` command (the ? button that opens the rules panel). The `shop_job_mode` option arrives. The new screens `shop_haggle` and `shop_job_rules` are described in the [job's reference](../reference/shop-job.md#interface) |
| 0.15.0 | The `story` namespaces arrive (the story's text box: `story.textbox()`, `story.choice()`, `story.input()`; the actions `story.advance`, `story.choose` and `story.cancel`; the events `story.changed`, `story.text` and `story.input`), `dialog` (the save dialog: `dialog.state()`, `dialog.options()`, `dialog.answer`, `dialog.cancel`, the event `dialog.changed`) and `naming` (the name box: `naming.state()`, `naming.cell`, `naming.write`, `naming.erase`, `naming.accept`, the event `naming.changed`). New settings `story_textbox.speaker`, `speaker_kind`, `options`, `first_option`, `input` and `page`, and the commands `story_advance`, `story_choose`, `story_cancel`, `naming_write`, `naming_erase` and `naming_accept`. The `story_textbox`, `dialog` and `naming_box` screens only read through them. An addon only sends them while its box is waiting for the player |
| 0.14.0 | The `battle` namespace arrives: the battle state, the fighters, the command strip, the finisher, the opening, the numbers, the enemies' bars, the shout, the end box and what a cup battle shows (`battle.match()`), with its actions (`battle.command`, `battle.charge`, `battle.open_bag`, `battle.close_bag`, `battle.close`, `battle.end_count`, `battle.end_counted`) and events (`battle.started`, `battle.ended`, `battle.phase`, `battle.changed`...). The `battle` and `tournament_match` screens only read through it. The `versus` namespace arrives: each view of online versus (`versus.state()`, `versus.setup()`, `versus.lineup()`, `versus.fighters()`, `versus.score()`...), its steps (`versus.step`, `versus.pick`) and its event (`versus.changed`); the `versus` screen only reads through it and through `battle.intro()`. An addon only sends the battle commands at their moment: those of the battle in the `fight` phase, closing the bag with the bag open and those of the end box in the `end` phase |
| 0.13.0 | The `battle` screen arrives with the command strip (`battle.menu.<n>.*`, with `mp_cost`, the MP each technique costs), the fighters' bars and what the battle sends back |
| 0.12.0 | The notices enter the contract: `pause_box`, `field_item_notice`, `award_notice`, `fishing_notice`, `save_indicator`, `money_box`, `lives_box` and `ui_notice`, with their settings and commands |
| 0.11.0 | Plugins (addons of type `plugin`, [interfaces](ui-addons.md)): documents on top of any interface. Option `ui_plugins` and setting `ui_plugins.count` |
| 0.10.0 | Interface addons ([interfaces](ui-addons.md)): `ui_addon`, `ui_addons`, `ui_addon.name.<id>`, `ui_addon.safe`, `ui_addon.problem` and `ui_addon_reload`; the `ui_notice` notice. An addon's `ui.send` only lets the contract's commands and options through |
| 0.9.0 | The engine's own API (`ui`, `hooks`, `t`, `document_name`, the `on_*` handlers and the methods of elements and events) enters the contract as functions. Definitions for the Lua server are generated in `content/schemas/ui_api/ui_api.d.lua` |
| 0.8.0 | Screens: the game asks for a screen and the active interface decides which document shows it. The default interface is described in `ui/addon.yaml` of the game pack |
| 0.7.0 | States arrive (`ui.report`): the menus tell the game which 3D portrait to draw (`pause_menu.portrait`, `pause_menu.medal`, `partner_menu.portrait`) instead of the game reading their classes |
| 0.6.0 | The `clock`, `field`, `partner`, `bag`, `player` and `options` namespaces arrive with their queries, actions and events, and `hooks.on`/`hooks.off`. The HUD and the menu options already read through them |
| 0.5.0 | The HUD reads its rules from the game: `hud.clock.night`, `hud.clock.phase` and `hud.meter.<n>.mood`. Its script is split into `presenter.lua` (what it shows) and `view.lua` (how it draws it) |
| 0.4.0 | The `skin_picker` screen arrives, the first-startup box that asks for the skin: `ui_skin`, `ui_skin_preview` and `skin_picker_done` |
| 0.3.0 | The arguments of `ui.show` and `on_show(document, args)` arrive. `pause_menu.start_entry` goes away: the partner menu passes `entry` to the pause menu on returning |
| 0.2.0 | `ui.layout` arrives. `pause_menu.evolutions` and `open_evolutions` go away, the partner menu's evolutions tab that no skin uses any more, and the partner menu's `ui_skin` option, which now asks for its layout |
| 0.1.0 | The list of what the HUD, the pause menu and the partner menu use today |

The list comes from `apps/game/src/ui_contract.cpp`. A test compares it with this page, with
`content/schemas/ui_api/ui_api.json` (the same list for tools and editors) and with what each screen's scripts read and
send; `DWE_WRITE_API_DOC=1` when running the game's tests rewrites both. For now it covers the HUD, the pause menu, the
partner menu, the startup's skin box, the notices, the battle (with what a cup battle shows), online
versus and the story boxes (the text box, the save dialog and the name box); the rest of the screens comes later.

## Reference

<!-- ui-api:generated -->
UI API version: **0.16.0**.

### `ui`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `ui.setting(name: string): any` | function | number or text | The value of a setting the game publishes; nil if it does not publish it. |
| `ui.send(name: string, value: any?)` | function | — | Sends a command to the game, which handles it once. |
| `ui.report(name: string, value: any?)` | function | — | Tells the game a state; the game reads the last value when it needs it. |
| `ui.show(screen: string, modal: boolean?, args: table?)` | function | — | Shows a screen; modal takes the navigation. args reaches its on_show. |
| `ui.hide(screen: string)` | function | — | Hides a screen. |
| `ui.is_visible(screen: string): boolean` | function | yes/no | Whether a screen is visible. |
| `ui.layout(option: string): string?` | function | text | The value the skin gives a layout option on this screen; nil without one. |
| `ui.prompt(action: string): string` | function | text | The name of the control that performs an action on the device in use. |
| `ui.device(): string` | function | text | The device in use: keyboard_mouse or gamepad. |
| `ui.gamepad_family(): string` | function | text | The family of the gamepad in use. |
| `ui.languages(): string[]` | function | table | The languages the player can choose. |
| `ui.skins(): string[]` | function | table | The skins the player can choose. |
| `ui.skin_name_key(id: string): string` | function | text | The text key of a skin's name; empty if it does not exist. |
| `ui.cue(name: string): boolean` | function | yes/no | Plays box_open, box_close or confirm; false for any other name. |
| `t(key: string, args: table?): string` | function | text | The text of a key in the player's language, with its placeholders filled in. |
| `document_name` | function | text | The name the interface loaded the document with. |

### `hooks`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `hooks.on(event: string, handler: fun(event: table, document: Element))` | function | — | Calls handler each time the event arrives; up to 64 per document. |
| `hooks.off(event: string, handler: function)` | function | — | Stops calling handler for that event. |

### `document`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `on_show(document: Element, args: table)` | function | — | Called by the interface when the document is shown. |
| `on_hide(document: Element)` | function | — | Called by the interface when the document is hidden. |
| `on_setting(document: Element, name: string)` | function | — | Called by the interface when a setting changes, with the document visible. |
| `on_action(document: Element, action: string): boolean?` | function | yes/no | A navigation action (confirm, cancel, menu...); true if the document handles it. |
| `on_tick(document: Element, seconds: number)` | function | — | Every frame while the document is visible. |
| `on_device(document: Element)` | function | — | The device in use changed. |
| `on_language(document: Element)` | function | — | The language changed. |

### `Element`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `Element:valid(): boolean` | function | yes/no | Whether the element exists. |
| `Element.id` | function | text | Its id (a read-only field). |
| `Element:get(id: string): Element` | function | table | The element with that id inside this one. |
| `Element:set_text(text: string)` | function | — | Sets its text. |
| `Element:set_prompt(action: string, icon: boolean?)` | function | — | Turns it into the picture of the control that performs the action. |
| `Element:set_prompt_text(text: string, original: boolean?)` | function | — | Sets a text with its {prompt:action} marks drawn as controls. |
| `Element:text(): string` | function | text | Its content. |
| `Element:set_class(name: string, on: boolean)` | function | — | Adds or removes a class. |
| `Element:has_class(name: string): boolean` | function | yes/no | Whether it has a class. |
| `Element:attribute(name: string): string?` | function | text | The value of an attribute; nil without one. |
| `Element:set_attribute(name: string, value: string)` | function | — | Sets an attribute; not an event one (on...). |
| `Element:set_property(name: string, value: string)` | function | — | Sets a style property. |
| `Element:remove_property(name: string)` | function | — | Removes a property that set_property set. |
| `Element:rect(): number, number, number, number` | function | number | Its box in dp from the window's corner: left, top, width and height. |
| `Element:focus()` | function | — | Gives it the focus. |
| `Element:has_focus(): boolean` | function | yes/no | Whether it has the focus. |
| `Element:click()` | function | — | Presses it. |
| `Element:scroll_to_top()` | function | — | Scrolls a list back to the top. |

### `Event`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `Event.type` | function | text | The event's type (a read-only field). |
| `Event.wheel` | function | number | How far the wheel turned, positive downward (a read-only field). |
| `Event:stop()` | function | — | Stops the event from bubbling up. |

### `hud`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `hud.revision` | setting | number | Goes up each time the field state below changes. |
| `hud.shown` | setting | yes/no | Whether the field's status panels are shown. |
| `hud.clock.year` | setting | number | The year of the game clock. |
| `hud.clock.day` | setting | number | The day of the year of the game clock. |
| `hud.clock.hour` | setting | number | The hour of the game clock, from 0 to 23. |
| `hud.clock.minute` | setting | number | The minute of the game clock. |
| `hud.clock.night` | setting | yes/no | Whether it is night: before 6:00 and from 17:00, when the original's clock shows the moon. |
| `hud.clock.phase` | setting | text | The part of the day: morning, day, late or night (the game's rule, not the interface's). |
| `hud.meter.<n>.label` | setting | text | The text key of a partner meter (happiness, discipline). |
| `hud.meter.<n>.value` | setting | number | The value of a partner meter. |
| `hud.meter.<n>.min` | setting | number | The lowest value of a partner meter. |
| `hud.meter.<n>.max` | setting | number | The highest value of a partner meter. |
| `hud.meter.<n>.mood` | setting | text | low below the middle of its range, where the original changes the bar's icon; otherwise high. |
| `hud.meter.<n>.two_tone` | setting | yes/no | Whether the meter fills toward both sides from its center. |
| `hud.pool.<n>.label` | setting | text | The text key of the HP or the MP. |
| `hud.pool.<n>.value` | setting | number | The partner's HP or MP now. |
| `hud.pool.<n>.max` | setting | number | The partner's maximum HP or MP. |
| `hud.need.<n>.label` | setting | text | The text key of a need (hunger, sleep, toilet...). |
| `hud.need.<n>.active` | setting | yes/no | Whether the partner has that need now. |
| `hud.need.<n>.urgency` | setting | number | Its rank among the active needs; 1 is the most urgent. |
| `hud.partner.area` | setting | number | 1 if the partner likes the area, 2 if it dislikes it, 0 if neither. |
| `hud.partner.revision` | setting | number | Goes up each time the partner position below changes. |
| `hud.partner.x` | setting | number | The partner's position in the window, as a percentage of its width. |
| `hud.partner.y` | setting | number | The partner's position in the window, as a percentage of its height. |
| `hud.partner.visible` | setting | yes/no | Whether the partner is in the window. |
| `hud.partner.evolving` | setting | yes/no | Whether the partner is evolving. |
| `hud.minimap_allowed` | setting | yes/no | False while the minimap is not on screen (battles, scenes). |
| `hud.minimap_covered` | setting | yes/no | True while a window covers the minimap. |
| `battle.active` | setting | yes/no | Whether a battle is in progress. |
| `screen_fade` | setting | number | How dark the screen fade is, from 0 to 1. |
| `map_title_key` | setting | text | The text key of the map title shown on arrival; empty if there is none. |
| `shop_job.shown` | setting | yes/no | Whether the shop job bar is shown. |
| `shop_job.tally` | setting | number | The shop job's tally so far. |
| `shop_job.goal` | setting | number | The tally from which the shopkeeper joins. |
| `shop_job.better_pay` | setting | number | The tally from which the pay is better. |
| `shop_job.customers` | setting | number | The customers served. |
| `shop_job.customer` | setting | number | The customer being served. |
| `shop_job.top` | setting | number | The bar's end: the lineup's perfect score in the improved job, the joining tally in the original. |
| `shop_job.bonus` | setting | number | The tally from which the improved job gives the bonus; 0 if the bar does not reach it. |
| `shop_job.bonus_icon` | setting | text | The image of the bonus icon; empty if there is none. |
| `shop_job.bonus_icon_rect` | setting | text | The part of that image. |
| `shop_job.tier` | setting | text | The pay reached: lowest, better, joins or bonus. |
| `shop_job.improved` | setting | yes/no | Whether the improved job is played. |
| `shop_job.revision` | setting | number | Goes up each time something about the shop job changes. |
| `minimap` | option | text | The minimap: on or off. |
| `minimap_size` | option | text | The minimap's size. |
| `digital_time` | option | yes/no | The time in digits. |
| `need_bubble_stack` | option | yes/no | All the active needs over the partner, not only the first. |
| `open_menu` | command | — | Opens the pause menu (the HUD's menu button). |
| `shop_job_rules` | command | — | Opens the shop job's rules panel (the ? button on its bar). |

### `pause_menu`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `pause_menu.at_title` | setting | yes/no | The menu opens over the title screen: only the options. |
| `pause_menu.start_page` | setting | text | options when Esc or Select opened the menu on its options page. |
| `pause_menu.preview_player` | setting | yes/no | Opens the player page directly (previews). |
| `pause_menu.sleep_enabled` | setting | yes/no | Whether the partner can go to sleep now. |
| `pause_menu.fishing` | setting | number | 0 without a rod, 1 with a rod but no water, 2 the player can fish. |
| `hud.clock.year` | setting | number | The year of the game clock, for the date header. |
| `hud.clock.day` | setting | number | The day of the game clock, for the date header. |
| `field.player_x` | setting | number | The player's position in the window as a percentage, from where the menu's box grows. |
| `field.player_y` | setting | number | The player's position in the window as a percentage, from where the menu's box grows. |
| `season` | setting | text | The current season, when seasons are on. |
| `player.name` | setting | text | The player's name. |
| `player.level` | setting | number | The player's level. |
| `player.bits` | setting | number | The player's money. |
| `player.seconds` | setting | number | The time played, in seconds. |
| `player.raised` | setting | number | The partners raised. |
| `player.medals` | setting | number | The medals earned. |
| `player.medals.held` | setting | text | Each medal, 1 if held or 0, as a list of numbers. |
| `player.medal.<n>.name` | setting | text | The text key of a medal. |
| `player.medal.<n>.line.<n>` | setting | text | A line of a medal's description, as a text key. |
| `player.items.held` | setting | text | The key items held, as a list of numbers. |
| `player.cards.copies` | setting | text | The copies of each card, as a list of numbers. |
| `player.card.<n>.palette` | setting | number | The palette a card's image is drawn with. |
| `player.chart.count` | setting | number | The cells of the evolution chart. |
| `player.chart.<n>.id` | setting | text | The species of a chart cell. |
| `player.chart.<n>.name` | setting | text | The name of a chart cell. |
| `player.chart.<n>.level` | setting | number | The level of a chart cell. |
| `player.chart.<n>.source` | setting | text | The image of a chart cell. |
| `player.chart.<n>.<id>` | setting | number | The position of a chart cell and the rectangle of its image (index, x, y, u, v). |
| `player.chart.<n>.from.<n>` | setting | number | A cell a chart cell evolves from; -1 if none. |
| `player.chart.<n>.to.<n>` | setting | number | A cell a chart cell evolves into; -1 if none. |
| `player.chart.detail.<id>.<n>.<n>` | setting | number | The tables of the detail view (lines, colors, sprites, margins). |
| `player.chart.detail.layout_ready` | setting | yes/no | The detail view's tables are published. |
| `player.chart.detail.paths_ready` | setting | yes/no | The cells' from and to lists are published. |
| `player.chart.raised` | setting | text | The cells raised so far, as a list of numbers. |
| `player.chart.mega` | setting | text | The species of the Mega step, separated by commas. |
| `player.chart.preview` | setting | yes/no | Opens the player page on the chart (previews). |
| `player.city.species` | setting | text | The city's residents, as a list. |
| `player.city.levels` | setting | text | The residents' levels, as a list. |
| `player.city.points` | setting | text | The prosperity points each resident adds, as a list. |
| `player.city.recruited` | setting | text | Which residents have been recruited, as a list. |
| `player.city.residents` | setting | number | The residents recruited. |
| `player.city.total` | setting | number | The residents there are. |
| `player.city.prosperity` | setting | number | The city's prosperity. |
| `evoreq.version` | setting | number | Goes up each time the evolution requirements below change. |
| `evoreq.ready` | setting | yes/no | The evolution requirements are published. |
| `evoreq.hours` | setting | text | The hours of each evolution step. |
| `evoreq.partner` | setting | text | How the partner stands regarding its evolutions. |
| `evoreq.partner.<id>` | setting | text | How the partner meets the requirements of an evolution. |
| `evoreq.req.<id>` | setting | text | A species' evolution requirements, as ranges. |
| `open_partner_menu` | command | text | Opens the partner's menu on a page: items or status. |
| `pause_menu.portrait` | report | yes/no | Whether the game draws the player in 3D over its page (an improvement). |
| `pause_menu.medal` | report | number | The medal the game draws in 3D over the medals page, from 0 to 14; -1 none. |
| `praise` | command | — | Praises the partner. |
| `scold` | command | — | Scolds the partner. |
| `sleep` | command | — | Closes the menu and sends the partner to sleep. |
| `fish` | command | — | Closes the menu and takes out the rod. |
| `quick_save` | command | — | Saves the game. |
| `to_title` | command | — | Goes back to the title screen. |
| `quit` | command | — | Quits the game. |
| `title_options_back` | command | — | Closes the options opened over the title screen. |
| `language` | option | text | The language. |
| `fullscreen` | option | yes/no | Full screen or windowed. |
| `vsync` | option | yes/no | Vertical sync. |
| `ui_scale` | option | number | The interface's scale; 1 is the normal one. |
| `ui_skin` | option | text | The interface's skin. |
| `ui_addon` | option | text | The whole interface: default (the game's) or the id of one in ui/addons; applied when the menu closes. |
| `ui_addons` | setting | text | The interfaces offered, ids separated by spaces, default first. |
| `ui_addon.name.<id>` | setting | text | The name of an interface, from its addon.yaml. |
| `ui_addon.safe` | setting | yes/no | Safe mode (--safe-ui or Shift at startup): only the game's interface. |
| `ui_plugins` | option | yes/no | The plugins in ui/addons (addons of type plugin), all on or all off. |
| `ui_plugins.count` | setting | number | How many plugins are offered. |
| `ui_addon_reload` | command | — | Reads the interface in use and the plugins again from their files. |
| `text_font` | option | text | The font of the text with the original look: modern or original. |
| `walk_control` | option | text | Walking with the keyboard or the mouse: keys or mouse. |
| `autosave` | option | yes/no | Autosave. |
| `world_map` | option | yes/no | The island map. |
| `minimap` | option | text | The minimap: on or off. |
| `minimap_size` | option | text | The minimap's size. |
| `minimap_items` | option | yes/no | Items on the minimap. |
| `minimap_enemies` | option | yes/no | Enemies on the minimap. |
| `bulk_trades` | option | yes/no | Bulk trades. |
| `long_names` | option | yes/no | Long names. |
| `evolution_requirements` | option | yes/no | The evolution requirements on the chart. |
| `canonical_stages` | option | yes/no | Canonical stage names. |
| `status_extras` | option | yes/no | The extras of the partner's page. |
| `rare_items` | option | yes/no | Rare items. |
| `battle_speed` | option | text | Battle speed: normal, key or fast. |
| `enemy_hp_bars` | option | yes/no | Enemies' HP bars. |
| `gym_sessions` | option | number | Gym sessions in a row. |
| `quick_battle_start` | option | yes/no | Quick battle start. |
| `difficulty` | option | text | Difficulty: normal or easy. |
| `shop_job_mode` | option | text | The canyon shop job: improved or original. |
| `skip_evolution` | option | yes/no | Skip the evolution scene. |
| `choice_cancel_closes` | option | yes/no | Cancel closes a choice. |
| `fair_sickness_chance` | option | yes/no | Fair chance of falling sick. |
| `fair_evolution_average` | option | yes/no | Fair evolution average. |
| `fair_move_forgetting` | option | yes/no | Fair move forgetting. |
| `fair_card_stock` | option | yes/no | Fair card stock. |
| `choose_egg_after_lives_lost` | option | yes/no | Choose the egg when the lives are lost. |
| `generous_talk` | option | yes/no | Generous talk. |
| `extra_toilets` | option | yes/no | Extra toilets. |
| `smooth_motion` | option | yes/no | Smooth motion. |
| `hd_scenery` | option | yes/no | HD scenery. |
| `scenery_quality` | option | text | Scenery quality: upscaled or smooth. |
| `antialiasing` | option | yes/no | Antialiasing. |
| `texture_filter` | option | text | Texture filter: upscaled, smooth or nearest. |
| `model_characters` | option | yes/no | Characters as models. |
| `widescreen_pictures` | option | yes/no | Widescreen pictures. |
| `enhanced_lighting` | option | yes/no | Enhanced lighting. |
| `vivid_colors` | option | yes/no | Vivid colors. |
| `shadows` | option | text | Shadows: sun, round or off. |
| `chibi_style` | option | text | Chibi style: off or external. |
| `weather` | option | yes/no | Weather. |
| `seasons` | option | yes/no | Seasons. |
| `music_volume` | option | number | Music volume. |
| `effects_volume` | option | number | Effects volume. |
| `continuous_music` | option | yes/no | Continuous music. |
| `reverb` | option | yes/no | Reverb. |
| `translated_signs` | option | yes/no | Translated signs. |
| `digital_time` | option | yes/no | The time in digits. |
| `save_notice` | option | yes/no | The save notice. |
| `need_bubble_stack` | option | yes/no | All the active needs over the partner. |
| `shop_job_bar` | option | yes/no | The shop job bar. |
| `name_spacing` | option | yes/no | Name spacing. |
| `free_exploration` | option | yes/no | Free exploration. |
| `shop_extras` | option | yes/no | Shop extras. |
| `favorite_food` | option | yes/no | The partner's favorite food. |
| `art_fixes` | option | yes/no | Art fixes. |
| `dev_tools` | option | yes/no | Development tools. |
| `entry` | argument | text | When coming back from the partner's menu: items or partner, the entry that gets the focus in the original layout. |

### `ui_notice`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `ui_addon.problem` | setting | text | Why the chosen interface could not be used; empty if it could. |

### `field_item_notice`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `field_item_notice.text` | setting | text | The text key of the notice for an item on the ground or in a chest. |
| `field_item_notice.item` | setting | text | The text key of the item's name; empty for an empty chest. |
| `field_item_notice.answer` | setting | text | Whether the notice accepts confirm now: sound, quiet (no sound) or empty (not yet). |
| `field_item_notice.phase` | setting | text | open while shown; closing when the player has answered and the notice closes. |
| `field.player_x` | setting | number | Where the player is on the screen, from 0 to 1 horizontally (the box grows from there). |
| `field.player_y` | setting | number | Where the player is on the screen, from 0 to 1 vertically. |
| `field_item_notice_ok` | command | — | The player confirms the notice. |
| `field_item_notice_closed` | command | — | The box finished closing (after closing). |

### `award_notice`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `award_notice.lines` | setting | text | The text key of the notice for a prize or a change of the partner. |
| `award_notice.color` | setting | number | The color of the name, as a number. |
| `award_notice.name` | setting | text | The name the notice shows. |
| `award_notice_ok` | command | — | The player confirms the notice. |

### `fishing_notice`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `fishing_notice.text` | setting | text | The text key of what was caught. |
| `fishing_notice.item` | setting | text | The text key of the name of what was caught. |
| `fishing_notice.size` | setting | number | The size of what was caught. |
| `fishing_notice_ok` | command | — | The player confirms the notice. |

### `save_indicator`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `save_indicator.text` | setting | text | What the save indicator says (saving, loading...). |
| `save_indicator.opacity` | setting | number | How visible the indicator is, from 0 to 1. |
| `save_indicator.lift` | setting | number | How far the indicator has risen on appearing, from 0 to 1. |
| `save_indicator.figure` | setting | yes/no | Whether the indicator shows the spinning figure. |

### `money_box`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `money_box.money` | setting | number | The player's money. |
| `money_box.bits_key` | setting | text | The text key of the currency's name. |
| `money_box.revision` | setting | number | Goes up each time the values above change: the box is drawn again. |

### `lives_box`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `lives_box.outline` | setting | yes/no | Whether the lives box is opening or closing (the border only). |
| `lives_box.grow` | setting | number | How far the border has grown, from 0 to 1. |
| `lives_box.anchor_x` | setting | number | Where it grows from, from 0 to 1 horizontally (the partner). |
| `lives_box.anchor_y` | setting | number | Where it grows from, from 0 to 1 vertically. |
| `lives_box.lives` | setting | number | The lives the partner has left. |
| `lives_box.slots` | setting | number | How many lives fit in the box. |
| `lives_box.lost_full` | setting | yes/no | Whether the life being lost is still drawn full (it blinks as it is lost). |
| `lives_box.revision` | setting | number | Goes up each time the values above change. |
| `screen_fade` | setting | number | How dark the screen fade is, from 0 to 1. |

### `partner_menu`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `partner_menu.start_page` | setting | text | The page the menu opens on: items or status. |
| `partner_menu.context` | setting | text | battle for the player's bag in a battle; empty in the field. |
| `partner_menu.extra_presses` | setting | number | The presses of the extra button while the menu is open. |
| `partner.revision` | setting | number | Goes up each time the partner settings below change. |
| `partner.name` | setting | text | The text key of the species' name. |
| `partner.nickname` | setting | text | The partner's name. |
| `partner.age` | setting | number | Its age in days. |
| `partner.weight` | setting | number | Its weight. |
| `partner.lives` | setting | number | The lives it has left. |
| `partner.hp` | setting | number | Its HP now. |
| `partner.max_hp` | setting | number | Its maximum HP. |
| `partner.mp` | setting | number | Its MP now. |
| `partner.max_mp` | setting | number | Its maximum MP. |
| `partner.pool.<n>.<id>` | setting | number or text | The HP or MP bar: label, value, min, max. |
| `partner.stat.<n>.<id>` | setting | number or text | The bar of a stat: label, value, min, max. |
| `partner.meter.<n>.<id>` | setting | number or text | The bar of a meter: label, value, min, max, two_tone. |
| `partner.condition.count` | setting | number | The conditions in the list. |
| `partner.condition.<n>.key` | setting | text | The text key of a condition. |
| `partner.tiredness.<id>` | setting | number or text | The tiredness bar (status extras): label, value, min, max. |
| `partner.care_mistakes` | setting | number | Care mistakes (status extras). |
| `partner.battles` | setting | number | Battles fought (status extras). |
| `partner.lifetime_hours` | setting | number | Hours lived. |
| `partner.evolution.hours` | setting | number | Hours since it last changed level. |
| `partner.evolution.check_hours` | setting | number | Hours from which its level checks for an evolution; -1 if never. |
| `partner.awake.<id>` | setting | number | Its wake-up and sleep hour and minute (status extras). |
| `partner.favorite_food` | setting | text | The text key of its favorite food's name. |
| `partner.profile.type` | setting | number | Its type. |
| `partner.profile.active` | setting | number | Its activity pattern. |
| `partner.profile.special.<n>` | setting | number | A special type; -1 if none. |
| `partner.technique.<n>.<id>` | setting | number or text | An equipped move: empty, name, power, mp, range, effect. |
| `partner.technique.choice.<n>.<id>` | setting | number or text | A move of the grid: name, available, equipped, known, power, mp, range, effect, slot, special. |
| `partner.technique.can_finish` | setting | yes/no | Whether the move editing can finish (a damage move is left). |
| `partner.technique.result` | setting | text | The text key of the last change's message; empty if there is none. |
| `partner.care_result` | setting | text | The text key of the last praise or scolding's message. |
| `bag.revision` | setting | number | Goes up each time the bag settings below change. |
| `bag.money` | setting | number | The player's money. |
| `bag.slot.count` | setting | number | The bag's slots. |
| `bag.slot.<n>.<id>` | setting | number or text | A slot: empty, name, description, description_species, count, dimmed, usable, discardable, item_color, icon, icon_rect, favorite. |
| `bag.result` | setting | text | The text key of the last bag action's message. |
| `bag.result_item` | setting | text | The item of the last bag action. |
| `field.player_x` | setting | number | The player's position in the window as a percentage, from where the boxes grow. |
| `field.player_y` | setting | number | The player's position in the window as a percentage, from where the boxes grow. |
| `partner_menu.portrait` | report | yes/no | Whether the game draws the partner in 3D over the status page (an improvement). |
| `bag_use` | command | number | Uses the item of a slot. |
| `bag_drop` | command | number | Drops the item of a slot. |
| `bag_move` | command | text | Swaps two slots: "from to". |
| `bag_sort` | command | number | Sorts the bag in an order. |
| `partner_toggle_technique` | command | number | Equips or removes a move by its slot in the species. |
| `partner_praise` | command | — | Praises the partner (the care tab). |
| `partner_scold` | command | — | Scolds the partner (the care tab). |
| `language` | option | text | The language, for the text the page lays out by hand. |
| `status_extras` | option | yes/no | The extras of the status page. |
| `favorite_food` | option | yes/no | The partner's favorite food. |
| `art_fixes` | option | yes/no | Art fixes. |

### `skin_picker`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `ui_skin` | option | text | The skin shown; on confirming a row, the chosen one, which is saved in the settings. |
| `ui_skin_preview` | command | text | Sets the skin of the focused row without saving it. |
| `skin_picker_done` | command | — | The skin is chosen: the title screen goes on to «Press Start» (the language box already appeared before). |

### `story`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `story_textbox.revision` | setting | number | Goes up each time the text box below changes. |
| `story_textbox.open` | setting | yes/no | Whether a text box is open. |
| `story_textbox.kind` | setting | text | speech (the speaker's box) or sized (a box of its own size). |
| `story_textbox.columns` | setting | number | The text columns of the box of its own size. |
| `story_textbox.rows` | setting | number | The text rows of the box of its own size. |
| `story_textbox.opening` | setting | number | How far the box has opened, from 0 to 1; the text shows at 1. |
| `story_textbox.origin_x` | setting | number | Where the box grows from, as a fraction of the window's width; -1 if it appears without growing. |
| `story_textbox.origin_y` | setting | number | Where the box grows from, as a fraction of the window's height; -1 if it appears without growing. |
| `story_textbox.prompt` | setting | yes/no | Whether the page waits for confirmation and shows its prompt. |
| `story_textbox.cursor_row` | setting | number | The row the cursor of a choice is on, from 0; -1 without a choice. |
| `story_textbox.cursor_columns` | setting | number | The characters the bar of a choice spans; -1 without a choice. |
| `story_textbox.row_count` | setting | number | How many rows the page has. |
| `story_textbox.top` | setting | yes/no | Whether the box is at the top of the window instead of the bottom. |
| `story_textbox.row.<n>.runs` | setting | number | How many runs of one color a row has. |
| `story_textbox.row.<n>.run.<n>.text` | setting | text | The text of a run, already in the player's language. |
| `story_textbox.row.<n>.run.<n>.color` | setting | number | The font color of a run, from 0 to 10. |
| `story_textbox.speaker` | setting | text | The speaker's name (also in the first row); empty if nobody speaks. |
| `story_textbox.speaker_kind` | setting | text | Who speaks: none, player, partner, character or special. |
| `story_textbox.options` | setting | number | How many options the visible choice has; 0 without a choice. |
| `story_textbox.first_option` | setting | number | The row of the choice's first option. |
| `story_textbox.input` | setting | text | What the box waits for from the player: page, choice or empty. |
| `story_textbox.page` | setting | number | Counts the pages that have appeared: it changes with each new page. |
| `story_advance` | command | — | Confirms, like the confirm button, while the box waits for the player. |
| `story_choose` | command | number | Picks an option of the visible choice, from 0, like a click on it. |
| `story_cancel` | command | — | Cancels, like the cancel button, while the box waits for the player. |
| `story.input(): string` | query | text | What the box waits for from the player: page (its confirmation), choice (an option) or "" (nothing). |
| `story.textbox(): table` | query | table | The box: open, kind, size (columns, rows), top, opening, origin, prompt, input, page, speaker (name, kind), rows (each with runs: text, color), cursor_row and cursor_columns. |
| `story.choice(): table?` | query | table | The visible choice: options (text and row of each) and cursor (from 0); nil without a choice. |
| `story.advance(): boolean, string?` | action | yes/no | Asks to confirm: the page goes on or the choice takes the cursor's option. |
| `story.choose(option: integer): boolean, string?` | action | yes/no | Asks to pick an option of the choice, from 0. |
| `story.cancel(): boolean, string?` | action | yes/no | Asks to cancel, like the cancel button. |
| `story.changed` | event | table | The box changed: it opened, grew, another page, the cursor. Comes from `story_textbox.revision`. |
| `story.text` | event | table | A new page appears. Comes from `story_textbox.page`. |
| `story.input` | event | table | The box started or stopped waiting for the player. Comes from `story_textbox.input`. |

### `dialog`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `dialog.revision` | setting | number | Goes up each time the dialog below changes. |
| `dialog.text` | setting | text | The text key of what the dialog says. |
| `dialog.file` | setting | text | The save file it talks about, or empty. |
| `dialog.question` | setting | yes/no | Whether it is a question (yes or no); otherwise a notice (accept). |
| `dialog.start_on_yes` | setting | yes/no | Whether the question starts on yes, like the original's new game one. |
| `dialog.at_title` | setting | yes/no | Whether the title screen shows it. |
| `dialog_answer` | command | yes/no | The dialog's answer: true only for yes. |
| `dialog.state(): table` | query | table | The dialog: text_key, file, question, start_on_yes, at_title and options. |
| `dialog.options(): table` | query | table | Its options in order (id, label_key, yes): yes and no in a question, accept in a notice. |
| `dialog.is_open(): boolean` | query | yes/no | Whether the dialog is shown, waiting for its answer. |
| `dialog.answer(option: integer): boolean, string?` | action | yes/no | Answers with one of its options, from 0. |
| `dialog.cancel(): boolean, string?` | action | yes/no | Cancels: no to a question, accept to a notice. |
| `dialog.changed` | event | table | The game set the text of a dialog. Comes from `dialog.revision`. |

### `naming`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `naming_box.revision` | setting | number | Goes up each time the naming box below changes. |
| `naming_box.title` | setting | text | The text key of who is being named. |
| `naming_box.length` | setting | number | The letters the name can have. |
| `naming_box.name.<n>` | setting | text | A letter of the typed name; empty if there is none. |
| `naming_box.rows` | setting | number | The rows of the letter grid. |
| `naming_box.columns` | setting | number | The columns of the letter grid. |
| `naming_box.cell.<n>.<n>` | setting | text | The letter of a grid cell; a space if it is empty. |
| `naming_box.cursor_row` | setting | number | The cursor's row in the grid; -1 on a button. |
| `naming_box.cursor_column` | setting | number | The cursor's column in the grid; -1 on a button. |
| `naming_box.cursor_button` | setting | number | The cursor's button: 0 Back, 1 Accept; -1 in the grid. |
| `naming_write` | command | text | Types the grid letters it carries, like the keyboard. |
| `naming_erase` | command | — | Removes the last letter, like cancel. |
| `naming_accept` | command | — | Accept: the game takes the name if it is not empty. |
| `naming.is_open(): boolean` | query | yes/no | Whether the naming box is shown, waiting for a name. |
| `naming.state(): table` | query | table | The box: title_key, length, letters, name, grid (rows, columns) and cursor (row, column, button). |
| `naming.cell(row: integer, column: integer): string` | query | text | The letter of a grid cell, from 0; " " if it is empty. |
| `naming.write(letters: string): boolean, string?` | action | yes/no | Asks to type grid letters at the end of the name. |
| `naming.erase(): boolean, string?` | action | yes/no | Asks to remove the last letter. |
| `naming.accept(): boolean, string?` | action | yes/no | Asks to accept the name; the game does not take an empty one. |
| `naming.changed` | event | table | The name, the cursor or the grid changed. Comes from `naming_box.revision`. |

### `clock`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `clock.now(): table` | query | table | The time: year, day, hour, minute, night, phase (morning, day, late, night) and running (whether the game's time runs). |
| `clock.changed` | event | table | The field state changed, the time included. Comes from `hud.revision`. |

### `field`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `field.fade(): number` | query | number | How dark the map change's fade is, from 0 to 1. |
| `field.map_title(): string?` | query | text | The text key of the map name shown on arrival, or nil. |
| `field.minimap(): table` | query | table | The minimap: shown (the option and the field allow it), covered (a window covers it) and size. |
| `field.player(): table` | query | table | Where the player is in the window: x and y as a percentage. |
| `field.shop_job(): table?` | query | table | The shop job's tally while it lasts (tally, goal, better_pay, customer, customers, top, bonus, bonus_icon, bonus_icon_rect, tier, improved), or nil. |
| `field.fade` | event | table | The fade changed. Comes from `screen_fade`. |
| `field.map_title` | event | table | The map name shown changed. Comes from `map_title_key`. |
| `field.minimap` | event | table | Whether the minimap can be seen, or whether something covers it, changed. Comes from `hud.minimap_allowed hud.minimap_covered battle.active`. |
| `field.shop_job` | event | table | The shop job changed. Comes from `shop_job.revision`. |

### `partner`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `partner.status(): table` | query | table | The bars (meters: id, label_key, value, min, max, two_tone, mood), the HP and MP (pools) and what it thinks of the map (area: liked, disliked or nil). |
| `partner.needs(): table` | query | table | The needs in order: id, label_key, active and urgency (1 the most urgent; 0 if it does not have it). |
| `partner.bubble(): table` | query | table | Where its head is in the window: visible, x, y (percentage) and evolving (the evolution bubble's box, or nil). |
| `partner.extras(): table?` | query | table | Care mistakes, battles and hours lived; nil if the player has the status extras off. |
| `partner.praise(): boolean` | action | yes/no | Asks to praise it. |
| `partner.scold(): boolean` | action | yes/no | Asks to scold it. |
| `partner.toggle_technique(slot: integer): boolean, string?` | action | yes/no | Asks to equip or remove the move of that slot of the species. |
| `partner.changed` | event | table | Something about the partner changed; name says which setting. Comes from `hud.revision partner.revision hud.meter.0.label hud.meter.1.label hud.pool.0.label hud.pool.1.label hud.need.0.label hud.need.1.label hud.need.2.label hud.need.3.label hud.need.4.label hud.need.5.label`. |
| `partner.bubble_moved` | event | table | Its head moved in the window. Comes from `hud.partner.revision`. |

### `bag`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `bag.count(): integer` | query | number | The bag's slots. |
| `bag.money(): integer` | query | number | The player's money. |
| `bag.field(index: integer, name: string): any` | query | number or text | A piece of data of a slot, or its default value. |
| `bag.slot(index: integer): table` | query | table | A slot: empty, name, description, description_species, count, dimmed, usable, discardable, item_color, icon, icon_rect, favorite. |
| `bag.result(): table?` | query | table | The answer to the last action: key (the message's key) and item; nil before any. |
| `bag.use(index: integer): boolean, string?` | action | yes/no | Asks to use the item of a slot. |
| `bag.drop(index: integer): boolean, string?` | action | yes/no | Asks to drop the item of a slot. |
| `bag.move(from: integer, to: integer): boolean, string?` | action | yes/no | Asks to swap two slots. |
| `bag.sort(order: integer): boolean, string?` | action | yes/no | Asks to sort the bag in an order. |
| `bag.changed` | event | table | The bag changed. Comes from `bag.revision`. |
| `bag.result` | event | table | The game answered a bag action. Comes from `bag.result`. |

### `player`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `player.profile(): table` | query | table | The player: name, level, bits, seconds, raised, medals, items, cards. |
| `player.medals(): table` | query | table | The medals the game publishes (not a fixed number): held and name_key. |
| `player.city(): table` | query | table | The city: recruited, total and prosperity. |

### `options`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `options.list(): table` | query | table | The game's options in order: name, kind (toggle, list, range), label_key, help_key, tab, default, original, values, texts, min, max. |
| `options.get(name: string): any` | query | number or text | An option's value now (or its default value); nil if it does not exist. |
| `options.set(name: string, value: any): boolean, string?` | action | yes/no | Asks to give an option a value; checks the type and the values it accepts. |
| `options.changed` | event | table | An option changed; name says which and value its value. Comes from `@options`. |

### `battle`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `battle.revision` | setting | number | Goes up each time the fighters, the command strip, the finisher or the bag change. |
| `battle.phase` | setting | text | The phase: intro (the opening), fight, victory (the poses), end (the end box) or closing (the strip goes up). |
| `battle.frame_seconds` | setting | number | How long a battle frame lasts, for the pace of the end box. |
| `battle.fast` | setting | yes/no | Whether the battle runs at double speed. |
| `battle.menu.count` | setting | number | How many commands the strip has. |
| `battle.menu.<n>.command` | setting | number | The command's number (1 run... 8 to 10 the moves, 11 the finisher). |
| `battle.menu.<n>.label` | setting | text | The text key of the command's or move's name. |
| `battle.menu.<n>.icon` | setting | text | The command's icon in the skin's sheets; empty for the finisher. |
| `battle.menu.<n>.available` | setting | yes/no | Whether the command can be given now. |
| `battle.menu.<n>.current` | setting | yes/no | Whether it is the command the partner follows now. |
| `battle.menu.<n>.mp_cost` | setting | number | The MP the move costs the partner now; 0 for the other commands. |
| `battle.menu.motion` | setting | number | Goes up each time the strip or the partner's bars move, or the icon under the cursor blinks. |
| `battle.menu.top` | setting | number | The strip's top edge, in lines from the screen's center. |
| `battle.menu.bottom` | setting | number | The strip's bottom edge. |
| `battle.menu.blink` | setting | yes/no | Whether the icon under the cursor shows its second frame. |
| `battle.bars.hp_x` | setting | number | The left edge of the partner's HP bar while it enters or leaves. |
| `battle.bars.mp_x` | setting | number | The left edge of the partner's MP bar while it enters or leaves. |
| `battle.fighter.count` | setting | number | How many fighters there are: the partner (0) and the enemies. |
| `battle.fighter.<n>.name` | setting | text | The text key of its name. |
| `battle.fighter.<n>.status` | setting | text | The text key of its status ailment, or empty. |
| `battle.fighter.<n>.hp` | setting | number | A fighter's HP. |
| `battle.fighter.<n>.max_hp` | setting | number | A fighter's maximum HP. |
| `battle.fighter.<n>.mp` | setting | number | A fighter's MP. |
| `battle.fighter.<n>.max_mp` | setting | number | A fighter's maximum MP. |
| `battle.fighter.<n>.fainted` | setting | yes/no | Whether it has fainted. |
| `battle.fighter.<n>.hp_bar` | setting | yes/no | Whether its HP bar is shown now (an enemy's, for a while after taking damage). |
| `battle.finisher.progress` | setting | number | How much the finisher gauge has charged. |
| `battle.finisher.goal` | setting | number | How much it has to charge to be full. |
| `battle.finisher.ready` | setting | yes/no | Whether the finisher is ready. |
| `battle.finisher.charging` | setting | yes/no | Whether the aura, in which each press charges the finisher, lasts. |
| `battle.finisher.charge` | setting | number | The aura's charge, as a percentage. |
| `battle.revive_seconds` | setting | number | The countdown to revive the fainted partner; -1 without one. |
| `battle.match_seconds` | setting | number | The clock of a cup battle; -1 in the other battles. |
| `battle.bag.available` | setting | yes/no | Whether the bag can be opened. |
| `battle.bag.open` | setting | yes/no | Whether the bag is open over the battle. |
| `battle.item_in_flight` | setting | yes/no | Whether a thrown item is on its way to the partner. |
| `battle.end.outcome` | setting | text | The text key of the result in the end box. |
| `battle.end.count` | setting | number | How many lines the end box has. |
| `battle.end.<n>.parts` | setting | text | The pieces of a line, separated by |: text keys or marks in braces. |
| `battle.end.<n>.name` | setting | text | The text key of the line's name ({name}). |
| `battle.end.<n>.subject` | setting | text | The text key of who it refers to ({subject}). |
| `battle.end.<n>.amount` | setting | number | The line's number ({amount}). |
| `battle.end.stat.<n>.value` | setting | number | A stat before the gain (HP, MP, attack, defense, speed, brain). |
| `battle.end.stat.<n>.max` | setting | number | That stat's cap. |
| `battle.end.gain.<n>.value` | setting | number | How much that stat goes up. |
| `battle.end.bits` | setting | number | The bits earned. |
| `battle.end.money` | setting | number | The money with the bits already in. |
| `battle.end.count_delay_frames` | setting | number | The frames before the stats start to go up. |
| `battle.end.wait_frames` | setting | number | The frames each line of the end box waits. |
| `battle.intro.revision` | setting | number | Goes up each time the battle's opening changes. |
| `battle.intro.step` | setting | text | The opening's step: anger, banner, burst or done. |
| `battle.intro.anger.<n>.shown` | setting | yes/no | Whether the anger mark over an enemy is shown. |
| `battle.intro.anger.<n>.x` | setting | number | Where that mark is, as a percentage of the width. |
| `battle.intro.anger.<n>.y` | setting | number | Where that mark is, as a percentage of the height. |
| `battle.intro.anger.scale` | setting | number | The size of the marks, as a percentage. |
| `battle.intro.flee.<n>.shown` | setting | yes/no | Whether the mark over a fleeing character is shown. |
| `battle.intro.flee.<n>.x` | setting | number | Where it is, as a percentage of the width. |
| `battle.intro.flee.<n>.y` | setting | number | Where it is, as a percentage of the height. |
| `battle.intro.flash` | setting | number | The banner's flash, from 0 to 2. |
| `battle.intro.pieces` | setting | text | The banner's pieces: seven numbers each (x, y, width, height, texture, trail, left). |
| `battle.damage.revision` | setting | number | Goes up each time the numbers over the fighters change. |
| `battle.damage.numbers` | setting | text | The numbers over the fighters: seven values each (x, y, dx, dy, value, color, icon). |
| `battle.hp_bars.revision` | setting | number | Goes up each time the enemies' health bars, the markers or the fainted partner's countdown change. |
| `battle.hp_bars.always` | setting | yes/no | Whether the enemies' health bars are shown for the whole battle. |
| `battle.hp_bars` | setting | text | The enemies' health bars: nine values each. |
| `battle.markers` | setting | text | The 1P and COM markers of a cup battle between two of the same species: three values each. |
| `battle.death_countdown` | setting | text | The countdown over the fainted partner: seven values, or empty. |
| `battle.shout.revision` | setting | number | Goes up each time the player's shout changes. |
| `battle.shout.shown` | setting | yes/no | Whether the player's shout is shown. |
| `battle.shout.text` | setting | text | The text key of the shout. |
| `battle.shout.anchor_x` | setting | number | Where the shout is anchored, as a percentage of the width. |
| `battle.shout.anchor_y` | setting | number | Where the shout is anchored, as a percentage of the height. |
| `battle.shout.node_x` | setting | number | The partner's point from the screen's center. |
| `battle.shout.node_y` | setting | number | The partner's point from the screen's center. |
| `battle.shout.y` | setting | number | The shout's line. |
| `battle.shout.hop` | setting | number | The shout's hop. |
| `battle.shout.scale` | setting | number | The shout's size. |
| `battle_command` | command | number | Gives the partner a command (its number, battle.menu.&lt;n&gt;.command). For an addon, only in the fight phase. |
| `battle_bag_open` | command | — | Opens the player's bag. For an addon, only in the fight phase. |
| `battle_bag_close` | command | — | Closes the player's bag and returns to the battle. For an addon, only with the bag open. |
| `battle_charge` | command | — | A press of the finisher's charge. For an addon, only in the fight phase. |
| `battle_close` | command | — | Closes the battle's end box. For an addon, only in the end phase. |
| `battle_end_count` | command | — | A step of the count of the bits at the battle's end. For an addon, only in the end phase. |
| `battle_end_counted` | command | — | The count of the bits at the battle's end has finished. For an addon, only in the end phase. |
| `battle.state(): table` | query | table | The battle: phase, active, fast, frame_seconds, bag (available, open, item_in_flight), revive_seconds and match_seconds (or nil) and cup (a cup battle). |
| `battle.fighters(): table` | query | table | The fighters, the partner first: name_key, status_key, hp, max_hp, mp, max_mp, fainted and hp_bar. |
| `battle.commands(): table` | query | table | The command strip, the finisher first: command, label_key, icon, available, current and mp_cost. |
| `battle.finisher(): table` | query | table | The finisher: progress, goal, ready, charging (the aura) and charge (as a percentage). |
| `battle.strip(): table` | query | table | Where the strip and the partner's bars are when entering and leaving: top, bottom, blink, hp_x and mp_x (or nil). |
| `battle.intro(): table` | query | table | The opening: step, anger (3 marks), flee (8), anger_scale, flash and pieces (the banner's pieces, with key). |
| `battle.numbers(): table` | query | table | The numbers over the fighters: x, y (percentage), dx, dy, value, kind (damage, gain, poison) and icon. |
| `battle.hp_bars(): table` | query | table | The enemies' health bars: always and bars (enemy, x, y, left, top, fill, red, green, hp). |
| `battle.markers(): table` | query | table | The 1P and COM markers: fighter (0 the partner, 1 the rival), x and y (percentage). |
| `battle.death_countdown(): table?` | query | table | The countdown over the fainted partner (x, y, digit, digit_scale, badge_scale, turn, palette), or nil. |
| `battle.shout(): table?` | query | table | The player's shout (text_key, x, y, node_x, node_y, line, hop, scale), or nil. |
| `battle.result(): table` | query | table | The end box: outcome_key, lines (parts, name_key, subject_key, amount), stats (value, max, gain), bits, money, count_delay_frames and wait_frames. |
| `battle.match(): table` | query | table | What a cup battle shows on top: shown, name, name_y, stats, counted, stat, time_up, time_up_label, box, partner, rival, damage_label, dealt, taken and wipe. |
| `battle.command(command: integer): boolean, string?` | action | yes/no | Asks the partner to follow a command of the strip (its command); only in the fight phase. |
| `battle.charge(): boolean, string?` | action | yes/no | A press of the finisher's charge; only in the fight phase. |
| `battle.open_bag(): boolean, string?` | action | yes/no | Asks to open the bag over the battle (the partner_menu screen); only in the fight phase. |
| `battle.close_bag(): boolean, string?` | action | yes/no | Asks to close the bag and return to the battle. |
| `battle.close(): boolean, string?` | action | yes/no | The end box has finished: the battle closes; only in the end phase. |
| `battle.end_count(): boolean, string?` | action | yes/no | A point or a bit counted in the end box (its sound); only in the end phase. |
| `battle.end_counted(): boolean, string?` | action | yes/no | All the bits in (the sound that ends the count); only in the end phase. |
| `battle.started` | event | table | A battle starts (battle.active becomes true). Comes from `battle.active`. |
| `battle.ended` | event | table | A battle ends, after the end box closes (battle.active becomes false). Comes from `battle.active`. |
| `battle.phase` | event | table | The phase changed; value says which. Comes from `battle.phase`. |
| `battle.changed` | event | table | The fighters, the strip, the finisher, the bag or the counts changed. Comes from `battle.revision`. |
| `battle.speed` | event | table | The battle switched to fast or to normal speed. Comes from `battle.fast`. |
| `battle.strip_moved` | event | table | The strip or the partner's bars moved, or the icon under the cursor blinked. Comes from `battle.menu.motion`. |
| `battle.intro` | event | table | The opening changed. Comes from `battle.intro.revision`. |
| `battle.numbers` | event | table | The numbers over the fighters changed. Comes from `battle.damage.revision`. |
| `battle.overlays` | event | table | The enemies' health bars, the 1P and COM markers or the fainted partner's countdown changed. Comes from `battle.hp_bars.revision battle.hp_bars.always`. |
| `battle.shout` | event | table | The player's shout changed. Comes from `battle.shout.revision`. |
| `battle.match` | event | table | What a cup battle shows on top changed. Comes from `tournament_match.revision`. |

### `tournament_match`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `tournament_match.revision` | setting | number | Goes up each time what is below changes. |
| `tournament_match.shown` | setting | yes/no | Whether it is shown. |
| `tournament_match.name` | setting | text | The name of the fighter in the introduction, as much of it as has come in (already in the language). |
| `tournament_match.name_y` | setting | number | The name's row. |
| `tournament_match.stats` | setting | yes/no | Whether the stats box is shown. |
| `tournament_match.counted` | setting | yes/no | Whether its numbers are shown. |
| `tournament_match.stat.<n>.label` | setting | text | The name of a stat. |
| `tournament_match.stat.<n>.value` | setting | number | The value of a stat. |
| `tournament_match.stat.<n>.width` | setting | number | The width of its bar. |
| `tournament_match.time_up` | setting | yes/no | Whether the time-up notice is shown. |
| `tournament_match.time_up_label` | setting | text | The text of that notice. |
| `tournament_match.box` | setting | yes/no | Whether the damage box is shown. |
| `tournament_match.partner` | setting | text | The partner's name. |
| `tournament_match.rival` | setting | text | The rival's name. |
| `tournament_match.damage_label` | setting | text | The title of the damage box. |
| `tournament_match.dealt` | setting | number | The damage dealt. |
| `tournament_match.taken` | setting | number | The damage taken. |
| `tournament_match.wipe.<n>` | setting | number | The height of one of the curtain's 32 columns, from 0 to 240. |

### `versus`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `versus.revision` | setting | number | Goes up each time the versus changes. |
| `versus.view` | setting | text | The view: menu, host_setup, hosting, join_entry, joining, save_pick, lineup, waiting_lineup, match, intermission, result or error. |
| `versus.cursor` | setting | number | The row with the focus, or in a battle the command pointed at. |
| `versus.battles` | setting | number | How many battles the series has. |
| `versus.choice` | setting | number | The row of the chosen series length. |
| `versus.choices` | setting | number | How many series lengths there are. |
| `versus.arena` | setting | number | The number of the chosen arena (its text is versus.arena.&lt;n&gt;). |
| `versus.arena_choice` | setting | number | The row of the chosen arena. |
| `versus.arenas` | setting | number | How many arenas there are. |
| `versus.code` | setting | text | The room code; empty until there is one. |
| `versus.stage` | setting | text | How the connection stands: creating, waiting or connecting. |
| `versus.typed` | setting | text | The code typed so far. |
| `versus.bad_entry` | setting | yes/no | Whether the last code was not valid. |
| `versus.saves` | setting | number | How many saves there are to choose from. |
| `versus.save.<n>.player` | setting | text | The name of the save's player. |
| `versus.save.<n>.nickname` | setting | text | The partner's name. |
| `versus.save.<n>.partners` | setting | number | How many partners it has. |
| `versus.save.<n>.usable` | setting | yes/no | Whether it has partners for the whole series. |
| `versus.partners` | setting | number | How many partners there are to choose from. |
| `versus.partner.<n>.name` | setting | text | The partner's name. |
| `versus.partner.<n>.species` | setting | text | The text key of its species. |
| `versus.partner.<n>.stats` | setting | text | Its stats in a text. |
| `versus.partner.<n>.picked` | setting | yes/no | Whether it is already chosen. |
| `versus.picks` | setting | number | How many partners are chosen. |
| `versus.pick.<n>.name` | setting | text | The name of a chosen one, in order. |
| `versus.local` | setting | number | The player's side: 0 the host's, 1 the guest's. |
| `versus.round` | setting | number | The battle being shown, from 1. |
| `versus.wins.<n>` | setting | number | The wins of each side (0 and 1). |
| `versus.clock` | setting | number | The seconds on the battle's clock. |
| `versus.stalled` | setting | yes/no | Whether the rival has sent nothing for a while. |
| `versus.leave_asked` | setting | yes/no | Whether another cancel leaves the versus. |
| `versus.blink` | setting | yes/no | Whether the icon pointed at shows its second frame. |
| `versus.f.<n>.hp` | setting | number | The HP of a side (0 and 1). |
| `versus.f.<n>.max_hp` | setting | number | Its maximum HP. |
| `versus.f.<n>.mp` | setting | number | Its MP. |
| `versus.f.<n>.max_mp` | setting | number | Its maximum MP. |
| `versus.f.<n>.segments` | setting | number | The lit letters of the finisher, from 0 to 6. |
| `versus.f.<n>.finisher_ready` | setting | yes/no | Whether the finisher is ready. |
| `versus.f.<n>.menu` | setting | number | How many commands its strip has. |
| `versus.f.<n>.hover` | setting | number | The command pointed at in its strip (the rival's, its current command). |
| `versus.f.<n>.menu.<n>.icon` | setting | text | The icon of a strip command; empty for the finisher. |
| `versus.f.<n>.command_name` | setting | text | The text key of the command pointed at, or empty. |
| `versus.log` | setting | number | How many lines what has happened has. |
| `versus.log.<n>.key` | setting | text | The text key of the line. |
| `versus.log.<n>.who` | setting | text | Who. |
| `versus.log.<n>.what` | setting | text | The text key of the move, or empty. |
| `versus.log.<n>.amount` | setting | number | How much. |
| `versus.results` | setting | number | How many battles have been played. |
| `versus.result.<n>.outcome` | setting | text | How the player did: won, lost or draw. |
| `versus.winner` | setting | text | Who won the series at the end: you, rival or draw. |
| `versus.error_key` | setting | text | The text key of why it stopped. |
| `versus.error_detail` | setting | text | The error in its own words. |
| `versus.show` | setting | text | What the arena shows: banner, fight, ending, score or nothing; not present without an arena. |
| `versus.score.round` | setting | number | The row of the score screen that moves. |
| `versus.score.offset` | setting | number | How far it has moved, in the original's pixels. |
| `versus.score.flash` | setting | yes/no | Whether the row's flash is shown. |
| `versus.score.black` | setting | number | The opacity of the fade to black, from 0 to 1. |
| `versus.score.results` | setting | number | How many battles have a result. |
| `versus.score.result.<n>` | setting | text | Who won a battle: host, guest or draw. |
| `versus.score.wins.<n>` | setting | number | The wins of each side. |
| `versus.score.losses.<n>` | setting | number | The losses of each side. |
| `versus.score.face.<n>.<n>` | setting | number | The face of a side's fighter in a battle (by its species' number). |
| `versus_action` | command | text | A step in the view: up, down, left, right, confirm, cancel, menu, tab_previous, tab_next, finisher, or pick:&lt;row&gt; to go to a row and confirm it. |
| `versus.state(): table` | query | table | The view, the cursor, the series' battles and, in a battle: round, clock, side (the player's side), wins, stalled, leave_asked, blink and arena (what the arena shows, or nil without one). |
| `versus.setup(): table` | query | table | The host's setup: series and series_count, arena, arena_row and arenas. |
| `versus.connection(): table` | query | table | The connection: stage (creating, waiting, connecting) and code (the room code). |
| `versus.entry(): table` | query | table | The code typed (typed) and whether the last one was not valid (bad). |
| `versus.saves(): table` | query | table | The saves: player, nickname, partners and usable. |
| `versus.lineup(): table` | query | table | The partners (name, species_key, stats, picked) and the chosen ones (picks, their names). |
| `versus.fighters(): table` | query | table | The two sides, the host first: hp, max_hp, mp, max_mp, segments, finisher_ready, hover, icons (the strip) and command_key (the command pointed at). |
| `versus.log(): table` | query | table | What has happened, the latest last: key, who, what_key and amount. |
| `versus.results(): table` | query | table | The battles played (rounds: won, lost, draw) and the series' winner (winner). |
| `versus.score(): table` | query | table | The score screen: round, offset, flash, black, results and sides (wins, losses, faces). |
| `versus.error(): table` | query | table | Why it stopped: key (text key) and detail. |
| `versus.step(step: string): boolean, string?` | action | yes/no | A step in the view: up, down, left, right, confirm, cancel, menu, tab_previous, tab_next or finisher (the finisher, with its meter full). |
| `versus.pick(row: integer): boolean, string?` | action | yes/no | Goes to a row of the view (or to a command of the player's strip) and confirms it. |
| `versus.changed` | event | table | The versus changed. Comes from `versus.revision`. |

<!-- /ui-api:generated -->
