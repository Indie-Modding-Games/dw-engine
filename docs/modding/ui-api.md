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
| 0.27.0 | The room shows the host's co-op difficulty before a guest readies (`online.current.coop_difficulty`, `online.room()` `coop_difficulty` and `coop_difficulty_key`), and the `coop_open_to_join` option arrives: whether a co-op game the player hosts takes new players once under way, applied at once to the room it hosts |
| 0.26.0 | Nine languages: the naming box turns pages of letters (`naming_box.pages`, `naming_box.page`, `naming_box.page_name`, `page` in `naming.state()`), and `ui.upper` and `ui.lower` know accented, Greek and Cyrillic letters |
| 0.25.0 | The co-op tag says when a player trains at the gym (`coop_hud.tag.training`, `coop_hud.tag.<n>.training`) |
| 0.24.0 | A game in progress takes players: the room list shows a game under way that has a seat as `in_progress` (`online.list()` rooms gain `in_progress`), the room says whether its game is under way and whether it takes new players (`online.current.in_progress`, `online.current.open_to_join`, `online.room()` `in_progress` and `open_to_join`), and the host opens or closes it to new players (`online.set_open_to_join`, `online_open_to_join`) |
| 0.23.0 | Every partner fights in a co-op battle: an ally's card counts down its revive (`battle.ally.<n>.revive_seconds`, `revive_seconds` in `battle.allies()`), and the `coop_battle` screen shows each fallen partner's countdown (`coop_battle.fighter.<n>.revive_seconds`) and lets the player pick the partner its items fly to (`coop_battle.target`, the `coop_battle_target` command). The `coop_difficulty` option arrives (off, standard or hard) |
| 0.22.0 | The `coop_trade` screen trades items between two co-op players: the phase (`coop_trade.phase`), the other player (`coop_trade.other.*`), this player's bag (`coop_trade.bag.<n>.*`), both offers (`coop_trade.own.<n>.*`, `coop_trade.their.<n>.*`) and both ready marks, with the commands `coop_trade_answer`, `coop_trade_add`, `coop_trade_take`, `coop_trade_confirm` and `coop_trade_cancel`. The `coop_hud` tag says when the other player can be asked (`coop_hud.tag.trade`) |
| 0.21.0 | Rooms of up to four players: each member says when its seat is taken while its connection is still being set up (`online.member.<n>.arriving`), and the co-op HUD shows a tag over each of the other players (`coop_hud.tag.<n>.*` for the second and third) |
| 0.20.0 | Both partners fight in a co-op battle: the battle screen lists the other players' partners fighting beside this player's (`battle.ally.<n>.*`, `battle.allies()`), and its partner, strip, finisher and bag are this player's own. The `coop_battle` screen gains the role `ally`, the fighters' MP, side and whether one is this player's (`coop_battle.fighter.<n>.mp`, `.max_mp`, `.side`, `.mine`); orders and items go to this player's own partner |
| 0.19.0 | The `coop_battle` screen shows the host's battle on a co-op guest's screen: its role (`coop_battle.role`), phase, the fighters' life (`coop_battle.fighter.<n>.*`), the starter's orders (`coop_battle.order.<n>.*`) and the items of its own bag it can throw (`coop_battle.item.<n>.*`), with the commands `coop_battle_order`, `coop_battle_cheer`, `coop_battle_item` and `coop_battle_close` |
| 0.18.0 | Rooms show real data: `online.room()` follows the room's phase as the network keeps it (open, connected, ready, playing, closed), and a guest sees the host's name. Listed rooms carry the host's name and mode, rooms already playing leave the list, and the list can say it was refreshed too often (`list_limit`). The online profile's relay only (`online.profile().relay_only`, `online.set_relay_only(on)`, command `online_profile_relay`) connects rooms through the server's relay so the other player never learns this machine's address. New errors: `server_refused`, `no_relay`, `list_limit` (online) and `versus.error.refused`, `versus.error.no_relay` |
| 0.17.0 | The `online` namespace arrives: the public room list (`online.list()`), the room the player is in (`online.room()`), the modes (`online.modes()`) and the screens' state (`online.state()`), with their steps (`online.host`, `online.join`, `online.join_by_code`, `online.quick_match`, `online.set_ready`, `online.start`, `online.leave`...) and the `online.changed` event. The room steps are the co-op lobby's commands (`coop_host`, `coop_join` with a code, `coop_ready`, `coop_start`). The player's online profile (`online.profile()`, `online.icons()`, `online.save_name`, `online.set_icon`) and `online.retry` after a failure. The new screens `online_hub`, `online_profile`, `online_status`, `online_rooms` and `online_room` only read through it. The `coop_hud` screen shows the co-op game's tag over the other player (`coop_hud.tag.*`) and its notices (`coop_hud.notice.*`) |
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
versus, the online rooms, the co-op game's tag and notices and the story boxes (the text box, the save dialog and the name box); the rest of the screens comes later.

## Reference

<!-- ui-api:generated -->
UI API version: **0.27.0**.

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
| `ui.upper(text: string): string` | function | text | The text in capitals, accented, Greek and Cyrillic letters too (`string.upper` only knows ASCII). |
| `ui.lower(text: string): string` | function | text | The text in small letters, in the same alphabets. |
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
| `coop_difficulty` | option | text | How co-op battles scale their enemies: off, standard or hard. |
| `coop_open_to_join` | option | yes/no | Whether a co-op game this player hosts takes new players once under way. |
| `skip_evolution` | option | yes/no | Skip the evolution scene. |
| `choice_cancel_closes` | option | yes/no | Cancel closes a choice. |
| `fair_sickness_chance` | option | yes/no | Fair chance of falling sick. |
| `fair_evolution_average` | option | yes/no | Fair evolution average. |
| `fair_move_forgetting` | option | yes/no | Fair move forgetting. |
| `fair_card_stock` | option | yes/no | Fair card stock. |
| `choose_egg_after_lives_lost` | option | yes/no | Choose the egg when the lives are lost. |
| `generous_talk` | option | yes/no | Generous talk. |
| `extra_toilets` | option | yes/no | Extra toilets. |
| `angemon_event_zone` | option | yes/no | Angemon's event zone. |
| `whamon_in_water` | option | yes/no | Whamon in the water. |
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

### `coop_hud`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `coop_hud.tag.visible` | setting | yes/no | Whether the other co-op player's name shows over their head (they are in the window and the field runs). |
| `coop_hud.tag.x` | setting | number | Where the point over the other player's head is, in percent of the window's width. |
| `coop_hud.tag.y` | setting | number | Where the point over the other player's head is, in percent of the window's height. |
| `coop_hud.tag.name` | setting | text | The other player's own name; empty when they are named by a text key. |
| `coop_hud.tag.name_key` | setting | text | The text key that names the other player without a name of their own (the host, or a guest by number). |
| `coop_hud.tag.number` | setting | number | The number that fills {number} in the name's text key. |
| `coop_hud.tag.revision` | setting | number | Goes up each time the tag's values change: the tag is drawn again. |
| `coop_hud.tag.trade` | setting | yes/no | Whether the other player can be asked for a trade now (the trade key's hint under the name). |
| `coop_hud.tag.training` | setting | yes/no | Whether the other player's partner is training at the gym now (a line under the name). |
| `coop_hud.tag.<n>.visible` | setting | yes/no | In a room of more than two, whether the tag over another player shows (n 2 and 3; coop_hud.tag.revision redraws them too). |
| `coop_hud.tag.<n>.x` | setting | number | Where that tag's anchor is across the window, in percent. |
| `coop_hud.tag.<n>.y` | setting | number | Where that tag's anchor is down the window, in percent. |
| `coop_hud.tag.<n>.name` | setting | text | That player's own name, or empty. |
| `coop_hud.tag.<n>.name_key` | setting | text | The text key that names that player when it has no name of its own. |
| `coop_hud.tag.<n>.number` | setting | number | The number that fills {number} in that name's text key. |
| `coop_hud.tag.<n>.trade` | setting | yes/no | Whether that player can be asked for a trade now (the trade key's hint under the name). |
| `coop_hud.tag.<n>.training` | setting | yes/no | Whether that player's partner is training at the gym now. |
| `coop_hud.notice.shown` | setting | yes/no | Whether a co-op notice shows (a few seconds each). |
| `coop_hud.notice.text` | setting | text | The notice's text key; {name} in it stands for the player it is about. |
| `coop_hud.notice.name` | setting | text | The own name of the player the notice is about; empty when named by a text key. |
| `coop_hud.notice.name_key` | setting | text | The text key that names the player the notice is about, if they have no name. |
| `coop_hud.notice.number` | setting | number | The number that fills {number} in that text key. |
| `coop_hud.notice.revision` | setting | number | Goes up with each notice and when it goes: the notice is drawn again. |

### `coop_battle`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `coop_battle.shown` | setting | yes/no | Whether the host's battle shows on this co-op guest's screen. |
| `coop_battle.role` | setting | text | This player's part in it: starter (their partner fights and takes their orders), ally (their partner fights beside the starter's and takes their orders) or helper (they help with items and cheers). |
| `coop_battle.phase` | setting | text | Where the battle is: intro, fight, victory, end (the end box) or closing. |
| `coop_battle.command` | setting | number | The order this player's partner follows now (the fighting one's for a helper; a command's number). |
| `coop_battle.bag` | setting | yes/no | Whether an item can be thrown now. |
| `coop_battle.fighter_count` | setting | number | How many fighters there are: the partner first, then the enemies, then the allies. |
| `coop_battle.fighter.<n>.name_key` | setting | text | The text key of a fighter's species name. |
| `coop_battle.fighter.<n>.hp` | setting | number | A fighter's HP now. |
| `coop_battle.fighter.<n>.max_hp` | setting | number | A fighter's most HP. |
| `coop_battle.fighter.<n>.down` | setting | yes/no | Whether a fighter is beaten. |
| `coop_battle.fighter.<n>.mp` | setting | number | A fighter's MP now. |
| `coop_battle.fighter.<n>.max_mp` | setting | number | A fighter's most MP. |
| `coop_battle.fighter.<n>.side` | setting | text | Whose side a fighter is on: partner (a player's partner) or enemy. |
| `coop_battle.fighter.<n>.mine` | setting | yes/no | Whether a fighter is this player's own partner. |
| `coop_battle.fighter.<n>.revive_seconds` | setting | number | A fallen partner's revive countdown in seconds; -1 without one. |
| `coop_battle.target` | setting | number | The fighter this player's items fly to (a partner's number, picked with coop_battle_target); -1 for its own partner (the fighting one for a helper). |
| `coop_battle.order_count` | setting | number | How many orders this player can give its partner now (none for the helper). |
| `coop_battle.order.<n>.command` | setting | number | An order's command number, sent with coop_battle_order. |
| `coop_battle.order.<n>.label_key` | setting | text | The text key of an order's name. |
| `coop_battle.item_count` | setting | number | How many kinds of items of this player's bag can be thrown in a battle. |
| `coop_battle.item.<n>.id` | setting | text | An item's id, sent with coop_battle_item. |
| `coop_battle.item.<n>.name_key` | setting | text | The text key of an item's name. |
| `coop_battle.item.<n>.count` | setting | number | How many of an item this player has. |
| `coop_battle.revision` | setting | number | Goes up each time the values above change: the screen is drawn again. |
| `coop_battle_order` | command | number | This player's order for its own partner (a command's number). |
| `coop_battle_cheer` | command | — | A cheer: charges the finisher as the battle's charge buttons do. |
| `coop_battle_item` | command | text | Throws one of an item (its id) from this player's bag at the partner coop_battle.target names, else at its own partner (the fighting one for a helper). |
| `coop_battle_target` | command | number | Picks the partner this player's items fly to (a fighter's number); the same again picks none. |
| `coop_battle_close` | command | — | The starter closes the end box. |

### `coop_trade`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `coop_trade.shown` | setting | yes/no | Whether an item trade with another co-op player shows (an ask or the trade itself). |
| `coop_trade.phase` | setting | text | Where the trade is: asking (this player waits for the answer), invited (the other player asks this one), open (both choose) or executing (both are ready: the items change hands). |
| `coop_trade.other.name` | setting | text | The other player's own name; empty when named by a text key. |
| `coop_trade.other.name_key` | setting | text | The text key that names the other player without a name of their own. |
| `coop_trade.other.number` | setting | number | The number that fills {number} in that text key. |
| `coop_trade.bag_count` | setting | number | How many kinds of items this player's bag holds. |
| `coop_trade.bag.<n>.id` | setting | text | A bag item's id, sent with coop_trade_add. |
| `coop_trade.bag.<n>.name_key` | setting | text | The text key of a bag item's name. |
| `coop_trade.bag.<n>.count` | setting | number | How many of a bag item are left out of the offer. |
| `coop_trade.bag.<n>.offered` | setting | number | How many of a bag item this player offers. |
| `coop_trade.bag.<n>.tradeable` | setting | yes/no | Whether a bag item can be given (what can be thrown away). |
| `coop_trade.bag.<n>.item_color` | setting | number | A bag item's use: 0 field, 1 battle, 2 both, 255 none. |
| `coop_trade.bag.<n>.icon` | setting | text | A bag item's icon image. |
| `coop_trade.bag.<n>.icon_rect` | setting | text | The icon's part of that image. |
| `coop_trade.own_count` | setting | number | How many kinds of items this player offers. |
| `coop_trade.own.<n>.id` | setting | text | An offered item's id, sent with coop_trade_take. |
| `coop_trade.own.<n>.name_key` | setting | text | The text key of an offered item's name. |
| `coop_trade.own.<n>.count` | setting | number | How many of an item this player offers. |
| `coop_trade.own.<n>.item_color` | setting | number | An offered item's use: 0 field, 1 battle, 2 both, 255 none. |
| `coop_trade.own.<n>.icon` | setting | text | An offered item's icon image. |
| `coop_trade.own.<n>.icon_rect` | setting | text | The icon's part of that image. |
| `coop_trade.their_count` | setting | number | How many kinds of items the other player offers. |
| `coop_trade.their.<n>.id` | setting | text | An item the other player offers: its id. |
| `coop_trade.their.<n>.name_key` | setting | text | The text key of that item's name. |
| `coop_trade.their.<n>.count` | setting | number | How many of it the other player offers. |
| `coop_trade.their.<n>.item_color` | setting | number | That item's use: 0 field, 1 battle, 2 both, 255 none. |
| `coop_trade.their.<n>.icon` | setting | text | That item's icon image. |
| `coop_trade.their.<n>.icon_rect` | setting | text | The icon's part of that image. |
| `coop_trade.own_confirmed` | setting | yes/no | Whether this player is ready to trade what is on the table. |
| `coop_trade.their_confirmed` | setting | yes/no | Whether the other player is ready; any change of an offer takes both back. |
| `coop_trade.can_confirm` | setting | yes/no | Whether this player can say ready now (something is on the table and its own last change reached the host). |
| `coop_trade.revision` | setting | number | Goes up each time the values above change: the screen is drawn again. |
| `coop_trade_answer` | command | yes/no | The answer to a trade ask: true accepts. |
| `coop_trade_add` | command | text | Puts one more of a bag item (its id) on the table. |
| `coop_trade_take` | command | text | Takes one of an offered item (its id) back. |
| `coop_trade_confirm` | command | yes/no | Says ready (true) or takes it back (false). |
| `coop_trade_cancel` | command | — | Calls the trade (or this player's ask) off. |

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
| `naming_box.pages` | setting | number | The pages of letters the player's language has (its own, then the Latin one). |
| `naming_box.page` | setting | number | The page of letters shown, from 0. |
| `naming_box.page_name` | setting | text | The text key of the shown page's name. |
| `naming_write` | command | text | Types the grid letters it carries, like the keyboard. |
| `naming_erase` | command | — | Removes the last letter, like cancel. |
| `naming_accept` | command | — | Accept: the game takes the name if it is not empty. |
| `naming.is_open(): boolean` | query | yes/no | Whether the naming box is shown, waiting for a name. |
| `naming.state(): table` | query | table | The box: title_key, length, letters, name, grid (rows, columns), cursor (row, column, button) and page (index, count, name_key). |
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
| `battle.fighter.count` | setting | number | How many fighters there are: the partner (0, in co-op this player's own) and the enemies. |
| `battle.fighter.<n>.name` | setting | text | The text key of its name. |
| `battle.fighter.<n>.status` | setting | text | The text key of its status ailment, or empty. |
| `battle.fighter.<n>.hp` | setting | number | A fighter's HP. |
| `battle.fighter.<n>.max_hp` | setting | number | A fighter's maximum HP. |
| `battle.fighter.<n>.mp` | setting | number | A fighter's MP. |
| `battle.fighter.<n>.max_mp` | setting | number | A fighter's maximum MP. |
| `battle.fighter.<n>.fainted` | setting | yes/no | Whether it has fainted. |
| `battle.fighter.<n>.hp_bar` | setting | yes/no | Whether its HP bar is shown now (an enemy's, for a while after taking damage). |
| `battle.ally.count` | setting | number | How many other players' partners fight beside the partner (co-op; 0 otherwise). |
| `battle.ally.<n>.name` | setting | text | The text key of an ally's name. |
| `battle.ally.<n>.status` | setting | text | The text key of an ally's status ailment, or empty. |
| `battle.ally.<n>.hp` | setting | number | An ally's HP. |
| `battle.ally.<n>.max_hp` | setting | number | An ally's maximum HP. |
| `battle.ally.<n>.mp` | setting | number | An ally's MP. |
| `battle.ally.<n>.max_mp` | setting | number | An ally's maximum MP. |
| `battle.ally.<n>.fainted` | setting | yes/no | Whether an ally has fainted. |
| `battle.ally.<n>.hp_bar` | setting | yes/no | Always on for an ally. |
| `battle.ally.<n>.revive_seconds` | setting | number | The countdown to revive a fainted ally; -1 without one. |
| `battle.finisher.progress` | setting | number | How much the finisher gauge has charged. |
| `battle.finisher.goal` | setting | number | How much it has to charge to be full. |
| `battle.finisher.ready` | setting | yes/no | Whether the finisher is ready. |
| `battle.finisher.charging` | setting | yes/no | Whether the aura, in which each press charges the finisher, lasts. |
| `battle.finisher.charge` | setting | number | The aura's charge, as a percentage. |
| `battle.finisher.gauge` | setting | text | The charge gauge over the partner during the aura: its top-left corner (x, y as percentages), or empty. |
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
| `battle.allies(): table` | query | table | The other players' partners fighting beside this player's in a co-op battle (empty otherwise): name_key, status_key, hp, max_hp, mp, max_mp, fainted and revive_seconds (a fallen ally's countdown, or nil). |
| `battle.commands(): table` | query | table | The command strip, the finisher first: command, label_key, icon, available, current and mp_cost. |
| `battle.finisher(): table` | query | table | The finisher: progress, goal, ready, charging (the aura), charge (as a percentage) and gauge (the charge gauge's top-left corner over the partner: x, y as percentages, or nil). |
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

### `online`

| Name | Kind | Value | What it is |
|---|---|---|---|
| `online.revision` | setting | number | Goes up each time the online state changes. |
| `online.open` | setting | yes/no | Whether the rooms screens are open. |
| `online.sample` | setting | yes/no | Whether the list and the room are the development sample's. |
| `online.sample_offered` | setting | yes/no | Whether the switch to the sample rooms is offered (the developer tools are on). |
| `online.list.state` | setting | text | The public room list: loading, ready or unavailable. |
| `online.list.error` | setting | text | Why the list is unavailable (an error code), or empty. |
| `online.rooms` | setting | number | How many rooms the list has. |
| `online.room.<n>.id` | setting | text | The room's id in the list. |
| `online.room.<n>.name` | setting | text | The name its host gave it, or empty. |
| `online.room.<n>.name_key` | setting | text | The text key of its name when it has no name of its own (the sample rooms). |
| `online.room.<n>.mode` | setting | text | Its game mode: campaign, versus, training or free. |
| `online.room.<n>.players` | setting | number | How many players are in it. |
| `online.room.<n>.capacity` | setting | number | How many players fit. |
| `online.room.<n>.ping` | setting | number | The round trip to it in milliseconds; -1 when not measured. |
| `online.room.<n>.joinable` | setting | yes/no | Whether it can be joined now (waiting, not full, a mode this game plays in rooms). |
| `online.room.<n>.code` | setting | text | Its join code. |
| `online.room.<n>.message` | setting | text | The host's message, or empty. |
| `online.room.<n>.host_name` | setting | text | The host's name, or empty. |
| `online.room.<n>.state` | setting | text | waiting, full, starting or in_progress (a game under way that takes players). |
| `online.field` | setting | text | The text field taking the keyboard: filter, code, name, or empty. |
| `online.filter` | setting | text | What is typed in the room search. |
| `online.code` | setting | text | What is typed in the room code field. |
| `online.code_length` | setting | number | How many characters a room code has. |
| `online.quick` | setting | text | none when the quick match found no room to join; else empty. |
| `online.notice` | setting | text | A step the game did not take (a notice code), or empty. |
| `online.can_retry` | setting | yes/no | Whether online_retry has a room to ask for again (one was asked for). |
| `online.profile.name` | setting | text | The name the other players see (kept in the game's settings). |
| `online.profile.named` | setting | yes/no | Whether the player chose that name (false while it is the default one). |
| `online.profile.icon` | setting | text | The profile's icon id. |
| `online.profile.wins` | setting | number | Online versus matches won. |
| `online.profile.losses` | setting | number | Online versus matches lost. |
| `online.profile.draft` | setting | text | The name being typed on the profile screen. |
| `online.profile.relay_only` | setting | yes/no | Rooms connect through the server's relay only, hiding this machine's address. |
| `online.icons` | setting | number | How many icons a profile can pick from. |
| `online.icon.<n>.id` | setting | text | An icon's id, in the screens' order. |
| `online.modes` | setting | number | How many game modes a room lists. |
| `online.mode.<n>.id` | setting | text | A mode's id, in the screens' order. |
| `online.mode.<n>.available` | setting | yes/no | Whether the mode can be played in rooms now. |
| `online.current.active` | setting | yes/no | Whether the player is in a room. |
| `online.current.code` | setting | text | The room's code; empty until it has one. |
| `online.current.name` | setting | text | The room's name, or empty. |
| `online.current.name_key` | setting | text | The text key of its name when it has no name of its own. |
| `online.current.host` | setting | yes/no | Whether this player hosts it. |
| `online.current.listed` | setting | yes/no | Whether it is public (in the list) rather than private (by code only). |
| `online.current.phase` | setting | text | connecting, open, connected, ready, playing or closed. |
| `online.current.mode` | setting | text | The room's game mode. |
| `online.current.capacity` | setting | number | How many players fit. |
| `online.current.error` | setting | text | Why it closed or a step failed (an error code or the game's words), or empty. |
| `online.current.can_start` | setting | yes/no | Whether the host can start now. |
| `online.current.can_ready` | setting | yes/no | Whether this guest can mark itself ready now. |
| `online.current.ready` | setting | yes/no | Whether this player is ready. |
| `online.current.members` | setting | number | How many players are in the room. |
| `online.current.in_progress` | setting | yes/no | Whether the room's game is under way (a guest that readies joins it). |
| `online.current.open_to_join` | setting | yes/no | Whether the room's game, once under way, takes new players (guests coming back to their seat always get in). |
| `online.current.coop_difficulty` | setting | text | The host's co-op difficulty: off, standard, hard, or empty until it is known. |
| `online.member.<n>.name` | setting | text | A member's name, or empty. |
| `online.member.<n>.name_key` | setting | text | The text key of a member's name when it has none of its own. |
| `online.member.<n>.icon` | setting | text | The member's profile icon id, or empty when it has not sent one. |
| `online.member.<n>.host` | setting | yes/no | Whether the member hosts the room. |
| `online.member.<n>.ready` | setting | yes/no | Whether the member is ready. |
| `online.member.<n>.local` | setting | yes/no | Whether the member is the player at this machine. |
| `online.member.<n>.arriving` | setting | yes/no | Whether the member's seat is taken while its connection is still being set up. |
| `online_open` | command | — | The rooms screens opened: the list is asked for. |
| `online_close` | command | — | The rooms screens closed. |
| `online_refresh` | command | — | Asks for the room list again. |
| `online_field` | command | text | The text field that takes the keyboard: filter, code, name, or empty for none. |
| `online_filter` | command | text | Replaces the room search's text. |
| `online_code` | command | text | Replaces the code field's text. |
| `online_profile_name` | command | text | Replaces the name being typed on the profile screen. |
| `online_profile_save` | command | — | Keeps the typed name as the profile's (notice name_empty when nothing is left). |
| `online_profile_icon` | command | text | Changes the profile's icon. |
| `online_profile_relay` | command | yes/no | Connects the next rooms through the server's relay only (true) or directly (false). |
| `online_retry` | command | — | Asks again for the room last asked for (hosting, joining from the list or by code). |
| `coop_host` | command | yes/no | The co-op lobby's: opens a room this player hosts, true public (listed), false private (by code only). |
| `online_join` | command | text | Joins a listed room by its id. |
| `coop_join` | command | text | The co-op lobby's: joins a room by the code given (without one it opens the lobby's code entry). |
| `online_join_code` | command | — | Joins a room by the code typed in the code field. |
| `online_quick` | command | — | Joins the first open room of a playable mode, or answers online.quick none. |
| `online_mode` | command | text | The host picks the room's mode. |
| `coop_ready` | command | — | The co-op lobby's: the guest marks itself ready, or not when it was. |
| `coop_start` | command | — | The co-op lobby's: the host starts the room's game. |
| `online_open_to_join` | command | yes/no | The host says whether its game, once under way, takes new players (true, the default) or only guests coming back. |
| `online_leave` | command | — | Leaves the room (the host closes it). |
| `online_dismiss` | command | — | Clears the notice, the quick match's answer and a room that closed. |
| `online_sample` | command | yes/no | Switches to the sample rooms and back (only with the developer tools on). |
| `online.state(): table` | query | table | The screens' state: open, sample, sample_offered, field, filter, code, code_length, quick, notice, notice_key and can_retry. |
| `online.profile(): table` | query | table | The player's online profile: name, named, icon, wins, losses, draft and relay_only. |
| `online.icons(): table` | query | table | The icons a profile can pick: id and label_key. |
| `online.list(): table` | query | table | The room list: state, error, error_key, total and the rooms matching the filter (id, code, name, message, host_name, state, mode, mode_key, players, capacity, ping, ping_level, full, in_progress, joinable). |
| `online.modes(): table` | query | table | The game modes: id, label_key, description_key, short_key, available and selected. |
| `online.room(): table?` | query | table | The room the player is in, or nil: code, name, host, listed, phase, phase_key, mode, mode_key, capacity, error, error_key, detail, can_start, can_ready, ready, in_progress, open_to_join, coop_difficulty, coop_difficulty_key and members (name, icon, host, ready, local). |
| `online.error_key(code: string?): string` | query | text | The text key of an error code; unknown codes give online.error.unknown. |
| `online.notice_key(code: string?): string` | query | text | The text key of a notice code. |
| `online.open(): boolean` | action | yes/no | The rooms screens opened. |
| `online.close(): boolean` | action | yes/no | The rooms screens closed. |
| `online.refresh(): boolean` | action | yes/no | Asks for the list again. |
| `online.edit(field: string?): boolean, string?` | action | yes/no | The text field that takes the keyboard: filter, code, name or nil. |
| `online.set_filter(value: string): boolean, string?` | action | yes/no | Replaces the room search's text. |
| `online.set_code(value: string): boolean, string?` | action | yes/no | Replaces the code field's text. |
| `online.host(public: boolean): boolean, string?` | action | yes/no | Opens a room: public (listed) or private (by code only). |
| `online.join(room_id: string): boolean, string?` | action | yes/no | Joins a listed room. |
| `online.join_by_code(code: string?): boolean, string?` | action | yes/no | Joins a room by a code, or by the typed one. |
| `online.quick_match(): boolean` | action | yes/no | Joins the first open playable room, or answers quick = none. |
| `online.set_mode(id: string): boolean, string?` | action | yes/no | The host picks the room's mode. |
| `online.set_ready(ready: boolean): boolean, string?` | action | yes/no | The guest marks itself ready or not. |
| `online.start(): boolean` | action | yes/no | The host starts the game. |
| `online.set_open_to_join(open: boolean): boolean, string?` | action | yes/no | The host says whether its game, once under way, takes new players or only guests coming back. |
| `online.leave(): boolean` | action | yes/no | Leaves the room. |
| `online.set_name_draft(value: string): boolean, string?` | action | yes/no | Replaces the name being typed on the profile screen. |
| `online.save_name(): boolean` | action | yes/no | Keeps the typed name as the profile's. |
| `online.set_icon(id: string): boolean, string?` | action | yes/no | Changes the profile's icon. |
| `online.set_relay_only(on: boolean): boolean, string?` | action | yes/no | Connects the next rooms through the server's relay only, or directly when possible. |
| `online.retry(): boolean` | action | yes/no | Asks again for the room last asked for, after a failure. |
| `online.dismiss(): boolean` | action | yes/no | Clears the notice, the quick match's answer and a closed room. |
| `online.set_sample(on: boolean): boolean, string?` | action | yes/no | Switches to the sample rooms and back. |
| `online.changed` | event | table | The list, the room or the screens' state changed. Comes from `online.revision`. |

<!-- /ui-api:generated -->
