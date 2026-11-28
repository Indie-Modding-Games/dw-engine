# screens

The view models of the game's screens (`dwe::screens`): what the battle screen, the field HUD
and the partner menu show, taken from the session (`packages/session`) and from the systems (`packages/game/*`) without deciding any rule. It runs without a
window, so its tests open nothing. The game (`apps/game`) publishes them to the UI and the documents (`ui/battle`,
`ui/partner_menu` of the `game` pack) only draw them.

## How it reaches the UI

Each view is turned into UI settings (`settingsOf`) and `publish` changes only the ones that changed and then bumps
a revision setting (`battle.revision`, `partner.revision`, `bag.revision`). The document redraws when the
revision changes, so it reads a complete view and never half of one.

Lists go as `<list>.count` and `<list>.<i>.<field>` (`battle.fighter.0.hp`). Texts go as keys:

- the original's names (species, items, techniques) are keys of the extracted text tables, by their index
  in the data table (`NameKeys`: `main.digimon_names.NNNN`, `main.item_names.NNNN`, `main.move_names.NNNN`);
- the labels the original has (the commands, `btl.command_names`; HP, MP, Offense..., `main.stat_labels`;
  use, move, sort, drop, `main.item_menu_labels`) too;
- the rest are the game's own keys in `localization/<language>/battle.yaml` and `partner_menu.yaml`, and the currency's
  label, `common.bits` (the original's `main.battle_ui_bits_labels` is kept as extracted in every translation, so the
  language's own word for the currency, «ビット» in Japanese, comes from the engine's texts).

## Battle screen

`battleViewOf(battle, names)` while the battle lasts; `battleEndViewOf(result, aftermath, names)` when it ends.

| Setting | Value | What it is |
|---|---|---|
| `battle.fighter.<i>.{name,hp,max_hp,mp,max_mp,fainted,status,hp_bar}` | key, numbers, booleans, key | The partner (0) and the enemies: name, bars, whether it has fallen, its status (`battle.status.*`) and whether the HUD now shows its HP bar. The partner's is always shown; an enemy's, only for a while after taking damage and with no MP bar (`battle_main.c: BTL_renderEnemyHPBars`) |
| `battle.menu.<i>.{command,label,available,current}` | number, key, booleans | The entries its brains allow, in order (`command_menu.c: BTL_renderCommandMenu`): the command, its label (the original's commands or the technique's name), whether it can be chosen (the finisher, only with the gauge full) and whether it is the next order |
| `battle.menu.<i>.icon` | text | The BTLCOMND icon of the entry: `command-<n>` (the command number, from run to change), `special-<type>` (a technique, by the type of its effect, 0 to 6) or empty (the finisher, which has no icon in the strip) |
| `battle.menu.{top,bottom,blink}` and the revision `battle.menu.motion` | numbers, boolean | The order strip in motion (`command_menu.c: BTL_tickCommandMenu`, `screens::CommandMenuMotion`): its top and bottom edges in original lines (0 at the center). When it opens with the "BATTLE" banner it drops from -152/-120 to -100/-68 in 13 frames, the bottom edge first (the strip stretches) and then the top one; when the battle ends it rises the same way in reverse and the battle ends 16 frames later (`BTL_isCommandMenuClosed`). `blink` switches the pointed order's icon to its second frame every 8 frames, counted from when the strip opens and only while fighting (measured: the first change with the timer at 56). In a cup battle (`std_main.c: STD_addCommandMenu`, `CommandMenuMotion::openForMatch`) the strip appears already still at the bottom of the screen, from 68 to 100, without sliding, with the order's name 14 lines above (`STD_renderMoveName`); when it ends it is removed instantly (`STD_removeCommandMenu`); the original look places it with the `match` class of `#commands` (read from the decomp; Assumption: not measured, there is no cup save state) |
| `battle.bars.{hp_x,mp_x}`, with the revision `battle.menu.motion` | numbers | The partner's HP and MP bars in motion (`battle_hud.c: BTL_tickPartnerStatusBars`, `screens::PartnerBarsMotion`): the left edge of each one in original pixels from the center (`BTL_STATUS_BAR_X`: 160, off to the right, to 46 in place). They follow the strip (`BTL_COMMAND_MENU_ACTIVE`): when it opens they come in from the right, the HP one in 7 frames and the MP one from the 3rd to the 9th; they stay, with the finisher gauge, during the victory and the final box (dwe used to hide them), and when the strip rises they leave, the MP one first and the HP one from the 3rd frame, out by the 9th. Same as the original; only the original look slides them |
| `battle.shout.{shown,text,anchor_x,anchor_y,node_x,node_y,y,hop,scale}`, with the revision `battle.shout.revision` | text and numbers | The player's shout when giving an order (`command_shout.c: BTL_drawCommandShout` and `BTL_renderCommandShout`, `screens::CommandShout`): the order's text (`btl.shouts.*`, orders 1 to 7) or the technique's name (8 to 11, the finisher uses its slot). It is anchored on node 4 of the player's model (`FieldScene::playerNodeOnWindow`), in original pixels from the center: x limited to ±140 counting half the width (12 px per letter), and y to [-100, 100]. It grows from 0.2 to 1 in 4 frames (`(f*2+2)/10`), lasts 20 frames and follows the jump table (rises up to -40) or, if the node is above -40, the fall one (drops down to 40); black shadow at +1,+1. Same as the original; it does not show if no opponents remain |
| `battle.finisher.{progress,goal,ready,charging,charge}` | numbers, booleans | The finisher gauge (`battle_hud.c: BTL_renderFinisherGauge`) and its charge during the aura (`command_menu.c: BTL_tickFinisherChargeup`) |
| `battle.revive_seconds` | number, -1 for no count | The seconds the fallen partner has left to get up, from when the original shows the count (`command_menu.c: BTL_renderDeathCountdown`) |
| `battle.bag.{available,open}`, `battle.item_in_flight` | booleans | The tamer's bag: whether it can be opened (not while running, not without a tamer, with no other item in the air), whether it is open and whether a thrown item is on its way (`battle_tick.c: tamerTickBattle`, `main.c: tickThrownItem`). The UI draws nothing with `item_in_flight`: the original has no text while the item flies, so the line "Here it goes!" was removed from both skins |
| `battle.end.outcome` | key | `battle.outcome.victory`, `fled` or `defeat` |
| `battle.end.<i>.{parts,name,amount,subject}` | text, keys, number | The lines of the final box (`battle_ui.c: handleBattleEndBox`), in the original's order: items the enemies drop, wound, orders learned, MP bonus, technique learned; then dwe's: lives lost, items lost, who joins the city. `parts` are the pieces of the line separated by `|`: a text key, or loose marks if it starts with `{` (`{color:7}` yellow, `{color:1}` white, `{br}` row, `{wait}` row and wait). `name`, `subject` and `amount` fill in the keys |
| `battle.end.gain.<i>.value` | number | What each stat gained (HP, MP, offense, defense, speed, brains) |
| `battle.end.stat.<i>.{value,max}` | numbers | Each stat before rising and its cap: the box counts from there and draws its bar against the cap (`renderPostBattleStatsBox`) |
| `battle.end.bits`, `battle.end.money` | numbers | The bits won and the money with them already included: the balance box counts up to it (`tickBitBox`) |
| `battle.frame_seconds`, `battle.end.{count_delay_frames,wait_frames}` | numbers | How long a battle frame lasts and the pace of the boxes, from the ruleset (`battle.hud`): the wait before counting and that of each line. The boxes close by themselves at the end; a button skips what is running and writes the next line |

With the bag open, the game publishes `bagViewOf(inventory, items, names, UseContext::Battle)` (the `bag.*` settings
below): `usable` says which items can be thrown in battle and `discardable` is always false. The bag shown
is the field's (`ui/partner_menu`, items page, with `partner_menu.context = battle`): its Use sends `bag_use`
when its boxes have closed and the battle throws the item (QA-M19).

The game adds `battle.phase` (`intro`, `fight`, `victory`, `end` or `closing`; `closing` is the order strip rising after the final box and the enemies leaving (`battle_main.c: BTL_deinitializeCombat`), with no input; `victory` is the victory before the final box, with no box or input: the wait of 0x29 frames of `BTL_handleVictorySequence`, sound 0x18 and the player's (clip 0xb) and partner's (clip 0x2a) poses, until both finish; the port cuts the poses at 200 battle frames if one does not finish), and the document sends `battle_command` (the command number),
`battle_charge`, `battle_close`, `battle_end_count` and `battle_end_counted` (the count of the end box, for its
sounds), `battle_bag_open`, `battle_bag_close` and `battle_throw` (the slot number)
([ui.md](../../architecture/ui.md)).

With the original look, the partner's HP and MP are drawn with the original's digits
(`battle_hud.c: BTL_renderNumber`: the 7 × 11 ones from BI_IN, white and untinted, right-aligned in four positions),
the same as the versus HUD. `ui/common/hud_digits.lua` places them and `skins/original/theme.rcss` gives them their image.
The modern look writes the number as text.

### Battle opening

`BattleIntro` is the original's opening step by step, at its battle pace (20 frames per second): the anger
mark over each enemy while the original loads the battle (`btl.c: handleBattleStart`, `renderFleeBubble`),
the "BATTLE" banner with its 155 pieces that fly in with their trail and blink when they come together
(`battle_hud.c: BTL_renderBattleStartText`, `battle_setup.c: BTL_handleBattleIntro`) and its burst
(`BTL_renderBattleStartTextBurst`). The pieces come from a seed, so a route always sees the same opening.
`battleIntroViewOf(intro, marks)` and `settingsOf(view, enemies)` give the settings the document reads, which redraws
with `battle.intro.revision`. The game publishes them once per drawn frame (not per tick: a slow frame that
does several ticks does not redraw several times) and the document only touches the pieces whose group changed:

| Setting | Value | What it is |
|---|---|---|
| `battle.intro.step` | text | `anger`, `banner`, `burst` or `done` |
| `battle.intro.anger.<i>.{shown,x,y}` | boolean, % | Whether enemy `i`'s mark is visible and where, in % of the window (its center) |
| `battle.intro.anger.scale` | % | The size of the marks: they grow for 5 frames and then pulse (`FLEE_BUBBLE_SCALE`) |
| `battle.intro.flash` | 0 to 2 | The palette the banner blinks with when all its pieces have arrived |
| `battle.intro.pieces` | text | The pieces that are visible, seven numbers each in pixels of the original's 320 × 240 screen: x, y, width, height, texture (0 to 2), trail and 1 if the trail goes left |

Banner parity (measured on the original on 2026-11-04, `11_battle_agumon`):

| Rule | Decomp | Here | Test | Status |
|---|---|---|---|---|
| Order the pieces arrive in: when preparing the banner each piece gets its index and then, piece by piece, it is swapped with the one from a `randomLimit(155)` roll (measured piece 0 ends up with index 151; x 519, y −48, speed −96, drift (−258, −72), trail 24, texture 1) | `battle_hud.c: BTL_initializeBattleStartTextBurst`, `BTL_shuffleBattleStartTextPieces` | `BattleIntro::startBanner`, `shufflePieces` | the banner is finished two frames after its pieces are in, and again once they burst | Same in the order of the rolls; Assumption: the rolls come from the opening's own generator (`std::minstd_rand` with a seed), not the battle's, so the pieces do not match the original one by one |
| End of the banner: `BTL_BATTLE_TEXT_FINISHED` is set on the frame in which all the pieces have arrived (the blinking starts) and is read at the start of the next one, so the burst starts two frames later; it is set again when no piece is left inside the ±180 × ±140 box. The measured burst lasts 60 VSync, 20 battle frames | `battle_hud.c: BTL_isBattleStartTextFinished`, `BTL_renderBattleStartTextBurst`; `battle_setup.c: BTL_handleBattleIntro` | `BattleIntro::finished`, `stepBanner`, `stepBurst`, `burstCorner` | the banner is finished two frames after its pieces are in, and again once they burst (15 to 25 frames) | Same |

### Fallen partner countdown

`deathCountdownLookOf(timer)` gives the look of the countdown the original puts over the fallen partner
(`command_menu.c: BTL_initializeDeathCountdown`, `BTL_addDeathCountdown`, `BTL_tickDeathCountdown`,
`BTL_renderDeathCountdown`): the BI_IN star and on top the digit, 5 to 1. `deathCountdownTimerOf(battle)` counts
its frames from frame 20 of the partner on the ground (`reviveCountdownShownAt`) and `deathCountdownAnchorOf`
puts it on the partner's node 1 as it looked when it appeared. `BattleScreen` publishes it with the HP bars
(`battle.hp_bars.revision`):

| Setting | Value | What it is |
|---|---|---|
| `battle.death_countdown` | text | Empty if it is not visible; otherwise, seven values: x and y of the anchor in % of the window, the digit (5 to 1), the digit's scale and the star's (1 as is), the rotation in degrees and the star's palette (0 to 2) |
| `battle.stun_signs` | text | Empty without a stunned fighter; otherwise four values per stun sign the field draws over a fighter: left, top, width and height in % of the window (the sign is 40 x 44 pixels of the original's 240-line screen over the foot of its post). The battle document keeps the enemies' bars off them |

| Rule | Decomp | Here | Test | Status |
|---|---|---|---|---|
| Frame-by-frame look, measured on 2026-11-04 (`dw-original`): on the first tick the star measures 40 × 40 (u 0x32, v 0x80 of page 896, 256; center 20, 20), scale 0x1000, CLUT y 0x1ed; the digit 16 × 16 (center 8, 8) at 0x1000. Step = timer up to 30 and then `t % 31 + 1`. Steps 2 to 4: the digit +0x199 per step; 5 to 7: −0x199; otherwise 0x1000. Every third step up to 20 the star moves to the next palette (13 → 14 → 15 → 13; six changes per cycle, so each cycle starts at 13). From step 21, both at `((100 − (step − 20) × 5) << 12) / 100` and +0x40000 of rotation per step (64°); on tick 26, 0x0B33 and 0x180000. The digit is `(t + 29) / 30`: 5, 4, 3, 2 (u 0x50/0x68/0x58/0x68, v 0xa8/0x90/0x90/0x80) and then 1 (0x58, 0x80); it is drawn for the last time on tick 150 | `command_menu.c: BTL_initializeDeathCountdown`, `BTL_tickDeathCountdown`, `BTL_renderDeathCountdown` | `screens::deathCountdownLookOf`; `ui/battle/logic.lua: draw_death_countdown`; original look: `#death-countdown` with the `death-badge-*` and `death-digit-*` sprites | the countdown pops its digit, then shrinks and spins away, a second each; the countdown's setting is empty while it does not show | Same. The width 39 when the digit goes past 0x2000 never happens (it reaches 0x14CB). The modern look keeps its text (`#revive`) |
| When and where it appears: at frame 20 of the partner on the ground (`DEATH_COUNTDOWN` 20) on its node 1 (`getEntityScreenPos(entity, 1)`), clamped to x ±140 and y ±100 from the center (measured: 3, −56); it advances only with no thrown item in flight, like the battle's count; reviving removes it (`item.c: handleRestore` → `BTL_removeDeathCountdown`) | `battle_setup.c: BTL_isBattleFinished`; `command_menu.c: BTL_addDeathCountdown`, `BTL_removeDeathCountdown` | `screens::deathCountdownTimerOf`, `deathCountdownAnchorOf`, `BattleScreen::publishHpBars` | the countdown shows from the partner's frame 20 on the ground until it is revived; the countdown stands on the partner's node, kept near the screen's middle | Same. Assumption: the object does its first tick on the same frame it is added (timer 1 on frame 20) |

### Damage numbers

`DamageNumbers` are the numbers that jump off the fighters (`entity_text.c`): a queue per fighter of up to 8, each number fixed where their feet were when it was added and moved frame by frame at the battle's pace (see the row in [battle.md](../game/battle.md#parity-with-the-original)). `BattleScreen` adds those of each frame and takes a step; after the victory they keep going on their own. `settingsOf(numbers)` gives the settings the document reads, which redraws with `battle.damage.revision`:

| Setting | Value | What it is |
|---|---|---|
| `battle.damage.numbers` | text | The numbers drawn in the last frame, seven values each: x and y of the anchor in % of the window, offset of the first digit from it in original pixels (x, y), value, color (0 normal, 11 green for healing or a rise, 12 poison) and icon (0 none, 1 HP, 2 MP, 3 offense, 4 defense, 5 speed) |

### Enemy HP bars

`enemyHpBarOf` places and tints an enemy's bar (`battle_main.c: BTL_renderEnemyHPBars`): it receives where node 1 of its model is seen (fraction of the window), the window's aspect ratio and its HP, and gives the bar's corner from that point in original pixels, the width of the fill and its tint (see the row in [battle.md](../game/battle.md#parity-with-the-original)). `BattleScreen` computes it on every tick for the enemies whose bar is visible (`Battle::hpBarShown`), so it follows the enemy, and removes it when the battle ends. `settingsOf(bars)` gives the settings the document reads, which redraws with `battle.hp_bars.revision`; only the original look shows them:

| Setting | Value | What it is |
|---|---|---|
| `battle.hp_bars` | text | The bars drawn in the last frame, nine values each: enemy (1 to 3), x and y of the anchor in % of the window, corner of the bar from it in original pixels (x, y), width of the fill (0 to 50), red and green tint (128 as is, 255 double) and HP |
| `battle.markers` | text | The marks of a cup battle between two of the same species (`std_main.c: STD_initializeCombat` calls `STD_initializePlayerMarker` only then; `STD_renderPlayerMarker`), three values each: fighter (0 the partner, "1P"; 1 the rival, "COM") and x and y of the anchor in % of the window: node 1 of its model moved right by half the species' radius at its depth; the mark's corner, 32 pixels above. The original look draws the STDTIM bubbles (page 768, 0; CLUT 0, 481; 48 × 31 and 47 × 31); the modern one, a label with the text. Read from the decomp; Assumption: not measured (there is no cup save state) |

## Lives box

`LivesBox` is the lives box of a lost life (`murd.c: MURD_createLivesBox`, `MURD_tickLivesBox`,
`MURD_renderLivesBox`, `MURD_animateLivesBoxOut`, `MURD_removeLivesBox`) on the original's animated box (`ui.c:
createAnimatedUIBox`, `renderUIBoxAnimated`), frame by frame at the scene's pace: its gray outline grows
from the partner for 5 frames, the box shows a slot per life, with the one just lost full until
frame 0xa, blinking two frames at a time until 0x1e and empty afterward; when closing it shrinks another 5 frames toward the
partner. Sound 0 plays when it opens and 1 when it starts to close, on channel 0. Measured on the original (a life lost
after a battle): the box is created on frame 0x23 of `MURD_tickScene`, the outline is drawn from 35 to 39, the box
is in place from 40 and `MURD_tickLivesBox` counts its frame from that same 40 (frame = scene − 39),
because `renderUIBoxAnimated` puts it in place after drawing the last outline and `tickUIBox` moves it from the
next one; the lost heart thus blinks from 49 to 68 of the scene. The scene's fade (layer 0, 0 to 0xa0 from
0x5f to 0x87) also darkens the box: the document lowers its opacity with `screen_fade`, like the HUD (Assumption:
opacity 1 − fade instead of the original's subtraction). The `ui/lives_box` document redraws with `lives_box.revision`:

| Setting | Value | What it is |
|---|---|---|
| `lives_box.shown`, `lives_box.outline` | booleans | Whether the box is visible, and whether only its outline (while it grows or shrinks) |
| `lives_box.grow` | number | How much it has grown: 0 at the partner, 1 in place |
| `lives_box.anchor_x`, `lives_box.anchor_y` | numbers | The partner's midpoint on screen, as a percentage of the window |
| `lives_box.lives`, `lives_box.slots` | numbers | The lives left after losing one and the slots (the lives you start with) |
| `lives_box.lost_full` | boolean | Whether the life just lost is shown full this frame |

The document draws each life as the original's picture (`life-N`, `ETCHI/LIFE.TIM`'s full or empty cell) inside a
slot (`slot-N`) that `logic.lua` marks `empty` for a life gone or blinking out, so a skin can draw its own life on the
slot and hide the picture. The pixel skin draws its hearts that way and leaves the box see-through (`image-color`
c0 on its frame) so the partner stays visible behind it, as behind the original's translucent box. Before, it drew
the hearts on the picture itself under a transparent `image-color`, which hid them too: a lost life showed three
empty slots and no blink, the report "no death animation when losing the last life" (2026-11-20).

## Field HUD

`fieldHudViewOf(time, partner, shown)` is the field's state: the time and day, happiness and discipline, hunger,
sleepiness, potty, tiredness, wound and sickness, with what each one urges (`urgency`: its rank by the deadline in
`PartnerView::needDeadlines`, for the stacked bubble), and HP and MP, only with numbers and booleans so the game can
compare it every frame without building texts.
`fieldHudLabelSettings()` gives the labels, which are published once. The `hud.*` settings, their units and when they
update are the data contract of any look: they are in [ui.md](../../architecture/ui.md#field-status).

## Partner menu

`partnerStatusViewOf(partner, names)` is the status page and `bagViewOf(inventory, items, names, context)` the bag
(`context` is `UseContext::Field` in the menu and `UseContext::Battle` in battle).

| Setting | Value | What it is |
|---|---|---|
| `partner.{name,age,weight,lives,hp,max_hp,mp,max_mp}` | key, numbers | What the original's stats view shows (`game_menu.c: renderDigimonStatsView`): name, age in days, weight, lives, current and maximum HP and MP |
| `partner.pool.<i>.{label,value,min,max}` | key, numbers | The HP (0) and MP (1) bars: the maximum of each over 9999, not what it has left, as the original draws them under the numbers (`game_menu.c: renderDigimonStatsView`, `renderDigimonStatsBar`) |
| `partner.stat.<i>.{label,value,min,max}` | key, numbers | The offense, defense, speed and brains bars, over 999 |
| `partner.meter.<i>.{label,value,min,max,two_tone}` | key, numbers, boolean | Happiness, discipline and virus, with their ruleset limits. Happiness and discipline are two-tone bars: the bar fills from the minimum to the middle of the range and fills again in another color from the middle to the maximum (`game_menu.c: renderDigimonStatsView`) |
| `partner.condition.<i>.key` | key | What is happening to it now (sleepiness, tiredness, hunger, potty, anger, wound, sickness) |
| `partner.tiredness.{label,value,min,max}` | key, numbers | Extra the original does not show: tiredness (0–100, `PartnerState::tiredness`) as a simple bar. The page draws it with the `status_extras` option ([sheet extras](#sheet-extras)) |
| `partner.care_mistakes` | number | Extra the original does not show: the care mistakes with this partner (`careMistakes`), with the same option |
| `partner.favorite_food` | key | Extra the original does not show: the name of the species' favorite food (`screens::setFavoriteFood`); empty if the favorite is not a food (newborns have item 0) and the sheet says "None". The row shows with the `favorite_food` Improvement (see "Favorite food" in `game.md`) |
| `partner.battles` | number | Extra: this partner's battles (`battles`, `PartnerView::battles`), which count toward evolution |
| `partner.lifetime_hours` | number | Extra: the hours of life it has left (`remainingLifetime`, `PartnerView::lifetimeHoursLeft`); it drops one every game hour and with the hours slept |
| `partner.evolution.{hours,check_hours}` | numbers | Extra: the evolution counter (`evoTimer`, hours since the last level change) and from how many hours on evolution checks its level (`Evolution::checkHoursFor`: `check_hours` from the ruleset, or the Mega step); -1 if its form does not evolve by itself |
| `partner.awake.{wake_hour,wake_minute,sleepy_hour,sleepy_minute}` | numbers | Extra: at what time it wakes up and at what time it gets sleepy today (`SleepState`, `wakeUpHour` and `sleepyHour`); the schedule shifts if it sleeps late or early |
| `partner.profile.special.<i>` | number | Species specialties (0–6); -1 stands for an absent position. |
| `partner.profile.type` | number | Species' original type; 0 draws no icon. |
| `partner.profile.active` | number | SpeciesCare activity pattern; -1 when the source is missing. The original presentation takes the labels from the disc's local text. |
| `partner.technique.<i>.{empty,name,power,mp,range,effect,boosted}` | boolean, key, numbers | Three equipped (0–2) and the finisher (3), resolved from the battle catalog and the partner's own variants (its boosted technique too); MP already converted by its rules. An empty slot shows no figures or effect. |
| `partner.technique.choice.<i>.{name,available,equipped,known,slot,special,power,mp,range,effect,boosted}` | key, booleans | 56 cells of the original grid; slot resolves the species-compatible position (or -1); boosted marks the partner's boosted technique, whose power and MP are already the boosted ones. Learning, variants and selection come from the session; Lua does not decide rules. |
| `partner.technique.can_finish` | boolean | The editor can finish if an offensive technique remains equipped; the finisher does not count. |
| `partner_menu.extra_presses` | number | Counts the presses of the `extra` action with the partner menu in view; the original skin uses it to open the techniques help page. |
| `bag.slot.<i>.{empty,item,name,description,count,dimmed,usable,icon,icon_rect}` | boolean, number, keys, number, booleans, texts | The slots the bag has, in order (`inventory.c: renderInventoryTop`); `dimmed` is an item that is not used from the bag (`item_color` 255), which the original paints a different color; `usable`, whether its `item_color` allows using it in that context (`inventory.c: selectOptionMenu`); `icon`, the image of its icon with its palette (`/texture/etcdat/item?palette=N`, from `icon_palette`), and `icon_rect`, its 16 × 16 cell in it, `"x y width height"`, 15 wide in the last column (`utils2.c: setItemTexture`; `itemIconOf`, the same for any item list); both empty if the item has no icon. `ui/common/bag_slot.lua` paints a slot (icon, name, quantity, ink classes) in any document that lists the bag |
| `bag.slot.<i>.item_color` | number | Item classification; the original look tells field (0), battle (1), both (2) and never (255) apart with its ink. |
| `bag.slot.<i>.discardable` | boolean | Item present, Field context and droppable value accepted by UseRules; the UI disables Drop without duplicating rules. |
| `bag.slot.<i>.favorite` | boolean | The item is the current partner's favorite food (`bagViewOf` with `InventoryAdapter::favoriteFood`); `bag_slot.lua` sets the `favorite` class (golden and a mark) if the `favorite_food` Improvement is on. Both the field bag and the battle bag publish it |
| `bag.money` | number | The bits |
| `bag.result` | key | What the last order did on the bag (`partner_menu.bag_result.*`); the game adds `bag.result_item`, the key of the item's name |

The game adds `partner.care_result` (`partner_menu.care_result.praised` or `scolded`).

**Proportional virus bar (decision of the design lead, 2026-10-21).** The original draws it with 3 pixels
per point of the bar (`game_menu.c: renderDigimonStatsView`: `renderBox(..., virusBar * 3, 6, ...)`); here it fills
in proportion to its ruleset maximum. It is a difference in look only and it stays.

**Long name on the sheet.** The original look's name box fits 12 letters. With the
[hidden content](game.md#hidden-content) `species_t` has 13: `partner_menu/logic.lua` sets the `long` class on
`#status-name` and the box widens (324dp instead of 294dp) without reaching the profile titles. A name of wide
letters (a nickname in capitals) can still be wider than its box: the text (`#status-name-text`) is then squeezed
into the box's inside with `common/fit_text.lua`, so it never runs over «Special». A name that fits is not touched.

**Portrait of a new form.** The sheet's portrait is placed with the species' raise row (`view`), which
`game.cpp` reads from the disc table. The forms the ruleset patches add (`species_q`, `species_t`) are only
in the patched table, so for them it asks `PlaySession::raiseTable` (the session ruleset's, with
`ruleset::loadPatchedRaiseTable`), and scales it as in the field (`PlaySession::speciesScale`, the patch's
`field_scale`: `species_t` at 0.6). A clip its model does not have plays 0.

**Battle (decisions of the design lead, 2026-10-21).** An enemy's HP panel carries its name in the
modern look; the original has no panel and draws the bar next to the enemy (`battle.hp_bars`); the final
box's title ("You escaped." when fleeing) stays as an accessibility deviation. The bag that threw on confirming the
slot was removed on 2026-10-30 (QA-M19): the battle opens the field's bag with its options box. They are in the parity table of the [packages/game/battle page](../game/battle.md).

**Praise and scold only in the field (decision of the design lead, 2026-10-20/21).** In the original the partner is praised and
scolded in the field, next to it (`partner.c: partnerTickPraiseScold`, `tamer.c: tamerTickPraiseScold`),
not from the sheet. The game menu's entries (`praise`, `scold`) already play that scene in the field
([Praise and scold](game.md#praise-and-scold)). The partner menu's tab (`partner_praise`,
`partner_scold`) stays, without a scene; it will be removed (TODO in `apps/game/src/partner_menu.hpp`).

### Sheet extras

An optional Improvement, on by default. The original shows none of this although it uses all of it: tiredness for the
"tired" state, mistakes and battles for evolution, the sleep schedule for sleep, the remaining lifetime
for natural death and the evolution counter to know when it evolves. The `status_extras` setting (row
"Status extras" of the pause menu options, saved in the user settings) arrives as a UI setting from `PlaySession`; `partner_menu/logic.lua` (`draw_extras`) uses it to set the class
`status-extras` on `#scrim` and draw the values, which `partnerStatusViewOf` always publishes. It changes no rule.
Off, the page is the usual one.

What it shows and where each value comes from (all are state the engine already simulates; none is computed for the sheet):

| Value | Comes from |
|---|---|
| Tiredness, 0–100 | `MoodState::tiredness` (`tiredness`) |
| Happiness, discipline and virus with a number | The same bars as the original (`happiness`, `discipline`, `virusBar`) |
| Care mistakes | `EvolutionCounters::careMistakes` (`careMistakes`) |
| Battles | `EvolutionCounters::battles` (`battles`) |
| Hours awake today | `SleepState`: wake-up and sleepy hour (`wakeUpHour`, `sleepyHour`) |
| Remaining lifetime, in hours | `LifeState::remainingLifetime` (`remainingLifetime`) |
| Evolution counter / hours of the check | `EvolutionCounters::hours` (`evoTimer`) and `Evolution::checkHoursFor` (the level's `check_hours` in `rules/original/evolution.yaml`: 6, 24, 72 and 144; nothing for an Ultimate) |

- **Original skin.** The mistakes and battles go at the end of the age and weight row, each one a figure in a
  box like theirs after its label ("Battles", "Mistakes") in the cyan of the field labels. The
  "Active" field writes the hours it is awake, from hour to hour ("08-20"), instead of the pattern's word, in a
  sunken box of its own like the other fields and centered (class `active-hours`); with the option off the word
  stays where the original puts it. Tiredness is a fourth condition bar, with the icon of the "tired"
  need, its short word ("Tired") and the blue strip of the original's bars. The four bars are
  shorter (84dp; 69dp with the labels written) and carry their number in white behind, with "%" on happiness,
  discipline and tiredness (`partner_menu.percent.value`). To make the four fit, the life and the bars move up and
  close together (rows every 36dp) and the model's gap is 6dp lower. The hours of life it has left and the evolution
  counter go in the gap to the right of the offense, defense, speed and brains bars: a cyan label ("Lifetime",
  "Evo. timer") over a box with the figure ("353 h"; the counter with the check hours and the
  unit, "5/72 h", `partner_menu.evolution.value`; only the counter, "5 h", if its level does not evolve by itself).
- **Modern skin.** The mistakes and battles share a line under the weight (the profile lines come together a
  little); tiredness is a fourth care bar, "Tiredness" with its number, and under the bars
  go three more lines: "Awake 08:00-20:00", "Lifetime 353 h" and "Evo. timer 5/72 h". Happiness, discipline and
  tiredness carry "%"; the virus does not. The two cards grow one line. With the option off the battles and
  hours-awake lines are hidden (they used to show loose over the header).

Units and format (both skins):

| Value | Original | dwe | Why |
|---|---|---|---|
| HP and MP | `game_menu.c`, the stats view (US version): the current and the maximum as two separate figures; the Japanese and European versions draw a 12 px "/" bar between them | Original skin: "958 / 1053", the bar in its fixed place (`.pool-slash`) between the two figures, which fit at 9999 / 9999. Modern skin: "958 / 1053" as a single figure that does not split | The bar exists in the original itself (JP/EU) and makes clear what each figure is |
| Happiness, discipline, tiredness | Bars without a number | Number with "%" (`partner_menu.percent.value`) | Discipline and tiredness go from 0 to 100; happiness from -100 to 100, so "-40%" can be read |
| Virus | Bar without a number | Number without "%" | It is a count from 0 to 16 (`virus_bar_limit`), not a percentage |
| Evolution | Not shown | "hours / check hours h" | Both figures are hours: those it has been at its level since the last change and those its level waits for before checking evolution; "Lifetime 353 h" uses the same unit |

The route `route-status-extras.yaml` gives the partner tiredness (two trainings) and a care mistake (the fast clock
until a meal passes it by), opens the sheet with the option on at 1280x720 and 1920x1080, turns it off in the
pause menu and opens it again like the original, with `text_fit`; it is run in both languages and with both skins.
Before turning it off it opens the sheet with the widest values (HP and MP 9999 / 9999 healed, discipline 100, evolution
counter 350) at 1280x720 and 1024x768.
It also checks the battles (0) and the Rookie's evolution check hours (72). Tests: `the status
page's extras carry the tiredness and the care mistakes the original hides` and `the status page's extras carry the
battles, the lifetime, the evolution counter and the waking hours` (`packages/screens/tests/partner_status_view_tests.cpp`),
`the partner view carries the lifetime, the counters and the sleep schedule it lives by`
(`packages/session/tests/session_tests.cpp`), `a form's check hours are its level's, and an ultimate's are none`
(`packages/game/evolution/tests/report_tests.cpp`) and the user settings (`apps/game/tests/user_settings_tests.cpp`).

## Game menu (original look)

Parity with the menu review against the original (QA-M, 2026-10-30). Status: **Same**, **Assumption** or **Gap**.

| Behavior | Original | dwe | Test | Status |
|---|---|---|---|---|
| Bag in battle | `battle_tick.c: tamerTickBattle` → `inventory.c: addInventoryUI`: the same bag as the field, with the description below and the options box (Drop grayed out); Use closes the boxes and throws (`startThrowingItem`), 41 to 45 vblanks later; canceling closes it and returns to the battle, which does not stop | `ui/partner_menu` with `partner_menu.context = battle` (no tabs, returns to the battle, not to the game menu); the own box of `ui/battle` ("Throw an item", dashes in the gaps) disappears (QA-M19) | `ui_tests`: "the battle opens the field's bag…", "the field's bag uses an item at once…"; `route-battle-*` | Same |
| Going back from the bag or the partner to the game menu | `game_menu.c: tickTriangleMenu` (1372-1379) and `inventory.c: tickInventoryUI` (385-392): on canceling, the menu comes back and responds to the gamepad | `partner_menu/logic.lua: close_menu` shows `pause_menu` modal (`ui.show(name, true)`); before, it stayed visible without receiving navigation (QA-M01) | `ui_tests`: "a menu a script shows modal again takes the navigation back" | Same |
| The menu action (Tab) opens the game menu in the field | `game_menu.c: addGameMenu` (1306): the gamepad's menu button | The `menu` action is Tab on the keyboard and the gamepad's north button (before, the pause key); north is still `alt` in the name box, which reads it first (QA-M03) | `navigation_tests`, `ui_tests`: "the pause menu works with a gamepad alone…" | Same |
| The game menu (Tab, north button, the HUD's "Menu" button) does not open during transitions (#167) | `tamer.c` (520, the walking state's tick): Triangle opens the menu only with the player in its state 0 (walking), with `IS_SCRIPT_PAUSED` 1 (no script running), `FADE_PROTECTION` 0 and the dialog box closed (`UI_BOX_DATA[0]`). `fade.c`: `fadeToBlack` sets `FADE_PROTECTION` on the first frame of the fade-out and `renderFadeIn` clears it when the fade-in ends, one frame after the player walks again. `utils2.c: handlePause` (402) does not pause with `FADE_PROTECTION` 1 | `playerMenuShutBy` (`player_menu_gate.hpp`) with `PlaySession::menuShutBy`: fade (`Field::fadeProtected` or any fade on screen), script (`heldByScript`, `scriptRunning`), open dialog, battle/scene/screen (`busyWith`) or held player (`playerFree`). `Ui::setMenuShut` closes the way for Tab and the north button; `PlaySession::handle` swallows `open_menu` from the HUD button (mouse); Select/Back on the gamepad (there is no keyboard shortcut) do not open the menu either and the pause (P) waits for the end of the fade. Measured in dwe (twna01 → room08 walking, Tab every 4 ticks): no opening from tick 350 to 430 (fade-out, change at 370 and a 20-frame fade-in = 60 ticks); the first at 434. Before, it opened at any moment with no modal in front | `game_tests`: "player menu gate: …"; `ui_tests`: "a shut menu does not open …"; `field_tests`: "a map change's fade protects the game …" | Same; the pause does not yet copy the rest of `handlePause` (in the field it does not pause with the clock stopped: menus, scripts), dwe still pauses over menus and chats |
| The pause action (P or Pause) pauses the game | `utils2.c: handlePause` (402) and `createPauseBox` (443): the gamepad's pause button toggles `PAUSE_STATE`; box of -26, -14, 56 × 24 from the center (US version), "Pause" (`main.utils2_pause.0000`), `playSound(0, 3)` on opening, no sound on closing; the field, the battle and the menus freeze | `PauseBox` (`apps/game/src/pause_box.cpp`) with the `pause` action (P and Pause on the keyboard, the gamepad's pause button); the `pause_box` document on top of everything; the game's and UI's tick does not run while it lasts. On the title screen, a movie or the name box the gamepad's pause button does what it did before (`menu`) (QA-M02) | `game_tests`: "pause box: …" | Same |
| Field darkened under the pause box | The original's capture (`orig/pause_box.png`) looks darker, but `pauseFrame` (utils.c:75) only redraws the objects; not measured | The field stays as it was | — | Assumption |
| Menu font | `font.c`, `drawInventoryText` (inventory.c:255): pixel font from the disc in all texts | `ui/fonts/original_look.yaml`: `default: original` (before, the smooth TTF); the modern skin still uses Noto Sans (QA-M08) | `bitmap_font_tests`: "…defaults to it" | Same |
| Box border | `renderUIBoxBorder` (ui.c:268): top and bottom black, light (#90D9FA), black; on the left black, dark (#4A9FC5), light and an inner black line; on the right the inner black line, light, dark, black; 4 × 4 corners | `original-box` decorator (`packages/engine/ui/src/original_box.cpp`) with those 14 lines on all the original skin's boxes (menus, bag, pause, texts, battle, slots, HUD) and the `.original-box` class in `theme.rcss` (QA-M09) | `original_box_tests`: "the original box's border has renderUIBoxBorder's lines"; `skin_switch_tests`: "no shipped skin paints a box's fill over its original-box border" (the fill goes last in the list: RmlUi paints the first decorator on top, and a gradient in front covered the border of the title screen, the language, the slots and the dialog) | Same |
| Boxes open and close with an outline | `createAnimatedUIBox` / `renderUIBoxAnim` (ui.c:216/324): 1 px gray outline (#808080) that grows over 5 frames (edge at frame/4) from the player, the cell or the cursor; on closing 4..0. Measured: 20 frames per second (3 vblanks), menu 0..4 on vblanks 22362..22374; menu → bag: the menu closes (4..0, 22383..22392) and the bag opens (0 at 22396), about 10 frames | `ui/common/original_box.lua`: `open`/`close` with the same computation at 20 per second; the menu and the bag from the player, the description and the options from the cell; the box receives no input while it lasts (QA-M05) | `qa-box` routes (captures in `_work/routes-e2/menus-qa/m05-*`) | Same |
| Sounds of opening and closing boxes | `ui.c:222` `playSound(0, 0)` on creation and `ui.c:335` `playSound(0, 1)` on the first closing frame | `ui_sounds.yaml`: `box_open` (ss 0) and `box_close` (ss 1), which play with `ui.cue` when the animation starts (QA-M06) | `ui_tests`: "a script sounds the original boxes…" | Same |
| Icon of each item in the bag | `renderInventoryTop` (inventory.c:590) with `renderItemSprite` and `setItemTexture` (utils2.c:269/288): 16 × 16 from ETCDAT/ITEM.TIM, the cell of the item number in rows of 16 (the one starting at 0xF0, 15 wide), with its palette (`ITEM_CLUT_DATA`); at x+10 (x+154 on the right) and y+8+row×18 of the box: 3 px inside the cursor and 1 px below its edge (measured in `field_item_bag_1item`: icon at 18, 17 with the cursor at 15, 16). Always at color 0x80: an item that is not used does not dim the icon, only its name (TEXT_COLORS 8). An empty slot paints nothing | `itemIconOf` (`bag_view.cpp`) publishes `bag.slot.<i>.icon` and `icon_rect`; `ui/common/bag_slot.lua` paints them (also for the battle bag); `.slot-icon` of 48dp at 9dp and 3dp from the slot's outer edge: measured +3, +1 px and 16 px. QA saw no icons because it used content extracted before `icon_palette` (`content-f8-s2-4a`); with a new extraction they show up (QA-M07) | `screens_tests`: "an item's icon is its cell of the items' image…"; route `bag-icons` (`_work/routes-e2/menus-qa/bag/`) | Same |
| Description when moving an item | `renderInventoryBottom` (inventory.c:634): in all states, also when moving (state 4, which only adds the red outline 0x5D4AF1 of `renderInventoryTop`, inventory.c:607), it paints the description of the item under the cursor with TEXT_COLORS 9; over an empty slot it paints nothing | `partner_menu/logic.lua: draw_description` with `bag_slot.description`: in the original skin the focused slot's description while an item is moved; before, "Where to put …?", which the original does not have. The modern skin keeps the question (QA-M17) | `ui_tests`: "the original bag describes the slot under the cursor, also while an item moves"; route `bag-icons` (captures `move-*`) | Same |
| Options box cursor and the slot under it | `renderItemOption` (inventory.c:721) and `getItemOptionColor` (209): the four options in TEXT_COLORS 9 (0x6E; measured #B5B5B5) or 0xA if they cannot be used (0x46), the chosen one the same, over the cursor of `renderSelectionCursor`: border #B0B0B0 / #101010 and half-opacity gray fill (measured #525A63 over the box #293942). The slot they come from keeps its cursor (`renderInventoryTop` always paints it). The description, TEXT_COLORS 9 (measured #B5B5B5) | Cursor fill `#80808080` on the options, the sort and the confirmation (before opaque gray #808080, which dimmed the text; now measured #565C60); `.picked` class on the slot while the boxes are open; options and description ink #B2B2B2 (measured 178, same as before with the pixel font) (QA-M18) | `ui_tests`: "the original bag describes…" (`picked`); route `bag-icons` (capture `options`) | Same |
| Description of an empty slot | `renderInventoryBottom` (inventory.c:636) paints nothing if the slot is empty; the options come out with Use and Drop at 0xA (`getItemOptionColor`) | Empty description box (`bag_slot.description` gives "" for an empty slot); Use and Drop `.unavailable` (QA-M24) | `ui_tests`: "the original bag describes…"; route `bag-icons` (capture `empty-slot`) | Same |
| Bag cursor | `inventory.c: tickInventoryTop`: up and down two at a time inside the bag, left and right within the row, without wrapping; the rows scroll with the cursor; up and down repeat after 11 frames held (`isInventoryButtonPressed`) | `partner_menu/logic.lua: on_action` (original skin) with `focus_slot`; the repeat is the engine's | `ui_tests`: "the original bag describes…" | Same |
| Options, sort and drop boxes | `createItemOption` (cursor on Use), `tickItemOption` and `tickSortOption` with `tickInventoryOptionSelector` (they stop at the ends), `createSortOption` (Battle, Raise, Basic; cursor on Battle), `createDropItemConfirmBox` ("Are you sure?", cursor on No) and `tickDropItemConfirmBox` (left and right between Yes and No, without wrapping). Measured on 2026-11-04 (`15_item_option_box`): `ITEM_MENU_SELECTED` 0 on opening, 0 after up and 3 after four downs; `SORT_SELECTED` 0; `DROP_ITEM_SELECTED_OPTION` 1 on opening and 0 after two lefts | `partner_menu/logic.lua: step_box` (the modern skin wraps around), `choose_option`, the `confirm-box` block of `on_action` | `ui_tests`: "the original bag's boxes stop at their ends…" | Same |
| Sorting leaves the cursor where it was | `selectSorting`: `sortItems` and both boxes close (`tickInventoryUI`, cases 11 to 13); `INVENTORY_POINTER` does not change. Measured: with the cursor on slot 3, Battle sorted Small Recovery, S.Def.disk and Meat into 0 to 2 and the cursor stayed on 3 (empty, no description) | `partner_menu/logic.lua: choose_sort` focuses the slot the options were opened from; before it went back to slot 0 | `ui_tests`: "the original bag's boxes stop at their ends…" | Same |
| Moving an item | `selectOptionMenu` (Move: state 4 and `ITEM_MOVE_SOURCE`), `selectRegularInventory` and `moveItem`: confirming on any slot swaps the two and the cursor stays on the destination; the description follows the cursor (`updateItemDescription`). Measured: from 0 to 2, Meat 3 and Small Recovery 2 swapped slots and the cursor ended on 2 | `partner_menu/logic.lua: pick_slot` sends `bag_move`; `Inventory::move` | `bag_tests`: "moving swaps…"; `ui_tests`: "the original bag describes…" | Same |
| Dropping an item | `selectDropItem`: Yes drops (`removeItem` with the slot's quantity) when the boxes close; No closes only the question and goes back to the options (case 0xF) | `partner_menu/logic.lua: confirm_drop`; `Inventory::discard` ([inventory](../game/inventory.md)) | `ui_tests`: "the original bag's boxes stop at their ends…"; `bag_tests`: "discarding a slot takes…" | Same |
| The bag starts without boxes | `initializeInventoryUI` (`INVENTORY_ACTION` 0) when the game starts | `partner_menu/logic.lua: on_show` clears boxes and movement every time it opens | — | Same |
| Switching between Status and Techniques | `game_menu.c: tickDigimonMenu` (1714-1733): right and left change page inside the same box, with `playSound(0, 2)`; canceling closes it (`TRIANGLE_MENU_STATE` 4) | `partner_menu/logic.lua: on_show` (`reopen_pages`) opens the box of all the pages before growing the starting one: before, closing the menu from Techniques left that page shrunk (`box-closed`) and when returning to it only the tabs were seen, in the modern skin too. The partner's portrait does not receive the mouse (`pointer-events: none` in the modern sheet, which the original one inherits): its image rose above the tabs and in the modern skin blocked the click on Techniques, Items and Care from Status | `ui_tests`: "the partner menu's pages stay shown however often the tabs switch and it reopens" | Same |
| Item in hand | `item.c` empties the item in the player's hand (to 0xFF) when the partner eats or refuses the item and when the thrown one arrives; `initializeDroppedItems` empties the 11 items on the ground at the start | `FeedScene::finish`, `BattleScreen::tickThrow` and `endThrow`; `FieldItems` starts empty (`clear`) | — | Same |
| Game menu: cursor and closing | `game_menu.c: tickGameMenu`: rows of three; left and right wrap around the whole list; up and down change row and wrap to the last row that has that column; Sleep is enabled when sleepy; canceling calls `closeTriangleMenu` (the box shrinks toward the player), returns the player to its normal state and calls `startGameTime` | `pause_menu/logic.lua: on_action` (original skin) and `close_menu`; Tab also closes it | `navigation_tests` | Same |
| Choosing techniques | `getEquippedSlot` (44 and 48, 55 and 57 count as one) and `equipMove`: confirming on an equipped technique removes it; otherwise it goes to the first free slot if the partner knows it and its species has it, and with all three occupied the error sounds. Measured on 2026-11-04 (`03_partner_status`): Spit Fire in slot 0 (`EQUIPPED_MOVES` 2, base technique 46); confirming removed it (255, 255) and confirming again put it back in 0 (2, 46); a technique it does not know changed nothing | `partner_moves.cpp: toggleTechnique` | `battle_adapter_tests` | Same |
| Sunken boxes, lines and the technique panel | `renderInsetBox` (lines #020202 top and left, #6987A0 bottom and right, fill #354B5C), `renderSeparatorLines` (each line with its `UI_LINES_BORDER_COLOR`) and the function that draws the chosen technique (the panel on the right with the cursor in the first three columns, on the left in the others) | `.player-inset` and the `separator-*` of the original skin; `#tech-info` with `info-right` (`draw_tech_info`): the name box and the stats box hanging from its bottom rim, their rims on the original's lines; a long name squeezed onto one line (`common/fit_text.lua`) | `skin_switch_tests`; `w98-classic-ui/pair-3-picker-*.png` | Same |
| Player's point on screen | `getEntityScreenPos` (bone 1) | `publishPlayerPoint` in `game.cpp` publishes `field.player_x/y`; the waist height (120 units) is an estimate | — | Assumption |
| Tabs of the player sheet | `tickPlayerMenu` (game_menu.c:1860): left and right move between Player, Chart, Med. and Card always | The four tabs are always reachable, in that order; without the evolution table (`data/evolution_chart`, which `dwx extract` produces; earlier extractions do not have it) Chart comes out with an empty table instead of being skipped. Before, Med. depended on a setting the game did not publish, Card was disabled and Chart was skipped (QA-M04) | Routes `pl-tabs` and `pl-held` (`_work/routes-e2/menus-qa/player`) | Same |
| City tab | It does not exist: the original does not show who lives in the city; it only adds up the prosperity in `main.c: recalculatePPandArena` (3902) | Fifth tab after Card (see [City tab](#city-tab-a-dwe-improvement)); the five share the box's width, each as wide as its longest word plus a margin, so the whole words fit ("Player", "Chart", "Card"); only "Medals" does not fit and stays "Med.", like the original | `route-city-tab`; `cityResidents`, `cityTab`, `CityAdapter::residents` | Documented improvement |
| Raised count and medals | `renderPlayerInfoView` (2976): the tamer's `raisedCount` (1 on a new game, `main_menu.c:4939`; +1 per rebirth up to 99, `partner_impl.c:803`) and the medals `hasMedal(0..17)` (triggers 0x16c + n) | `TamerState::raised` (section `tamer`, key `raised`, 1 if missing) and `StoryAdapter::medalCount`, published as `player.raised` and `player.medals`; before, only the UI demo set them (QA-M04) | `session_tests`: "the player counts the first partner raised…"; `story_adapter_tests`: "the player page counts the medals…" | Same |
| Card list | `renderCardsView` (3750): "Card List", 11 × 6 cells of 18 × 18 (`renderBorderBox`: #BEBEBE top and left, #3C3C3C bottom and right, fill #696969), 24 × 20 cursor on confirming (Enter), which moves without wrapping; in the cell of a card that is owned (`getCardAmount`), its 16 × 16 icon from `SBOY` at ((n % 8) × 32, (n / 8) × 16) with the palette `CARD_SPRITE_CLUT[n]` (0x80123E28, 66 bytes) | `#player-cards-view` in `pause_menu` with those measures; the game publishes `player.cards.copies` (the copies of the 66 cards) and `player.card.<n>.palette`, which `dwx extract` produces as `icon_palette` in `data/cards` (earlier extractions do not have it and use palette 0). `dwe-game` did not load the card table (`createGameSystems` with empty tables), so the player never had cards; now it loads it from the disc | `card_table_tests`, `exe_tables_tests`, `tables_yaml_tests`, `game_systems_tests`: "the game's systems read the cards from the packs…"; route `pl-held` | Same |
| Opening a card | `tickPlayerMenu` (2070-2088): confirming on a card with copies (`playSound(0, 3)`) opens two animated boxes from the cell: the image (`renderCardImage`, -75, -84, 150 × 180, box -75, -83) and the number of copies (`renderCardCount`, box 74, 69, 54 × 24); canceling closes them. Measured on the original: image from x 85 to 235 and y 36 to 216, number box from y 189 to 213 (the original's capture is stretched 16/15 vertically) | `#card-detail` (image `/card/NNN`) and `#card-count-box` in `pause_menu` with those measures and `original_box.open` from the center of the cell; on an empty cell confirming does nothing. The two boxes animate one after the other; the original animates them at the same time | Route `pl-held` (captures 08 to 12) | Same |
| Player's items | `renderPlayerInfoView` (3028-3042): with triggers 45 to 48 (the two rods and the other two items), a 16 × 16 icon from `SYSTEM_W` (u 176, 192, 224 and 208, v 32, palette 7) at x 52 + 21 × i, y 55; the cells are 20 pixels apart, so each icon shifts one more pixel | `player-item-0..3` sprites in the original skin (over the enlarged `system_w`, which is why they do not use `rect`), shown according to `player.items.held` (`StoryAdapter::hasPlayerItem`) | `story_adapter_tests`: "the player page shows the medals and the item boxes…"; route `pl-held` | Same |
| Medal table | `renderMedalView` (3664): 15 medals (5 × 3); the one not owned (`hasMedal`, trigger 0x16c + n) has the dark shade on top (`sboy` 240, 200); on confirming, the cursor goes to medal 0, and with the cursor's medal owned its name and its three lines appear | `player.medals.held` (15 values) sets the `held` class; `player.medal.<n>.name` and `.line.<j>` come from `data/medals` (`MedalTable`). The 3D medal in the box on the right (`renderDigiviceMedals`, `initializeMedalModel`, `activateMedalTexture`): `ETCNA/MEDAL.TMD` with the chosen medal's face copied from its sheet (24 × 32, nine per row next to the face gap), spinning 0x64 per frame on its y axis, seen from `MEDAL_VIEW` ((-1050, 220, -10000), projection 0x400); the game draws it in `runtime/medal-figure` (`MedalFigure`) while the chosen cell is owned and carries the `showing` class | `medal_table_tests`, `story_adapter_tests`, `medal_sheet_tests`; route `pl-held` (captures 03 to 06) | Same; Assumption: the spin at 20 frames per second and the menu portraits' light, not measured on the original |
| Inactive tabs of the sheet | `renderMenuTab` (2960) draws the unchosen tab with `renderRectPolyFT4` flag 1: color 0x32 over 0x80, the texture darkened, not transparent. Measured: (132, 239, 255) the chosen one, (49, 90, 99) the others | `image-color: #646464` on the unchosen tabs of `pause_menu` and `partner_menu` (before `opacity` 0.25 and 0.45, almost invisible over the field) (QA-M12) | Routes `pl-tabs` and `pl-held` | Same |
| Key hints on the techniques and player pages | `drawMoveViewHelpStrings` (game_menu.c:3318) and the Chart, Med. and Card pages: gamepad buttons in blue and green with "Change Techniques", "Select" and "Cancel" | Each hint names the key of the device in use with `ui.prompt`: Enter and Ctrl with a keyboard, the gamepad button only when a gamepad is playing, in the original's colors; those of Chart, Med. and Card end at the panel's right edge because a key's name is wider than a glyph (QA-M10) | Routes `qa-tech` and `qa-player` | Documented improvement |
| "Techs" heading and help box when changing techniques | `renderDigimonMovesView` (2577) and `MOVES_VIEW_HELP_LINES`: "Techs" where "Final" was; box from x -142 to -80 and y 26 to 93 with black, light (148, 222, 255 measured) and black lines, "Tech" and "help", the arrow (`system_w` 128, 12) and the button that opens the help page (`MENU_STATE` 7) | `#techs-heading` and `#tech-help` in `partner_menu` with those measures; the button is the key of the `extra` action (Q with a keyboard). The light lines of the Tech page go from #6987A0 to #94DEFF, the measured color, and the page carries the `original-box` border like the Status one | Route `qa-tech` (`_work/routes-e2/menus-qa/tech/`) | Same |
| Techniques help page | `tickDigimonMenu` and `renderDigimonMovesView` (`MENU_STATE` 7 to 9, US branch): the square button opens it with `playSound(0, 3)`; "Technique Select Help" at (-142, -82), two bulleted lines (`system_w` 180, 12, palette 4): "Select - choose [fire] to select tech" and "Cancel - ✕ deselects tech [red]"; separator at y -4; legend at x -120 (Current, Mastered, Unmastered, Unusable twice, Selected with the cursor at -123, 80); only cancel responds and goes back to changing | `#tech-help-page` in `partner_menu` with those measures; the game counts the presses of `extra` in `partner_menu.extra_presses` and the script opens the page with `ui.cue("confirm")`; Ctrl closes it. The ✕ is the confirm key of the device in use (Enter with a keyboard) | `skin_switch_tests`: "the original skin's Tech page: no Final range, the help page…"; route `qa-tech` | Same |
| Cells of the techniques grid | `renderDigimonMovesView`: each cell is a subtractive box of 0x68, black over the panel (0, 0, 0 measured); the learnable ones (0x4E, 0x60, 0x6E); the equipped one, red (200, 0, 40) at 50 % over the pictogram (173, 74, 90 measured); a known technique the partner cannot use leaves the pictogram under the subtractive box | The base goes from gray #686868 to black; `.tech-cell-shade` puts on top the equipped one's 50 % red or the unusable known one's 55 % black | Route `qa-tech` | Same |
| Range of the final technique | `renderDigimonMovesView`: the Final row never shows range; measured by writing `MOVE_DATA[59].range` to 1 and 3 (it stays blank) while the Techset row's changes (capture `orig-final-range-written-3.png`) | `logic.lua: draw_tech` leaves row 3's range empty with the original skin; before it showed "L" (QA-M11) | `skin_switch_tests`: "the original skin's Tech page…" | Same |
| Names of the partner's parameters | `renderDigimonStatsView` (2402): HP, MP, Off and Def with the letters at full size in color 3 (255, 247, 115 measured); SPEED and BRAINS are small-caps drawings from `system_w` (212, 70) and (212, 82), 36 × 12, palette 4 | `#status-parameters .label` at 36 dp in #FFF371; `#stat-2-label` and `#stat-3-label` with the `status-speed-name` and `status-brains-name` sprites; the BRAINS icon is 10 wide, without the "]" that peeked out (QA-M13) | `skin_switch_tests`: "the original skin's Tech page…" | Same |
| Sleep entry without sleepiness (QA-M15) | `addGameMenu` (game_menu.c:1319): without `PARTNER_PARA.condition & 1` entry 6 stays `disabled` = 1, and `renderGameMenu` draws it with `renderRectPolyFT4` flag 1 (color 0x32 over 0x80). Measured in `02_menu_main`: `condition` 0, `MENU_OPTIONS[6].disabled` 1, the Sleep icon and label with no light pixel at all (RGB sum < 250) against Scold (206, 214, 214). The review's capture was of a sleepy partner | The entry comes out darkened when the partner is not sleepy; unchanged | Capture `menu-shot` | Same |
| Date digits in the header (QA-M16) | `renderDateDigits` (game_menu.c:1481): 8 × 12 digits from FI_INFO with CLUT 0x7f10 (row 508, palette 12: white 0x739C with border 0x25B4); left-aligned and 9 pixels between digits. Measured: with "Year 1 Day 12" the year's 1 starts at x 138 and the day's at 200 and 209 | Palette 12 (before 4, which painted the border black and left the digit like a stencil); crop without the first column of the cell, which carries pixels of the neighboring digit; the day starts at 312dp (x 200) | Captures `menu-shot` against `02_menu_main` with `DAY` = 11 | Same |
| Selection bar of the dialog box (QA-M21) | `scriptShowSelection` (script_textbox.c:1017): `cursorWidth` = the longest line `showTextbox` writes (without the speaker's name) × 12 + 2, at most 240; `renderDialogueSelectionCursor` draws it at x − 1 and 2 pixels above the row (US), 13 tall under the light line. Measured on the gym sign (`guide_gym_sign_menu`): `cursorWidth` 86 ("Explain", 7), the bar from x 34 to 119 and y 181 to 194 | `story_textbox.cursor_columns` (`longestRow`) and the `.bar` of each row: the same width for all the options, same position and height; no line under the name. The new game's questions do not publish it and keep the bar under the words | `story_text_rows_tests.cpp`; capture `qa-gym` | Same |
| Speaker's name when several NPCs on the map have their script number | `getSpeakerName` (script_value.c:60): `ENTITY_TABLE[scriptIdToEntityId(id)]->type`, the species of the NPC on the map with that number. The city's item shop (room06) has one record per shopkeeper, all with number 5, and the header uses the one from variable 25 | `StoryTextbox::speakerName` takes the record of the NPC placed on the map (`FieldNpcs::recordOf`) and, if there is none, the first record of the map with that number; before it always took the first and `species_ai` came out with another shopkeeper's name | "a script id names the spawned NPC's record…" (`field_tests.cpp`); `route-stage4-monochromon-city.yaml` (the name row says `species_ai`) | Same |
| Position of the menu box and the dialog box | `tickTriangleMenu` (game_menu.c:1336): `createMenuBox(0, -66, -80, 132, 110)` from the center, x 94 to 226 and y 40 to 150; `setupDialogueBox` (script_textbox.c:923): (-130, 42), 262 × 59, x 30 to 292 and y 162 to 221. Measured on 2026-10-31 (`02_menu_main`, `e1-cp09`): the menu's top light line at y 35 of the capture and the bottom one at 150; the dialog's at 166 and 226, x 32 and 289. The capture stretches 224 lines to 240 (game y = (capture y) × 14 / 15 + 8): 41, 148, 163 and 219, just those of the decomp; that is why in the capture the menu looks 6 pixels higher and the dialog 4 lower | The menu box at 120dp (y 40) and the dialog at 486dp (y 162), like the decomp; the dialog was 1 pixel to the left (centered: x 29) and now starts at x 30 (`margin-left: -390dp`) | Captures at 1280x720 of `route-gamepad` (menu: light lines at y 123 and 444, x 448 and 829) and of the arrival dialog (y 489 and 657, x 256 and 1027 after the change) | Same |
| The partner's model on the status page | the stats view (game_menu.c:2403) and `renderDigiviceEntity` (3953): `startAnimation(entity, 0)` every frame, the model still on its first rest frame, next to the condition box | Improvement: the model has its own gap (`#partner-portrait-clip`, covering no hearts or bars) and does the gesture of its first need in the field's order (`conditionAnimation`): sick or wounded (pain), potty, hunger, tiredness, sleepiness, anger; with no needs, its rest. Mood does not change it (`partner::statusAnimation`, `PartnerPortrait::play`) | `field_animation_tests.cpp` ("the status page's model shows its first need, else stands"); captures with and without potty | Improvement |
| Figures on the status page | `renderNumber` (utils.c:217): right-aligned in fixed columns of n digits (8 pixels per digit in the US version); HP and MP bars over 9999 and the others over 999 (game_menu.c, each stat's bar) | Values right-aligned in fixed columns in both skins; the bars' scale does not change (which is why they come out almost empty with low values, as in the original) | autotest `text_fit` at 1280 × 720, 1920 × 1080 and 1024 × 768 | Same |
| Labels of the status and techniques page in other languages | The labels are drawings in English (Name, Species, Age, Weight, SPECIAL, TYPE, ACTIVE, Happiness, Discipline, Virus, Power, MP, Range, Effect, the technique categories) in their colors (#18FF10, #FFDE00, #FF39E7 on Special, Type and Active) | In English the drawings stay; in another language (`#scrim.status-text`) they are hidden and the keys `partner_menu.profile.{special,type,active}`, `partner_menu.tech.data` and `partner_menu.tech.category.<n>` come out with the same colors, in the full word and with an initial capital | autotest es and en, original and modern skin | Same |
| Activity pattern ("día") | The disc's activity label has the words in fixed columns of characters | `logic.lua: draw_status` cuts the label by UTF-8 characters, not by bytes; before the word with an accent ("día") came out empty in Spanish | autotest es | Same |
| Status page in the modern skin | — (dwe only) | Model on the left in its box and name, species, age, weight and condition next to it; before the tabs and the hints came out 20 dp outside the window at 1280 × 720. The HUD minimap remains, which overlaps the menu at 1920 × 1080 and 1024 × 768 (also in main) | autotest modern skin | Improvement |

### City tab (a dwe improvement)

An improvement with no option (decision of the coordinator, 2026-10-31): one more tab on the player sheet that changes no
rule. It shows who already lives in the city and who is missing, with each one's prosperity points.

- **Who can live in the city.** The species `recalculatePPandArena` counts with the rules of the ruleset's `city`
  section (`rules/original/city.yaml`): from 3 to 58 with Rookie level or higher in the species table, 50 in
  the original. Each one is in the city if its trigger 200 + species is set. It adds its level minus 2 (Rookie 1,
  Champion 2, Ultimate 3), and `species_ar`, `species_p` and `species_be` add 1: among the 50, 100 points. No list of its own:
  `packages/game/story: cityResidents` walks the rules and `CityAdapter::residents` serves it.
- **Header.** "Prosperity" with the value of variable 1 as the scripts read it (the one redone on loading and
  saving) and "Residents N/M": those who have joined and the total.
- **Grid.** One cell per species, 12 per row in both looks, grouped by level and by number inside
  each group, with the color of its column in the evolution table: Rookies in blue, Champions in green and Ultimates
  in brown. The icon is the 16 × 16 one from the evolution table (`data/evolution_chart`, `SBOY` with its palette), from the
  content the player extracts, at an integer scale so it fills the box: 4 times in the original look (64dp) and 3
  in the modern one. Those missing come out as a light gray silhouette: the same icon with the engine's gray tone
  (`&tone=grey`, [ui](../engine/ui.md)), which keeps the shape and shading over the cell's color (no new
  images).
- **Cursor and footer.** In the original look, left and right change tabs and Enter puts a yellow frame on the first cell,
  which the arrows move without wrapping; Ctrl removes it. In the modern look, the
  "City" entry of the player page opens the view with the cursor placed, and Ctrl goes back. With the mouse, a click on
  a cell chooses it. At the bottom come the name of the chosen cell (the missing ones too) and its points ("+2").
- **Cost.** The game publishes the lists (`player.city.*`) once when opening the pause menu (while it is open
  nobody joins); the tab only redraws when they change, and a cursor step touches two cells and the footer.

## Item keeper

The city's item keeper (room02, script 171, section 0x51 → `call_routine(11)` →
`shop/script_menu.c: tickItemKeeper`). The session (`session::ItemKeeper`, the ruleset's `item_keeper` section) keeps what
the player leaves, up to 99 of each item, and writes it into the save; the game's `item_keeper` screen
(`apps/game: ItemKeeperScreen`) asks, shows the two windows and says goodbye. Measured with the original at the counter
(captures `orig/bank/keeper-open.png`, `keeper-row4.png`, `keeper-moved-one.png`).

| Setting | Value | What it is |
|---|---|---|
| `item_keeper.phase` | text | `ask`, `choice`, `windows`, `description`, `farewell` or `closed` |
| `item_keeper.{speaker,line_key,choices_key,description_key}` | keys | The keeper's name, what it says (MAPHEAD 1237, lines 0 and 2), the "Yes/No" (line 1) and the item's description |
| `item_keeper.{choice,side}` | numbers | The marked answer (0 yes, 1 no) and the window the player is working in (0 kept, 1 the bag) |
| `item_keeper.{kept,bag}.{total,top,cursor}` | numbers | The rows of each list, the first one visible and the cursor's among the five visible |
| `item_keeper.{kept,bag}.<i>.{name,count,movable}` | key, number, boolean | The five rows that are visible; a row that cannot go to the other side is painted red (color 3) |

| Behavior | Original | dwe | Test | Status |
|---|---|---|---|---|
| Question, yes or no and farewell | `tickItemKeeper` (script_menu.c:892): lines 0, 1 and 2 of section 1237 of MAPHEAD | `ItemKeeperScreen`: `ask`, `choice`, `farewell` with the same lines | Route `bank` (room02) | Same |
| "Keeping" and "In hand" windows | `createItemKeepWindow`: boxes from x 8 to 155 and from 160 to 307, y 15 to 150; blue bar on the active one, MENU and COUNT labels from SYSTEM_W, five rows every 19.25 px, scroll bar from x 144 to 151 | `item_keeper/structure.rcss` and the original skin with those measures; the titles are texts from the pack (the original carries them in the executable) | Route `bank` | Same |
| Lists | `itemKeeperFillItemList`: the kept ones by item number; the bag, one row per slot; a row does not move without a free slot or stack in the bag, or with 99 kept | `ItemKeeper::keptRows` and `bagRows` | `itemKeeperFillItemList: a row that has nowhere to go cannot move` | Same |
| Move one (Enter or Space) | `keeperMoveOne` | `ItemKeeper::move(…, One)` | `keeperMoveOne moves one item each way…` | Same |
| Move ten (E, the gamepad's north button) | `keeperMoveTen`: 10 are needed and the other side must have fewer than 90 | `ItemKeeper::move(…, Ten)` | `keeperMoveTen needs ten to move…` | Same |
| Move the stack (Q, the gamepad's west button) | `keeperMoveStack`: what fits up to 99 | `ItemKeeper::move(…, Stack)` | `keeperMoveStack moves the whole stack…` | Same |
| Item description (P or Pause, Start on the gamepad) | Start opens `tickItemMenuDescriptionBox`; any button closes it | Phase `description`; any button closes it | Route `bank` | Same |
| Jump to the start or the end (Shift with up or down, R1 on the gamepad) | R1 held with up or down | The `walk` action held | — | Same |
| Help under the windows | `updateKeeperTextbox`: line 3 of MAPHEAD with the ✕, ○ and □ glyphs | Three lines from the pack with the key of the device in use (`ui.prompt`) in the original's colors | Route `bank` | Documented improvement |
| Repeat when holding confirm or alt | The original's repeat mask repeats up, down, confirm and alt | Up and down repeat (the UI's navigation); a held confirm keeps moving items faster and faster (`ConfirmRepeat`, from 0.45 s to 0.05 s between steps); alt does not repeat | `confirm_repeat_tests`: "a held confirm keeps moving items" | Gap: alt does not repeat |
| What is kept goes in the save | `SCRIPT_STATE` (0x54 + item), inside the save | `item_keeper` section of the save: `kept: [{item, count}]`; without it, nothing kept | `what the keeper holds goes in the save…` | Same |

## Dropping an item

The item menu that routine 0 opens (`script_common.c: openDiscardItem`, `ITEM_MENU_TYPE` 5): the bag slot by
slot, the "Is it OK?" box and the description. The session (`session::DiscardMenu`) decides what can be chosen and writes the
result; the game's `discard_menu` screen (`apps/game: DiscardScreen`) shows it. Measured with the original (full bag
written into RAM and routine 0 forced at room02's counter; captures `orig/discard-*.png` of
`_work/routes-e2/menus-qa/discard-bits/`); the emulator stretches 224 lines to 240, so the measures are of the game's
screen (screen y = (capture y) × 14 / 15 + 8).

| Setting | Value | What it is |
|---|---|---|
| `discard.phase` | text | `list`, `confirm`, `description` or `closed` |
| `discard.{total,top,cursor}` | numbers | The bag's rows, the first one visible and the cursor's among the six visible |
| `discard.row.<i>.{name,count,choosable,icon,icon_rect}` | key, number, boolean, image | The six rows; what cannot be dropped is in red (color 3) |
| `discard.{choice,confirm_key,cancellable,description_key}` | number, keys, boolean | Yes (0) or no (1), "Is it OK?" (MAPHEAD 1240, line 0), whether canceling closes the menu (trigger 3) and the description |

| Behavior | Original | dwe | Test | Status |
|---|---|---|---|---|
| List window | `ITEM_MENU_POS[5]`: x 124 to 290, y 22 to 151; MENU and COUNT from SYSTEM_W, six rows every 18 px with icon, name and quantity, 146 × 18 cursor, scroll bar | `discard_menu/structure.rcss` with those measures; box, rows and texts within a pixel of the original | Route `discard` (yaka13) | Same |
| What cannot be dropped | `calculateItemMenuStrings`: in red; confirming does not open the box | `DiscardRow::choosable`; the row in red and confirming does nothing | `shopFillSellItemList lists every slot; only droppable items can be picked` | Same |
| "Is it OK?" | `createSingleCardShopMenu`: 112 × 42 box at (104, 99) opaque (#293942); question at (8, 5), "Yes" at (18, 21), "No" at (72, 21), 40 × 14 cursor | `#confirm` with those measures | Route `discard`; `the yes of the confirm box writes the item's number to 254…` | Same |
| No or cancel in the box | `tickSingleCardShop`: the box closes and the list goes on | Goes back to phase `list` | Route `discard` | Same |
| Cancel in the list | `tickItemMenu`: only with trigger 3 (when the script lets you keep everything); writes 255 | `DiscardMenu::cancel`; the "Back" hint only shows when it is possible | `cancel closes the menu only while trigger 3 is set…` | Same |
| Description (P or Pause, Start on the gamepad) | Start opens `createItemMenuDescriptionBox`: box from x 28 to 292 and y 109 to 131 | Phase `description` | Route `discard` | Same |
| Moving and jumping to the ends (R1 or Shift held) | `itemMenuCursorUp/Down/Top/Bottom` | `DiscardListView` | `discard list: the cursor walks the six rows…`, `discard list: R1 held jumps…` | Same |
| Key hints | There are none (the script's text box stays below) | Accept and Back with the key of the device in use (`ui.prompt`), between the window and the text box | Route `discard` | Documented improvement |

## Jukebox

The jukebox that routine 9 opens (`shop/script_menu.c: openJukebox`, `createJukeboxMenu`, `tickJukeboxMenu`,
`calculateMusicMenuStrings`); in the game, `species_at` in the restaurant (room05b). The session (`session::Jukebox`) decides
the list, the mark and the music; the game's `jukebox` screen (`apps/game: JukeboxScreen`) shows it. Measured with the
original (the routine forced through RAM with variable 254, the city theme playing; capture `original_list.png` of
`_work/routes-e2/menus-qa/jukebox/`, and the route `jukebox.yaml` with its captures in `dwe/`).

| Setting | Value | What it is |
|---|---|---|
| `jukebox.phase` | text | `greeting`, `list`, `farewell` or `closed` |
| `jukebox.{speaker,line_key,chosen_key}` | keys | The owner's name (the map character with the script of variable 254), what it says (MAPHEAD 0x4d6) and the name of the chosen song, which replaces `{bgm:pstat=249}` |
| `jukebox.{total,top,cursor}` | numbers | The rows of the list, the first one visible and the cursor's among the six visible |
| `jukebox.row.<i>.{number,name,playing}` | number, key, boolean | The six rows: the number from 1, the name and whether it is the marked one (color 6) |

| Behavior | Original | dwe | Test | Status |
|---|---|---|---|---|
| Greeting, question, answer and farewell | `openJukebox` states 0, 3 and 4 and `tickJukeboxMenu`: lines 3, 0, 2 and 1 of section 0x4d6 of MAPHEAD, in the script's text box | Phases `greeting`, `list` and `farewell` with the same lines; while it is open it covers the script's text box, which stays hidden until the script changes it | Route `jukebox` (room05b) | Same |
| Which songs show up | `jukeboxFillTitleList`: the first 62; the 63rd only with trigger 50 | `Jukebox::open` | `jukeboxFillTitleList lists 62 songs, and 63 once trigger 50 is set` | Same |
| The one playing, marked | The row of the map's music in variable 249 (47, "48 File City Theme", with the city theme); if it is not there, 255 and the music stops | `Jukebox::open`; in room05b, 47 as in the original | `the map's song is marked in variable 249…`, `a map song the list lacks stops the music…`; route `jukebox` | Same |
| List window | `allocateItemMenuBox` and `createJukeboxMenu`: x 89 to 311, y 22 to 151; labels "No." and "TITLE" from SYSTEM_W; six rows every 18 px, the number at x 8 and the name at x 28; 202 × 18 cursor; scroll bar at x 210 | `jukebox/structure.rcss` and the original skin with those measures; a long name is cut at the window's edge | Route `jukebox`; `list-orig-vs-dwe.png` | Same |
| Where the cursor starts | `createJukeboxMenu`: the marked row in the first window, or at the very bottom with the list scrolled (top 42, cursor 5 with 47) | `Jukebox::startView` | `the map's song is marked…`; route `jukebox` (top 42, cursor 5) | Same |
| Moving and jumping to the ends (arrows; Shift held, R1 on the gamepad) | `itemMenuCursorUp/Down/Top/Bottom`: stops at the ends | `JukeboxListView` and the `walk` action held | `the list's cursor stops at the ends and R1 jumps to them` | Same |
| Choosing (Enter or Space) | `tickJukeboxMenu`: the row to 249, "&lt;song&gt;, right?", sound 3; the song plays about 20 frames later, when the text box is open | `Jukebox::choose` and `playChosen` right away; the chosen row turns blue | `a pick plays its song once over the map's…`; route `jukebox` (`playMusic(30, 2)` with 55) | Same; the song starts about 20 frames earlier |
| Leaving (Ctrl) | Cancel: sound 4, the list closes and the farewell; on confirming it, the map's music comes back from the start | Phase `farewell`; `Jukebox::close` → `playBGM` of the active source | `…leaving plays the map's again`; route `jukebox` | Same |
| Sound when hitting an end | `itemMenuCursorUp/Down`: sound 0xb | The UI has no such sound | — | Gap |

## Item trade

The trade of routine 2 (`shop/script_menu.c: openMojyamonShop`, `createMojyaTradeMenu`, `tickMojyaTradeMenu`,
`updateMojyaTradeStrings`): greeting, list of three trades, "You'll really give me…?", the player's yes or no, and "Thanks!",
"Too many Items." or the farewell. The session (`session::ItemTrader`) carries the rules; the `trade_menu` screen
(`apps/game: TradeScreen`) shows them. While it is open, the script's text box is hidden (the original
writes the trade's phrases in that same box). Measured with the original (routine 2 forced through RAM at room02's
counter with table 1; captures `orig/trade-*.png` from the trade folder in `_work/routes-e2/menus-qa/`) and checked in dwe
with the route `trade` in frzl14 (captures `eng/`).

| Setting | Value | What it is |
|---|---|---|
| `trade.phase` | text | `greeting`, `list`, `confirm`, `choice`, `thanks`, `bag_full`, `farewell` or `closed` |
| `trade.{speaker,player,line_key,offered_key,label_key}` | text, keys | Who speaks (in the yes or no, the player's name), the phrase (MAPHEAD 1238), the offered item and the titles "You have" and "Will trade" |
| `trade.{cursor,choice,total}` | numbers | The cursor's row, the marked answer (0 yes, 1 no) and how many rows there are |
| `trade.row.<i>.{give,get,owned,traded}` | keys, booleans | Each trade: what the player gives, what it receives, whether the bag has it and whether it was already done |

| Behavior | Original | dwe | Test | Status |
|---|---|---|---|---|
| List window | `createMojyaTradeMenu`: box from (72, 40) to (295, 123); titles at (bx + 20, by + 5) and (bx + 122, by + 5); line at (4, 21) of 215; rows every 18 px from by + 27, name at bx + 8 and what the other gives 116 px further right; 212 × 18 cursor | `trade_menu/structure.rcss` with those measures × 3 | Route `trade` | Same |
| What the bag does not have | `mojyaTradeFillItemList` (without bit 0x80): in red (color 3); confirming plays `playSound(0, 0xb)` and nothing happens | `missing` row in red; `refuse_sound` | `mojyaTradeFillItemList marks…`; route `trade` | Same |
| Trade already done | The original (the trade list's text): a yellow "-" (color 7) in front; the name shifts about 12 px and the other column about 9 | The mark shifts the name 11 px and the other column 9 (`.row.traded .give`) | Route `trade`; `route-bulk-trade` (in Spanish "Piloto auto." already fits with the mark) | Assumption: the name 11 px instead of 12 |
| Phrases | MAPHEAD 1238: 8 greeting, 9 "What will you give me?", 11 "You'll really give me…?", 12 yes or no, 13 "Thanks!", 14 "Too many Items.", 10 farewell | `item_trader.lines` | `the item trader reads the original ruleset…` | Same |
| Yes or no | `showMapheadSelection(0xc, 0xfd, 2)`: the player speaks; the bar measures the longest option × 12 + 2 px; up and down wrap around; canceling is "no" except with trigger 0x31 (then 0xb plays) | Phase `choice` with those rules | Route `trade`; `openMojyamonShop takes the table…` | Same |
| Moving and jumping to the ends (Shift held, R1 on the gamepad) | `itemMenuCursorUp/Down`, with R1 to the first or last row; the rows do not wrap | `TradeCursor` and the `walk` action | Route `trade` | Same |
| Sounds | Cursor (0, 2), confirm (0, 3), cancel (0, 4), refusal (0, 0xb); open and close the box (0, 0) and (0, 1) | The UI's cues (move, confirm, cancel) and the refusal through `SoundPort` | — | Gap: opening and closing the box do not play; on a row with no item the confirm cue also plays; moving plays at the ends |
| Savanna sound | `playShopSoundOnlyInSavannah`: (0, 0x17) only on screen 0x83 | It is not there | — | Gap: the trade is not on that map |
| Column titles | `MAIN_D_801307CC` ("You have Will trade"), a single string | `script_menu_trade_label`, split in two at the spaces | Route `trade` | Assumption |

## Bird transport

The transport of routine 10 (`shop/script_menu.c: tickBirdraTransport`, `createBirdraTransportMenu`,
`tickBirdraTransportMenu`, `renderBirdraTransportMenu`; `script_common.c: calculateBirdramonMenuStrings`,
`createShopBitsBox`): greeting, money box and list of destinations, "Are you sure?", the player's yes or no and travel or
farewell. The session (`session::BirdTransport`) carries the rules; the `bird_transport` screen (`apps/game:
BirdTransportScreen`) shows them and, after the yes, tells the story which section to continue in. While it is open, the
script's text box is hidden. Measured with the original (routine 10 forced through RAM at room02's counter;
captures `orig/` of `_work/routes-e2/bird-transport/`) and checked in dwe with the room12 route (Enter to talk,
arrows to move, Enter to confirm, Ctrl to cancel).

| Setting | Value | What it is |
|---|---|---|
| `bird.phase` | text | `greeting`, `list`, `confirm`, `choice`, `farewell` or `closed` |
| `bird.{speaker,player,line_key,bits_key}` | text, keys | Who speaks (in the yes or no, the player's name), the phrase (MAPHEAD 1238) and that of the money box |
| `bird.{money,cursor,choice,total}` | numbers | The money, the cursor's row, the marked answer (0 yes, 1 no) and how many rows there are |
| `bird.row.<i>.{name,cost,affordable}` | key, number, boolean | Each destination: the map's name, the price and whether it can be chosen (the money pays it and the story lock does not close its map) |

| Behavior | Original | dwe | Test | Status |
|---|---|---|---|---|
| List window | From (90, 13) to (309, 149); PLACE at (8, 6) and POINT at (162, 6) (`ITEM_MENU_HEADER_UVS` 6 and 7); line at y 15; six rows every 18 px from (8, 20); the price ends at x 196 and the name is cut where it starts; 202 × 18 cursor at (5, 18); 5 px bar at x 210 from y 26 to 121 | `bird_transport/structure.rcss` with those measures × 3 | Room12 route | Same |
| Money box | `createShopBitsBox`: from (8, 15) to (89, 47), "Bits" from x 30 and the money right-aligned | `#bits-box` | Room12 route | Same |
| Destination that cannot be afforded | Without bit 0x80: in red (color 3); confirming plays `playSound(0, 0xb)` and nothing happens | `unaffordable` row in red; `refuse_sound` | `birdraFillTargetList lists…`; room12 route | Same |
| Destination closed by the story lock | It does not exist (the original has no lock) | In red like one that is not affordable; confirming plays the refusal and does not charge (`BirdRow::open`) | `a place the story lock keeps closed…`; `route-stage4-birdramon` | Port improvement (decision of the coordinator, 2026-10-31) |
| Destination name | The map's `MAP_NAME_PTR`, without the sign's spaces | The key of the map's name (`mapNameKeyOf`), trimmed | Room12 route | Same |
| Yes or no | `showMapheadSelection(7, 0xfd, 2)`: the player speaks; up and down wrap around; canceling is "no" except with trigger 0x31 (then 0xb plays) | Phase `choice` with those rules | `cancel answers no…` | Same |
| The yes | State 5: 247 = map, 248 = entrance, the money drops by the price and the script jumps to MAPHEAD 0x4e3 | `travel()` and `screenClosed(jump)` | `the yes writes the place…`, `routine 10 waits…`; room12 route (arrives at koda00 with 300 bits) | Same |
| Moving and jumping to the ends (Shift held, R1 on the gamepad) | `itemMenuCursorUp/Down`, with R1 to the first or last row; they do not wrap | `BirdCursor` and the `walk` action | `bird_cursor_tests.cpp` | Same |
| Sounds | Cursor, confirm and cancel; open and close the boxes | The UI's cues and the refusal through `SoundPort` | — | Gap: opening and closing the boxes do not play |
| Marked row | The text of the cursor's row dims to gray | The gray cursor over the text, which stays white | Room12 route | Gap |

## Card shop

The menus of gias06b's card shop that routine 14 opens (`shop/script_menu.c: openBuyCardMenu`,
`openSellCardMenu`, `createCardMenu`, `tickCardMenu`; `ITEM_MENU_TYPE` 3 for buying and 4 for selling). The session
(`session::CardShop`, the ruleset's `card_shop` section) carries the day's six cards, charges and pays; the game's
`card_shop` screen (`apps/game: CardShopScreen`) shows the list, the "Is it OK?" box and the quantity box, with the
shopkeeper talking below. Measured with the original (routine 14 forced at room02's counter by writing the
buy or sell trigger, the money and the copies into RAM; captures `original_*.png` and dwe's `dwe_*.png` in
`_work/routes-e2/menus-qa/card-shop/`, with a game of 3000 bits, cards 58 to 63 for sale and copies of 1 to
3).

| Setting | Value | What it is |
|---|---|---|
| `card_shop.phase` | text | `talk` (no cards: "sold out"), `list`, `confirm`, `amount` or `closed` |
| `card_shop.{mode,speaker,line_key}` | text, keys | `buy` or `sell`, the shopkeeper's name (by its species) and what it says (MAPHEAD 1235) |
| `card_shop.{total,top,cursor}` | numbers | The rows of the list, the first one visible and the cursor's among the six visible |
| `card_shop.row.<i>.{name,price,count,rarity,ink}` | key, numbers, text | The six rows; `rarity` -1 on an empty row; `ink` `new` (yellow), `stuck` (red) or nothing (white) |
| `card_shop.{choice,confirm_key}` | number, key | Yes (0) or no (1) and "Is it OK?" (MAPHEAD 1240, line 0) |
| `card_shop.amount.{name,rarity,price,value,total}` | key, numbers | The sale's quantity box; the total is capped at the maximum money |

| Behavior | Original | dwe | Test | Status |
|---|---|---|---|---|
| Buy window | `ITEM_MENU_POS`: x 105 to 295, y 22 to 151; MENU and PRICE from SYSTEM_W, six rows every 18 px with the rarity card (12 × 12 from SYSTEM_W), the name and the price that ends at x 165 | `card_shop/structure.rcss` with those measures and the original skin; same list, prices and colors as the original | `dwe_buy_list.png` against `original_buy_list.png` | Same |
| Buy colors | `calculateCardMenuStrings`: yellow (7) the one the player does not have, white the one it has, red (3) the one it cannot afford | `ink` `new`/`stuck` (#f8f800, #e73129) | same | Same |
| Sell window | x 89 to 311; COUNT at (182, 5); 64 rows, one per card from 1 to 64, empty the ones it does not have; pays half | `#frame.mode-sell`; `CardShop::saleRows` | `dwe_sell_list.png`; `the card shop buys copies back for half their price` | Same |
| Name cut by the price | `calculateCardMenuStrings`: the price (4 digits) is written over the end of the name ("H-Kabuterimo2500") | The name is cut at x 129 of the window | same | Same |
| "Is it OK?" for buying | `createSingleCardShopMenu`: 112 × 42 box at (104, 99); left yes, right no; yes buys and closes everything, no or cancel goes back to the list (sound 4) | `#confirm` with those measures; phase `confirm` | `dwe_buy_confirm.png`; 3000 → 2900, copies 0 → 1 | Same |
| Sale quantity box | `createItemMenuAmountBox` (`AMOUNT_BOX_LAYOUT` US): 130 × 83 at (95, 78), name at (26, 7), price at y 27, quantity in a 26 × 14 gap at (85, 42), total at y 65; rules at y 23 and 60; it opens with sound 3 | `#amount` with those measures | `dwe_sell_amount.png` against `original_sell_amount.png`; 2 at 2500 → +5000, copies 3 → 1 | Same |
| Quantity: one, ten and all (arrows; E, the gamepad's north button; Q, the west button) | `tickItemMenuAmountBox`: ±1; alt held ±10 if it does not overshoot; square sets the maximum (sound 3) | `CardShopScreen::press` | — (the control channel sends neither E nor Q) | Same |
| Jump to the start or the end (Shift with up or down, R1 on the gamepad) | `tickCardMenu`: R1 held | The `walk` action held | — | Same |
| Cancel in the list (Ctrl) | `tickCardMenu`: closes (sound 4) and the script goes on | Phase `closed` and the story goes on | Test game | Same |
| Failure and deal | `playSound(0, 0xB)` on an empty row or one that cannot be afforded; `playShopSoundOnlyInSavannah`: (0, 0x17) only in gias06b | `CardShop::playFailSound`, `playTradeSound` | `the card shop's rules carry the fail sound ...` | Same |
| The script's box under the shop | `showMapHeadTextbox`: the shopkeeper talks in the bottom box while the list is open | The script's box is hidden and the shop paints its own in the same place | `dwe_buy_list.png` | Same |
| Animation when opening and closing the boxes | The boxes grow when opening | They appear at once | — | Gap |

## Bulk trades

An engine improvement, optional and on by default: the `bulk_trades` setting (row "Bulk trades" of the pause menu
options, saved in the user settings alongside `smooth_motion` and `save_notice`) lets you make several trades of the same item at once
and open several card packs, instead of going through the dialog once per unit.
Off, everything goes one at a time like the original. Each unit follows the original's rules to the letter; only
how many times the dialog is walked through changes.

**The original's rules per unit** (read in the decomp with dw-data and measured on the original on 2026-10-30, see
"Item trade" and "Card shop"):

- Trade (routine 2, state 5 of `shop/script_menu.c:1088`): `giveItem(what the trader gives, 1)` first; if the
  bag accepts it, `removeItem(what the player gives, 1)` and the trade's trigger is set (5 + the trade's number; 14
  when all nine are done). With no room, "Too many Items." and nothing changes. Only an item the bag has can
  be offered. There are no random rolls.
- Pack (script 126 of gias06b and routine 14, `rollCardPack`): the script asks for 500 bits (`money >= 500` to offer it),
  charges them (`reduce_money 500`) and the routine draws 3 cards, each with `rollCard` (rarity with `randomLimit(100)`,
  then a card of that rarity) and its copy (at most 9). Then the script runs routine 22 (medals). There is no
  limit of packs per day.

**How equality is kept.** The batch has no rules of its own: it repeats the unit.

- Trade: `makeTrades` calls `makeTrade` N times (the same function as the usual yes) and stops at the first trade
  the bag rejects or when the item runs out. The box's maximum (`tradeLimit`) tries those same trades on
  the bag and leaves it as it was: it counts what the player has and the room left, including the room a stack
  that runs out frees.
- Packs: the script charges the first one as usual; `CardShop::openPacks(N)` charges each extra pack (`pack_price: 500`
  in `card_shop.yaml`, the script's price) and draws its 3 cards in the same order with the same generator, so with
  the same seed the same cards come out as with N single purchases in a row. The maximum is 1 + money / 500. Routine
  22 runs once at the end; since it only looks at the collection, which only grows, the result is the same.

**Screen.** Trade: when choosing a row that could be traded more than once, the "How many?" box (`trade_menu`,
phase `amount`) shows what is given and what is received times the quantity. While it is up, the list stays on its titles
and the chosen row, and the box goes between the list and the text box with 12dp free on each side; then come the trader's question, the
yes or no and "Thanks!" as usual. Packs: with money for more than one pack, routine 14 opens the `CardPacks` screen
(`card_shop`, mode `packs`): "How many packs?" in the sale's quantity box (price, quantity and total), and
then the list of the cards that came out, grouped, with the titles "Card" and "Copies" in words (not the shop's
MENU and COUNT labels), how many of each and in yellow the ones the player did not have (the same ink as the original's
purchase). Without money for a second pack, the routine
goes on with the original's text boxes. Controls: left and right ±1, up and down ±10 (they stop at 1 and at
the maximum), Q (the gamepad's west button) sets the maximum, Enter accepts, Ctrl cancels (in the trade it goes back to the list;
in the packs it opens only the one already paid for).

| Setting | Value | What it is |
|---|---|---|
| `bulk_trades` | boolean | The option |
| `trade.amount.{value,most}` | numbers | The trade's quantity and the maximum |
| `card_shop.packs.{value,most,price,total}` | numbers | The packs chosen, the maximum, the price and the total |
| `card_shop.packs.{opened,cards,new}` | numbers | What came out: packs, cards and new cards |

| Behavior | Original | dwe | Test | Status |
|---|---|---|---|---|
| N trades at once | N times state 5 | `makeTrades` = N × `makeTrade` | `bulk trade: N trades at once leave the bag and the triggers as N single trades` | Engine improvement (same per unit) |
| Trade maximum | — | `tradeLimit`: item it has and room in the bag | `bulk trade: the limit is…`, `bulk trade: a full bag stops it…` | Engine improvement |
| N packs at once | N times 500 bits and 3 `rollCard` | `openPacks`: same cards, money and generator state | `bulk packs: N packs at once give the cards, money and generator of N single packs` | Engine improvement (same per unit) |
| Pack maximum | `money >= 500` per pack | 1 + money / 500 | `bulk packs: the money limits the packs…` | Engine improvement |
| Summary | One box per card | Grouped list, new ones in yellow | `bulk packs: the summary groups…`; route `route-bulk-cards` | Engine improvement |
| Quantity | — | `BulkAmount` | `bulk_amount_tests.cpp`; routes `route-bulk-trade` and `route-bulk-cards` | Engine improvement |
| Trade in the modern look | — | The list and the box to the left of the frame (`left: 30dp`), away from the minimap | Route `route-bulk-trade` (`expect_clear` at 1280x720, 1920x1080 and 1024x768) | Clear with the medium minimap in any aspect ratio and the large one on a wide screen |

## Merit exchange

The Shogun's exchange in gkyo02 that routine 1 opens (`shop/script_menu.c: openMeritShop`; `ITEM_MENU_TYPE` 6 for
giving cards and 7 for taking items). The session (`session::MeritShop`, the ruleset's `merit_shop` section) adds and subtracts the
merits; the game's `merit_shop` screen (`apps/game: MeritShopScreen`) shows the greeting, the three options, the
lists, what the card is worth and the yes or no's, with the points box at the top left and the script's box below
covered (`cover()`), like the card shop. Controls: arrows to move, Enter to accept, Ctrl to cancel,
Shift with up or down jumps to the start or the end (R1 on the gamepad). Test in dwe: route
`_work/routes-e2/menus-qa/merit-shop/merit.yaml` (game in gkyo02 with 150 merits and cards 1, 2 and 3; captures
`eng/merit-*.png`). Measured on the original on 2026-10-31 (routine 1 forced in room02 with 150 merits and cards 1,
2 and 3; `_work/routes-e2/measure-2026-10-31/merit-orig-vs-dwe.png`). The x of the emulator's capture are the game's;
the y match `(y - 8) × 240 / 224` (224 lines stretched to 240), and with that the boxes fall at the decomp's y.

| Setting | Value | What it is |
|---|---|---|
| `merit_shop.phase` | text | `greeting`, `choice`, `card_list`, `card_worth`, `card_choice`, `item_list`, `item_choice` or `closed` |
| `merit_shop.{list,speaker,player,line_key,shopvar,choice}` | text, keys, number | `cards`, `items` or nothing; who speaks (the shopkeeper by its species, or the player in the options); the phrase (MAPHEAD 1236); what the card is worth for `{shopvar}`; the marked option |
| `merit_shop.{points_shown,points_key,points}` | yes/no, key, number | The points box ("Points {merit}", line 7 of 1220) |
| `merit_shop.{total,top,cursor}` | numbers | The rows of the list, the first one visible and the cursor's |
| `merit_shop.row.<i>.{name,value,rarity,stuck,icon,icon_rect}` | key, numbers | Cards: name, copies and rarity (-1 on an empty row); items: name, price, icon and `stuck` (red) if it cannot be taken |

| Behavior | Original (decomp) | dwe | Test | Status |
|---|---|---|---|---|
| Greeting and options | State 0: "You want 'Merit' too?…" (line 0); state 3: opens the points box and the player chooses "Show a … card / Use Merit points / See ya" (line 1); the cursor goes back to 0 | Phases `greeting` and `choice` | `eng/merit-02-greeting.png`, `merit-03-choice.png` | Same |
| Points box | `createShopBitsBox(0)`: the shop's money box (x 8 to 90, y 22 to 55) with "Points" and the figure | `#points-box` with those measures | `eng/merit-04-card-list.png`; measured: x 8 to 89, "Points" from x 30 to 75 and the figure ends at x 67 | Same |
| Card list | Type 6: x 124 to 290, y 22 to 151, 146 cursor; MENU at (8, 5) and COUNT at (128, 5); the 64 cards (empty the ones it does not have), name and copies, always white; "What did you bring?" (line 5) | `#frame.list-cards`; the name is cut 104 pixels after it starts | `eng/merit-04-card-list.png`; measured: box from x 124 to 289, name from x 150 with twelve letters ("H-Kabuterimo"), the copies end at x 271, bar from x 278 to 285 (before dwe cut at eleven letters and the copies ended at x 264) | Same |
| What the card is worth | `createMeritCardTradeDialogue`: sound (0, 3), line 7 plus the rarity with `{shopvar}`; then "Give it / Cancel" (line 2) | Phases `card_worth` and `card_choice` | `eng/merit-05-card-worth.png`, `merit-06-card-choice.png` | Same |
| Item list | Type 7: x 105 to 295, 170 cursor; POINT (sprite 2) at (128, 5); the items with merit and their price (4 digits), in red (3) those that cannot be taken; "What do you want?" (line 6) | `#frame.list-items`; the name is cut as with the cards | `eng/merit-08-item-list.png`; measured: box from x 105 to 294, the price ends at x 264 (before 260), bar from x 283 to 290 | Same |
| Taking an item | `readSelectedItemMerit`: sound (0, 3) and "OK / Cancel" (line 3); yes subtracts and gives the item | Phase `item_choice` | `eng/merit-09-item-choice.png`; 250 → 230 | Same |
| Empty row or one that cannot be taken | `playSound(0, 0xB)` | `MeritShop::playFailSound` | — | Same |
| Cancel (Ctrl) | In a list it goes back to the options (state 0xa); in the options it answers "See ya" except with trigger 49 | Phase `choice`; `choiceCancellable` | Route `merit-shop` | Same |
| Leaving | State 6: closes the points box and the script goes on, with no farewell | Phase `closed` | `eng/merit-11-after.png` | Same |
| Animation when opening and closing the boxes | The boxes grow when opening | They appear at once | — | Gap |

## Shop money box

The box with the money that the shop opens next to buying and selling (`script_common.c: createShopBitsBox`,
`tickShopBitsBox`, `renderShopBitsBox`), in the `shop_menu` document. The game publishes `shop.bits_shown` (`ShopScreen`)
and the logic splits line 6 of the shop's phrases ("Bits {bits}", MAPHEAD 0x4c4) into the label and the figure. Measured
with the original (routine 8 forced in room02 with 0 and 1234 bits; captures `orig/shop-bits-*.png` of
`_work/routes-e2/menus-qa/discard-bits/`).

| Behavior | Original | dwe | Test | Status |
|---|---|---|---|---|
| Position and size | `createShopBitsBox`: rect (-0x98, -0x62, 0x52, 0x21) from the center, x 8 to 90 and y 22 to 55 | `#bits-box` in `shop_menu/structure.rcss` and the original's box in the skin; within a pixel | Route `shop` (room06) | Same |
| Label and figure | `renderShopBitsBox`: two strips of the line, at (22, 4) and (22, 17); the figure ends at x 67 however many digits it has | "Bits" on the first row and the figure right-aligned up to x 67 on the second | Route `shop` | Same |
| When it appears | `openShop` state 3 (and `openRecycleShop`): with the first choice; it stays during the list, the quantity and "Anything else?"; `triggerBoxCloseFlag(2)` closes it at the farewell (state 6) | `ShopScreen`: `bits_shown` from phase `choice` until `leave` | Route `shop` | Same |
| It updates when buying or selling | `UPDATE_SHOP_BIT_BOX` after each purchase or sale | The figure comes from `shop.money`, which the game publishes again | Route `shop` (1234 → 1134) | Same |
| Routine 15 (the box during a dialog) | `CALL_ROUTINE 15` → `createShopBitsBox(1)`: the box with line 6 of the shopkeeper that variable 254 names and the money at that moment (`tickShopBitsBox` only rewrites it when called again, as after `reduce_money` in the card shop); `CALL_ROUTINE 16` (`triggerBoxCloseFlag(2)`) closes it | `MoneyBoxScreen` in the `money_box` document, with the same rect as the shop's; 16 hides it | `money_box_tests.cpp`; `w11-missing/shots/money-box-after.png` (forced in gias06b). Routine 15 not measured on the original | Assumption |

## Recycle shop (gias06b)

The Gear Savanna shop (script 126, section 0x52: the counter's confirm cell) asks "regular item,
recycled item or next time". "Regular" sets the seven assortment triggers, clears trigger 3 and calls
routine 8 (`openShop`); "recycled" sets trigger 3 and routine 8 opens `openRecycleShop`, which sells what the
defeats took (`handleItemLoss`: 30 % of the occupied bag cells; those in `RECYCLABLE_ITEMS` go to the
assortment, up to 99 of each). Tester report (#175): "the recycle shop doesn't work". Measured in dwe
(`_work/routes-e2/recycle-shop-175/`): the regular purchase (7 items), the recycle one with an assortment (Restore 4000, Black
Wing 5000; purchase, "Thanks!", goes back to the choice), the defeat that fills the assortment (3 of 10 cells: `black_wing`,
`rainbowhorn`, `flamingmane` ×2) and the empty shop ("I don't have anything to sell.", line 1, and end of the visit)
worked; what failed was the choice, which stayed on screen under the shop.

| Behavior | Original | dwe | Test | Status |
|---|---|---|---|---|
| The choice leaves the screen when the shop opens | `openShop` and `openRecycleShop` talk with `showShopkeeperTextbox` → `showMapHeadTextbox(idx, owner, 0, 0xff)`: the script's same dialog box 0, so the shopkeeper's phrase replaces the choice | `PlaySession` covers the script's box (`StoryTextbox::cover`) while `ShopScreen` is active, as it already did with the card shop, the trade and the others; when the shop closes it does not come back until the script changes phrase. Before, the choice "I want a regular item / recycled item / Next time" stayed under the shop | Route `route-recycle-shop.yaml` (fails before: "story_textbox is still shown after 5 s"; passes after, 36 of 36 steps) | Same |
| Empty recycle shop | `openRecycleShop` state 0: with no assortment, the shopkeeper's line 1 and end (state 2) | `ShopScreen::start`: `nothing_to_sell` (line 1) and the visit closes | Route `route-recycle-shop.yaml` (`recycle-empty`) | Same |
| Trigger 3 after recycling | The script's "recycled" branch leaves it set; the original's other shops clear it before routine 8 | Same: the script leaves it | — | Same |
