# Fishing

Fishing in the original: on two water maps the player takes out the rod from the pause menu, puts on a bait, casts,
waits for a bite, pulls in time and lands the fish by fighting against the line tension. What is caught goes into the
bag.

## What it does

- The pause menu offers **Fish** only on a map with fishing; it is grayed out if the rod or food for bait is missing
  (`main/fish.c: getFishingAvailability`, 0/1/2) **(verified)**. The best rod rules: trigger 46 gives the good rod and
  45 the old one (`getBestFishingRod`) **(verified)**. Bait is any bag item with sort category 2, food
  (`hasFoodItem`) **(verified)**.
- While fishing, the game clock stops (`clock.c: tickGameClock` does not advance with `isFishing`) **(verified)**.
- On taking out the rod, the bait is chosen; with no bait the rod is put away. You aim by turning, charge the cast with
  Confirm and release; the strength (the frames charged plus 5, up to 20) gives the bobber's speed, between 100 and
  200 with the old rod or 400 with the good one (`FISH_tickRod`) **(verified)**.
- Fish swim in zones of the map screen (polygons over the prerendered background) and only in the water, which is the
  part of the screen on one side of a shoreline (`FISH_isFishableWater1` and `2`) **(verified)**.

## Rules

- **Population**: up to 33 fish; every 20 frames, if there are fewer than 32, one is born of the species with the
  fewest fish relative to its weight on the map (`FISH_tickFishPopulation`) **(verified)**. They live between 600 and
  1200 frames; after being scared (a splash nearby) none is born for 30 **(verified)**. The original divides by the
  total without checking whether it is 0; the engine avoids it **(verified)**.
- **Bite**: every 20 frames, a fish less than 200 from the bobber rolls its chance according to bait and species
  (`FISH_findFishToBite`); the bait a species likes attracts it from 600 **(verified)**.
- **Hook**: a fish under 150 nibbles for 20 to 40 frames and pulls for 30; a bigger one, 40 to 60 and 60. You have to
  pull in the first half of the tug (`FISH_tryStrike`); before that it is "too early", after that "too late" and it
  takes the bait **(verified)**. The fish's strength is its size plus 150, and up to 100 more **(verified)**.
- **Tension**: the limit is 5000 with the old rod and 10000 with the good one. Without Confirm it drops 166 per frame;
  with Confirm it rises from 71 to 125 according to size (from 50 to 300), and from 166 to 500 if the fish runs; if
  the fish runs without Confirm it rises from 41 to 83 (`FISH_tickHookedFish`) **(verified)**. At −limit the fish
  escapes; at +limit held for a few frames (from 15 to 5 according to size) the line breaks **(verified)**.
- **Snags**: with the bait eaten and Confirm pressed, from frame 31 and every 20, 1 in 10 snags something from an item
  table (`FISH_tickRod`) **(verified)**.
- **Catch**: the fish gives its item; if the bag is full it is released. The size record per species is saved and the
  partner adds one catch (`PARTNER_PARA.fishCaught`; medal 9 comes at 100, `tamer.c`) **(verified)**. Nothing else
  reads the record (`newRecord` is written and not used) **(verified)**.
- **Sea serpent**: on one map, at certain hours, a sea serpent can appear that bites with 20% every 8 frames; when it
  is pulled out fishing is turned off on the map and the scene from section 0x4e4 of MAPHEAD is launched
  (`triggerSeadramonCutscene`) **(verified)**.

## Data

`dwx` extracts the executable's tables to `data/fishing` (`content/schemas/fishing.schema.json`): the species
(`FISH_KIND_INFO`: item, size, pauses, bait that attracts it), the chances per bait and species (`FISH_BITE_CHANCE`),
the snags (`FISH_BAIT_CHANCES`), the shoreline lines and the maps (`FISHING_MAP_DATA`: zones, serpent hours, cast
reach, serpent route). The rules' constants are in the `fishing` section of the ruleset
([audit](../legal/ruleset-audit.md)).

## Oddities

- `FISH_REEL_TARGET` is never written: a fish that is reeled in swims toward the map's origin **(verified)**.
- The zones and the water are in pixels of the background screen, not in the world: with another camera they change
  place **(verified)**.

## Where it is in the decomp

- `src/main/fish.c`: availability, rod, per-map overlay loading, the player's tick and the serpent.
- `src/fish/fish.c`: `FISH_tickRod` (the rod), `FISH_tickHookedFish` (the hooked fish), `FISH_tickFishPopulation` and
  `FISH_tickFish` (the water), `FISH_updateRodPose` and `FISH_getRodTipPos` (the rod tip).

## In the engine

`packages/game/fishing` (rules, water and rod), `session::FishingAdapter` (tables, rod, bait, catches, records and the
partner's counter) and the fishing screen of `apps/game`, with its camera. The rod, bobber, line, fish, ripples and
bite marks are drawn like the original, and when the serpent is pulled out the story runs its scene (section 0x4e4 of
`MAPHEAD`).
