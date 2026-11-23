# Extra evolutions: all species as a partner

Optional improvement, **on by default** (an exception to the rule of "the original by default", decided on 2026-11-10):
every species with a model that the original never lets you raise is reached by evolving in a normal game, with its
combat clips or with the ones the engine makes for it. It is chosen from Folder Mode's rulesets when a new save is
created: the [mode selector](../../architecture/game-variants.md#choosing-a-variant) defines the save type and replaces
the old hidden-content activation. The legacy [rules screen](../apps/game.md#hidden-content) still offers the row
"All species as a partner" (Yes by default); with No the game uses the original's rules with nothing changed. The saved
game remembers the ruleset.

This document is the design: which species come in, where they come from, with what requirements and why. The format of
the patches is in [rulesets](../../architecture/rulesets.md#per-species-patches) and the check rules in
[evolution](evolution.md).

## Scope

The disc carries 178 species with a model: the player, the 61 partner forms, 3 that are only raised with items (`panj`,
`ggdr`, `mtet`) and 113 more. Of those 113, **60** come in:

- The 47 species that only exist as enemies (numbers 67 to 113 of the table): 21 rookies, 23 champions and 3 ultimates.
- `vand` (66), which the disc only carries as a boss, and `muge` (115), which it carries as a level 5 enemy.
- The 11 character models (`anlg`, `jiji`, `tens`, `tono`, `jure`, `hagu`, `brik`, `tirs`, `brak`, `scud`, `puti`):
  they carry only 2 to 8 field clips, so the engine makes the rest when the partner loads
  ([partner clips](../engine/animation.md#partner-clips)); see [Character models](#character-models).

Left out, with the reason:

- **The 53 enemy copies** (`egob` and numbers 128 to 179): they are the same species, with the same name, as a partner
  form, with a smaller model and fewer clips. That species is already raised through its partner form, so the copy adds
  nothing and would only duplicate cells of the tree.
- `panj`, `ggdr` and `mtet` stay as they are: with an item in the original and by evolution with the
  [hidden content](evolution.md#hidden-content).

The bosses come in (`vand`, `muge`): their models have the combat clips.

## Rulesets

The option is three more rulesets, one for each combination the rules screen already offers:

| Ruleset | Extends | For |
|---|---|---|
| `todas` | `original` | All species, without the hidden content |
| `todas_oculto` | `oculto` | All species and the hidden content |
| `todas_oculto_mega` | `oculto_mega` | All species, the hidden content and the Ultimate → Mega step |

The three read the same `all_species` section (`rules/todas/all_species.yaml`): the manifest takes it from the `todas`
folder with `sections_from`, so the data is there only once. The section has the format of `species_extra` (the same
per-species patches) and also a `wake_up` list with the format of `specials.wake_up` of `evolution.yaml`, which is added
after the original's specials.

- A species that another section of the chain already patches (`species_extra` or `mega_step`) keeps that patch and its
  `all_species` entry is not read: with the hidden content `vand` is the one from `oculto` (with its night gate), and
  with the Mega step `muge` is the Mega from `oculto_mega`, which comes from `mgdr` and `ggdr` instead of `tank` and
  `gard`.
- The data is our own design and by id; the raising and gain rows are **copied** from a form of the original (`copy`),
  no numbers from the disc are written.

## Original's priority

No new form takes an evolution away from anyone in the original. The `all_species` forms are **reserve** targets
(`SpeciesPatch::yields`): the natural check (`OriginalSelection`) first looks at the original's targets with its usual
rules and only if none passes does it look at the reserve ones, with the same rules (points, tie, stat average). After
both comes the original's fallback (`nume` at 96 rookie hours).

- Tiebreak: if one of the original's targets passes, it wins even if a reserve one has more points or a better average.
- With the option off there are no reserve targets and the selection is the original's step by step.
- The `all_species` wake-up specials go **after** the original's in the list: the original's one in `mayo02` stays the
  same (no new special uses that map).

## Rookies

In the original the in-trainings almost never fail: `koro`, `tuno` and `toko` have two targets that between them cover
the six stats and one of them gives the bonus for free (`techs: 0`), so one of the original's always passes. Only `tane`
fails, when its highest stat is attack and its weight is outside 10 to 20. That is why the new rookies come up in two
ways:

- **Natural check from `tane`** (8 plant, water and insect rookies): attack as the highest stat, as the gap the original
  leaves asks for, and each with its weight band (5, 25, 35 or 45 ± 5) and its care (0 mistakes or 2 or more). Two forms
  share a band and are told apart by the mistakes, as `agum` and `gabu` are told apart by the stats.
- **Wake-up special** (13 fire, ice, ogre, toy and darkness rookies): like the original's special in `mayo02`, an
  in-training (`koro`, `tuno`, `toko` or `tane`) that wakes up on the species' map evolves into it with a roll of 50 out
  of 100. Each species has its map, in its area: the tunnel for the fire lizard, Toy Town for the toys, Freezeland and
  the Ice Sanctuary for the ice ones, the Ogre Fortress for the ogres, the Mansion for the dark lizard, the Great Canyon
  for the rock one.

## Champions and ultimates

The requirements follow the original's style (3 of 4 groups: stats, care mistakes, weight ± 5 and a bonus of battles,
techniques, discipline or happiness). The new forms are heavier or more neglected than the original's in their line, with
somewhat lower stat bars (60 to 90 in champion): since they only count when none of the original's passes, they are the
reward for a raising that the original sends to `nume`. Each new champion comes from at least one new rookie and, where
the theme calls for it, also from one of the original's (`elec` to `sima`, `palm` to `redv`, `agum` to `varm`, `gabu` to
`grur`, `piyo` to `akat`, `penm` to `tuti` and `gole`, `beta` to `geco`, `pata` and `elec` to `pidd`). The new ultimates
come from new champions and from the original's (`nume` and `ogre` to `waru`, `dori` to `tkka`, `sead` to `wrse`, `devi`
and `bake` to `vand`).

No new rookie or champion is left without an exit: each one has targets (the original's and new ones) and the reach test
checks that each new path is picked by the check with legal values.

## Balance

- **Gains and raising.** Each form copies the rows of a form of the original of its level and theme (the design column
  `like`): stat gains (`stats_gains: {copy: ...}`) and raising (`raise: {copy: ...}`: hunger, sleep, poop, default
  weight, energy and training), so training and caring for a new form costs the same as for its sibling. The gains of a
  target that scales (`devi`, `nume`, `scum`, `nani`, which only have brains) are never copied: `pcsc` and `gere` copy
  those of `vegi`, `icdv` those of `garu`; `vand` uses the design gains of the hidden content.
- **Favorite food.** One from the item table, of its theme (meat for the fire ones, ice mushrooms for the ice ones, fish
  for the water ones...).
- **Techniques.** Each species fights with its own list of enemy attacks. The finisher is its own when it has one; if
  not, one from its line is put in an empty slot whose launch points work for its model (`effect_type` takes those of a
  sibling form when all its attacks still work with them). A rookie that takes a champion's finisher uses it with power 95
  (`partner_moves`). Six forms have no finisher that works with their model (`sngb`, `dkrz`, `gobr`, `inse`, `yanm` and
  `gard`) and fight without it. `muge` uses its own boss one with the partner values of the Mega step (power 240), fights
  with type 3 and goes at 0.6 size in the field.
- **Technique on evolving.** A reserve form whose first specialty teaches none of its attacks (`clea`, `sabd`) learns and
  equips the first one in its list, like the developer's evolve command.
- **Animations.** Enemy models have no care or gym clips: the field puts in their place one they do have
  (`MissingClips::StandIn`), as it already does with any partner. Character models first receive all their partner clips
  from their recipe (see below), so none is left to substitute.

## Character models

The 11 character models come in with their stage (`level`, which the table leaves at 0 or at another stage), their
technique row copied from the species their clips come from (`moves: {copy}`, so each technique plays its slot's clip and
launches from the bones that species uses, `effect_type`) and a size for the giants (`field_scale`: `tirs` 0.7, `scud`
0.5, `tono` 0.45, `jure` 0.38, `brak` 0.32: about 450 tall, like the big Ultimate partners; in combat they fight with
their radius and height at that size). Their partner clips come from a recipe in `animations/partner_clips.yaml`:

| Species | Clips | Donor | Weak clips |
|---|---|---|---|
| `tirs` | reassigned, same hierarchy | `tyra` | — |
| `scud` | reassigned, same hierarchy | `scum` | — |
| `puti` | reassigned, same hierarchy | `tuno` | no rebirth clip (28) |
| `anlg` | reassigned with a bone map | `andr` | — |
| `jiji` | reassigned with a bone map | `mtma` | the beard and the staff follow their parent; the staff rises with the arm |
| `tens` | reassigned with a bone map | `mame` | — |
| `brak` | reassigned with a bone map | `mono` | the long neck moves stiffly with its base |
| `tono` | procedural, the donor's timings | `etem` | all are simple body movements; no rebirth clip (28) |
| `jure` | procedural, the donor's timings | `vegi` | all are simple body movements |
| `hagu` | procedural, the donor's timings | `elec` | simple movements; the body does not lean (`upright`), it shifts; no rebirth clip (28) |
| `brik` | procedural, the donor's timings | `ogre` | all are simple body movements; no rebirth clip (28) |

The rebirth clip (28) does not exist in `tuno`, `etem`, `elec` or `ogre`, so their four species go without it, just like
those donors. `puti` is small and floats at the height of the player's chest: right after the evolution it is behind the
player, like any small partner, and it is seen as soon as they move. The characters in the scenes do not change: only the
partner's model receives the new clips.

## Evolution tree

Each new form has its cell in the tree (`chart` with `y: 0`: outside the grid, it takes the first free slot of its level's
box, in pages if it does not fit) with the icon of its sibling form and palette 2. A form that only comes up through a
wake-up special (no path leads to it) also has a cell. A cell's detail draws like the original's up to 5 sources and 6
targets; the extra paths exist but are not drawn.

## Table

| Species | Stage | From | Key requirement | Next |
|---|---|---|---|---|
| `flar` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `tunn01` (50%) | `mera`, `bird`, `varm`, `blmr` |
| `toya` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `omoc01` (50%) | `mono`, `tank`, `gard`, `brik` |
| `clea` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `omoc02` (50%) | `tyra`, `gard`, `varm`, `brik` |
| `ykag` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `frzl01` (50%) | `yuki`, `blmr`, `gole` |
| `icem` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `icsa01` (50%) | `yuki`, `hyog`, `gole` |
| `sngb` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `frzl05` (50%) | `ogre`, `hyog`, `tuti` |
| `dkrz` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `dgha01` (50%) | `bird`, `sabd`, `icdv` |
| `sham` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `ogre00` (50%) | `ogre`, `fuga`, `hyog` |
| `gobr` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `ogre01` (50%) | `ogre`, `fuga`, `pcsc` |
| `psyc` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `gomi01` (50%) | `garu`, `grur`, `sima` |
| `gotu` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `gcan01` (50%) | `dori`, `nise`, `gole`, `tank` |
| `inse` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `miha01` (50%) | `mono`, `varm`, `gole` |
| `tuka` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `saib01` (50%) | `bake`, `soul`, `icdv`, `pidd` |
| `modo` | Rookie | `tane` | attack is the highest; 0 mistakes; weight 25 ± 5 | `sead`, `sira`, `geco` |
| `otam` | Rookie | `tane` | attack is the highest; 0 mistakes; weight 5 ± 5 | `geco`, `hoee`, `mris` |
| `arur` | Rookie | `tane` | attack is the highest; 2+ mistakes; weight 25 ± 5 | `vegi`, `redv`, `zass` |
| `doku` | Rookie | `tane` | attack is the highest; 2+ mistakes; weight 5 ± 5 | `kuwa`, `redv`, `gere` |
| `tent` | Rookie | `tane` | attack is the highest; 0 mistakes; weight 35 ± 5 | `kabu`, `kuwa`, `akat` |
| `yanm` | Rookie | `tane` | attack is the highest; 2+ mistakes; weight 35 ± 5 | `kuwa`, `akat`, `sabd` |
| `sndy` | Rookie | `tane` | attack is the highest; 2+ mistakes; weight 45 ± 5 | `akat`, `sabd`, `coca` |
| `mcho` | Rookie | `tane` | attack is the highest; 0 mistakes; weight 45 ± 5 | `coca`, `akat`, `bird` |
| `hyog` | Champion | `icem`, `sngb`, `sham` | attack 80, defense 70; 6+ mistakes; weight 55 ± 5; battles 15 | `skul`, `waru` |
| `pcsc` | Champion | `gobr` | brains 80; 8+ mistakes; weight 50 ± 5; happiness 60 | `etem`, `digi`, `scud` |
| `sima` | Champion | `psyc`, `elec` | speed 80, brains 70; 0–3 mistakes; weight 50 ± 5; discipline 70 | `giro`, `houo`, `tens` |
| `tank` | Champion | `toya`, `gotu` | HP 800, defense 80; 0–5 mistakes; weight 60 ± 5; battles 15 | `andr`, `tkka`, `muge`, `brak`, `anlg` |
| `redv` | Champion | `arur`, `doku`, `palm` | MP 700, attack 70; 6+ mistakes; weight 50 ± 5; techniques 21 | `picc`, `digi`, `jure` |
| `jmoj` | Champion | `sngb` | HP 800, attack 70; 0–5 mistakes; weight 55 ± 5; battles 10 | `mame`, `skul` |
| `nise` | Champion | `gotu` | attack 80, speed 60; 4+ mistakes; weight 60 ± 5; happiness 50 | `mtgr`, `tkka`, `tirs`, `anlg` |
| `tuti` | Champion | `sngb`, `penm` | HP 700, brains 70; 4+ mistakes; weight 55 ± 5; happiness 40 | `mame`, `mtma` |
| `pidd` | Champion | `tuka`, `elec`, `pata` | MP 800, speed 70; 0–1 mistakes; weight 50 ± 5; discipline 80 | `houo`, `andr` |
| `gere` | Champion | `doku` | HP 700; 8+ mistakes; weight 60 ± 5; battles 20 | `etem`, `monz`, `scud` |
| `varm` | Champion | `flar`, `clea`, `inse`, `agum` | HP 700, attack 80; 0–3 mistakes; weight 60 ± 5; battles 15 | `mtgr`, `mtma`, `tirs` |
| `fuga` | Champion | `sham`, `gobr` | MP 600, attack 80; 6+ mistakes; weight 50 ± 5; battles 20 | `andr`, `giro`, `waru` |
| `mris` | Champion | `otam` | MP 700, defense 80; 5+ mistakes; weight 60 ± 5; techniques 21 | `hkab`, `mgsd` |
| `gard` | Champion | `toya`, `clea` | defense 90, brains 70; 0–3 mistakes; weight 60 ± 5; discipline 70 | `andr`, `tkka`, `muge`, `anlg` |
| `akat` | Champion | `tent`, `yanm`, `sndy`, `mcho`, `piyo` | attack 60, speed 80; 4+ mistakes; weight 50 ± 5; techniques 21 | `houo`, `picc` |
| `zass` | Champion | `arur` | MP 700, defense 60; 6+ mistakes; weight 55 ± 5; happiness 30 | `picc`, `hkab`, `jure` |
| `icdv` | Champion | `dkrz`, `tuka` | MP 700, attack 80; 6+ mistakes; weight 55 ± 5; battles 20 | `skul`, `mgdr`, `vand` |
| `blmr` | Champion | `flar`, `ykag` | MP 800, brains 70; 0–3 mistakes; weight 50 ± 5; techniques 25 | `andr`, `mgsd` |
| `grur` | Champion | `psyc`, `gabu` | attack 80, speed 70; 6+ mistakes; weight 60 ± 5; battles 20 | `skul`, `mgsd`, `waru` |
| `sabd` | Champion | `dkrz`, `yanm`, `sndy` | attack 70, speed 80; 6+ mistakes; weight 55 ± 5; techniques 21 | `houo`, `mgdr`, `vand` |
| `soul` | Champion | `tuka` | MP 800, brains 60; 5+ mistakes; weight 50 ± 5; techniques 21 | `skul`, `vand`, `waru` |
| `gole` | Champion | `ykag`, `icem`, `gotu`, `inse`, `penm` | HP 700, defense 80; 0–3 mistakes; weight 55 ± 5; battles 10 | `mtma`, `mame`, `tkka`, `brak` |
| `geco` | Champion | `modo`, `otam`, `beta` | HP 700, MP 700; 0–5 mistakes; weight 50 ± 5; happiness 50 | `mgsd`, `wrse`, `tono` |
| `waru` | Ultimate | `hyog`, `fuga`, `grur`, `soul`, `nume`, `ogre` | HP 3000, attack 300, defense 250; 10+ mistakes; weight 75 ± 5; battles 50 | — |
| `tkka` | Ultimate | `tank`, `gard`, `nise`, `gole`, `dori` | defense 300, speed 250, brains 300; 0–5 mistakes; weight 45 ± 5; techniques 35 | — |
| `wrse` | Ultimate | `geco`, `sead`, `mris` | MP 3000, defense 250, brains 250; 8+ mistakes; weight 75 ± 5; battles 40 | — |
| `vand` | Ultimate | `icdv`, `sabd`, `soul`, `devi`, `bake` | MP 5000, speed 400, brains 500; 0–5 mistakes; weight 30 ± 5; discipline 80 | — |
| `muge` | Ultimate | `tank`, `gard` | HP 5000, MP 4000, attack 500, defense 500, speed 300, brains 300; 0–5 mistakes; weight 80 ± 5; battles 100 | — |
| `puti` | In-Training | `bota`, `puni`, `poyo`, `yura` | wakes up in `tunn02` (50%) | `agum`, `piyo` |
| `hagu` | Rookie | `koro`, `tuno`, `toko`, `tane` | wakes up in `fact01` (50%) | `tank`, `gard`, `brik` |
| `brik` | Champion | `toya`, `clea`, `hagu` | defense 80, brains 60; 0–3 mistakes; weight 40 ± 5; techniques 21 | `andr`, `giro`, `tkka` |
| `scud` | Ultimate | `scum`, `pcsc`, `gere` | HP 3000, MP 2500; 12+ mistakes; weight 70 ± 5; happiness 30 | — |
| `tirs` | Ultimate | `tyra`, `varm`, `nise` | HP 4000, attack 350, defense 250; 0–5 mistakes; weight 70 ± 5; battles 50 | — |
| `brak` | Ultimate | `mono`, `tank`, `gole` | HP 5000, defense 350; 0–8 mistakes; weight 85 ± 5; battles 40 | — |
| `jure` | Ultimate | `vegi`, `redv`, `zass` | MP 3500, defense 250, brains 300; 10+ mistakes; weight 70 ± 5; techniques 35 | — |
| `tono` | Ultimate | `geco`, `sira`, `hoee` | HP 3500, MP 3000, defense 250; 8+ mistakes; weight 80 ± 5; happiness 50 | — |
| `jiji` | Ultimate | `unim`, `ange`, `yuki` | MP 4000, brains 400; 0–3 mistakes; weight 30 ± 5; discipline 90 | — |
| `tens` | Ultimate | `yuki`, `nani`, `sima` | MP 3000, speed 250, brains 300; 0–5 mistakes; weight 40 ± 5; happiness 70 | — |
| `anlg` | Ultimate | `gard`, `tank`, `nise` | HP 3000, defense 350, brains 300; 0–5 mistakes; weight 60 ± 5; techniques 40 | — |

## Tests

- `packages/game/evolution/tests/all_species_tests.cpp`: the reserve order (an original target wins even if a reserve
  one scores more; with no original target that passes, the reserve one wins; with no reserve targets the selection is
  the original's) and learning the reserve technique.
- `the user's disc, extracted: ...` (`extracted_tests.cpp`, with `DWE_CONTENT`): each new species is reached from an egg
  with legal values (each path is picked by the check, each wake-up special evolves on its map), no new rookie or
  champion is left without an exit, the `original` tables do not change with the option, and a chain of evolutions
  through new forms from egg to ultimate.
- `packages/engine/ruleset/tests/species_extra_tests.cpp`: `sections_from`, reading `all_species` (an already patched
  species is skipped), `stats_gains: {copy}` and the paths that grow beyond the disc's slots; the pack's rulesets follow
  their schemas (`the game pack's optional rulesets follow their schemas and read`).
- `the user's disc, extracted: the partner clip book gives each species every clip`
  (`packages/engine/animation/tests/clip_synthesis_tests.cpp`, with `DWE_CONTENT`): each character model receives all
  the partner and combat clips, with the duration and root motion of its donor's clip (the hit and effect frames fall
  where they fall in the donor).
- `a copied move row replaces the species' whole row, and a level patch sets its level` (`species_extra_tests.cpp`).
- `the user's disc, extracted: each character model fights with its partner clips`
  (`apps/game/tests/partner_clip_battle_tests.cpp`, with `DWE_CONTENT`): each one fights to the end, its combat clips
  advance step by step like the donor's and each technique is released on the same frame.
- `an upright body moves forward where it would lean` (`clip_synthesis_tests.cpp`) and `a species drawn at another size
  fights with its body at that size` (`packages/session/tests/battle_adapter_tests.cpp`).
- `a form only a ruleset's wake-up special leads to gets its cell` (`apps/game/tests/player_chart_tests.cpp`).
- Route `route-all-species.yaml` ([vertical slice](../../guides/vertical-slice.md)): the option on by default, the tree
  with 121 cells and `tane` → `otam` → `geco` in the game. The routes for the hidden content, the Mega step and the
  canonical stages turn it off and keep going with their 61, 65 and 66 cells.
