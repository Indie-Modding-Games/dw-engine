# weather

Field weather (`dwe::game::weather`, `game` layer): clear (sunny by day), cloudy, light and heavy rain, light and heavy
snow, fog, storm and wind, the fronts that travel between areas, the odds for each part of the day, and the seasons.
**Engine improvement**: the original has no weather or seasons. They are two options ("Weather" and "Seasons"), on by
default, and they never change a rule: stats, encounters, food, evolution, prices, events, timing and collisions are the
same with or without them.

The module draws nothing: it depends on `core` and `assets`. It decides which weather applies; the game (`apps/game`)
draws it and plays its sound, and the session stores its seed.

## Rules per area

The rules are pack data: `field/weather.yaml` (schema `field_weather.schema.json`, key `field_weather` of
`pack.yaml`), which `loadClimateRules` reads.

- `period_hours` (3): every this many game hours the area rolls again.
- `keep_chance` (0.45): chance of keeping the weather of the previous period; otherwise a new one is drawn by weight.
- `fade_seconds` (8): real seconds a change takes to fade in.
- `no_weather`: maps without weather (patterns ending in `*`). In addition, no map with a fixed time (interiors, caves,
  buildings, the tournament arena) has weather, whether or not an area lists it.
- `areas`: areas with their maps and weights (`clear`, `cloudy`, `light_rain`, `heavy_rain`, `light_snow`, `heavy_snow`,
  `fog`, `storm`, `wind`; a missing one never comes up), their neighbors (`neighbours`, see Fronts) and what their wind
  carries (`wind`: `leaves`, the default, or `dust` in the dry areas). All the maps of an area share weather, so it does
  not change when moving from one map to another of the same area. The first area that matches wins.

| Area | Maps | Weights (clear / cloudy / rain / heavy rain / snow / heavy snow / fog / storm / wind) |
|---|---|---|
| Native Forest | `mayo*` | 5 / 3 / 2 / 1 / - / - / 1 / 1 / 1 |
| Tropical Jungle | `trop*` | 3 / 2 / 3 / 2 / - / - / 1 / 2 / 1 |
| Mt. Panorama | `miha*` | 4 / 4 / 2 / 1 / - / - / 2 / 1 / 2 |
| Overdell | `dgha*` | 1 / 5 / 3 / 1 / - / - / 2 / 1 / 1 |
| Great Canyon | `gcan*` (not `gcan11`) | 7 / 3 / 1 / - / - / - / - / 1 / 3 (dust) |
| Gear Savanna | `gias*` | 6 / 3 / 1 / 1 / - / - / - / 1 / 2 (dust) |
| Ancient Glacier Region | `koda01`-`koda03` | 2 / 3 / - / - / 4 / 2 / 1 / - / 2 |
| Ancient Regions | rest of `koda*` | 5 / 3 / 2 / 1 / - / - / 1 / 1 / 1 |
| Freezeland | `frzl*` | 2 / 2 / - / - / 4 / 3 / 1 / - / 2 |
| Beetle Land | `betl*` | 4 / 3 / 2 / 1 / - / - / 1 / 1 / 1 |
| Misty Trees | `mist*` | 1 / 4 / 3 / 1 / - / - / 3 / 1 / - |
| Geko Swamp | `stic*` | 2 / 3 / 3 / 2 / - / - / 3 / 2 / - |
| Volume Villa | `gkyo*` | 4 / 3 / 2 / - / - / - / 1 / - / 1 |
| Toy Town | `omoc*` | 5 / 3 / 1 / - / - / - / - / - / 1 |
| Factorial Town | `fact*`, `gomi*` | 3 / 4 / 2 / - / - / - / 2 / - / 1 (dust) |
| File City | `twna*`, `twnb*`, `trai*` | 5 / 3 / 2 / 1 / - / - / 1 / 1 / 1 |

Cold areas have no storms (their rain is snow) and dry areas have no fog.

No weather: `tunn*`, `room*`, `tend*`, `topn*`, `mgen*`, `yaka*`, `chka*`, `saib*`, `icsa*`, `leom*`, `ogre*`,
`gcan11` and the fixed-time maps.

## Seasons

Spring, summer, autumn and winter take turns every `seasons.days` game days (3) from year 0, day 0, starting with
spring (`seasonOfDay`, `seasonOfHour`): the same hour counter the weather uses. With 3 days, a new partner (about 15
days of life, `rules/original/life.yaml`) sees all four and spring comes back; one game day is about 24 real minutes, so
a season lasts more than an hour of game time. The cycle (12 days) does not follow the calendar year (30 days): it is
looks only.

Each season in the `seasons` block of `field/weather.yaml` has:

- `odds`: multiplies the weight of each weather of the area. `rain_to_snow`: the part of the rain that falls as snow
  (light as light, heavy as heavy) in the areas with `snow_from_rain` (yes by default). `seasonalWeights` gives the
  weights in hundredths.
- `grade`: the color of the scenery. The foliage greens (about 60 to 170 degrees of hue, with some color) turn toward
  `foliage_hue` in `foliage`; then `saturation`, `gain` per channel and `lift` (paler).
- `petals`, `leaves`: falling petals or leaves, from 0 to 1.

| Season | Weather (multiplies) | Color | Particles |
|---|---|---|---|
| Spring | rain ×1.5, heavy rain ×1.2, snow ×0.4, wind ×1.3, fog ×1.2 | greens toward 105° (fresh) 0.25, saturation 1.18, a bit greener | petals |
| Summer | clear ×1.8, cloudy ×0.8, rain ×0.6, storm ×1.6, fog ×0.5 | saturation 1.15, warm gain (1.1 / 1.05 / 0.9), a bit brighter | — |
| Autumn | cloudy ×1.3, rain ×1.5, heavy rain ×1.3, fog ×1.5, wind ×1.4 | greens toward 25° (orange, a bit darker) 0.42, gain (1.06 / 0.97 / 0.86) | leaves |
| Winter | snow ×1.6, cloudy ×1.3, storm ×0.3; 75% of the rain as snow | saturation 0.6, cool gain (0.94 / 0.99 / 1.08), paler (0.07) | — |

Areas with their own climate keep their identity: `season_strength` brings the season closer to "no change" (the
factors, the color and the particles) and `snow_from_rain: false` prevents snow.

| Area | `season_strength` | `snow_from_rain` |
|---|---|---|
| Tropical Jungle | 0.5 | no |
| Great Canyon | 0.4 | no |
| Gear Savanna | 0.7 | no |
| Ancient Glacier Region, Freezeland | 0.4 | (no rain) |
| Rest | 1 | yes |

With seasons, `weatherAt(..., seasonal = true)` draws the weather with the weights of the season of the period being
rolled (a weather drawn at the end of a season can carry on into the first hours of the next one). With seasons off (or
without a `seasons` block), exactly the usual weather comes up: same rolls, same weights.

The look is `SeasonLook` (color, petals and leaves); `SeasonBlend` fades it like the weather (`fade_seconds`) when the
season changes at midnight, and it changes at once when entering another area, a map with no weather (neutral) or when
the option is changed. A season is only visible on maps with weather: interiors, caves and fixed-time maps do not
change.

## Parts of the day

The `day_parts` block of `field/weather.yaml` gives odds per part of the day: dawn (`dawn`), day (`day`), dusk
(`dusk`) and night (`night`). Each has `from`, the hour of the day (0 to 23) at which it starts (it lasts until the
next one; night goes on until dawn), and `odds`, which multiplies the weight of each weather like the seasons' do. They
stack: with seasons, the weight is the area's times the season's factor (brought closer to 1 by `season_strength`)
times the part of the day's. The parts of the day do not depend on the "Seasons" option or on `season_strength`.

The game clock only has day (from 6), dusk (16) and night (20), and dawn does not exist; that is why the hours are data
of these rules. The pack's follow the clock for dusk and night and put dawn from 5 to 8.

| Part | From | Weather (multiplies) |
|---|---|---|
| Dawn | 5 | fog ×3, cloudy ×1.3, clear ×0.8, wind ×0.7, storm ×0.4 |
| Day | 8 | wind ×1.3, clear ×1.2, storm ×1.2, fog ×0.4 |
| Dusk | 16 | storm ×1.8, heavy rain ×1.4, wind ×1.2, cloudy ×1.1 |
| Night | 20 | fog ×1.5, clear ×1.1, heavy rain ×0.8, storm ×0.8, wind ×0.8 |

So there is more fog at dawn (and some at night) and more storms at dusk.

As with the seasons, what counts is the part of the day at which the period that rolls the weather starts
(`dayPartOfHour`), so a weather drawn at the end of dusk can carry on into the night. `oddsWeights(rules, area, season,
part)` gives the weights with both factors, in hundredths; without a `day_parts` block the weather comes up exactly as
before.

## Fronts

A weather that an area draws anew (a period that does not carry on the previous one) can travel to the neighboring
areas, like a front. It is looks only: no rule reads it. The data is the `fronts` block of `field/weather.yaml` and the
`neighbours` list of each area:

- `chance` (0.2): chance that a new weather travels. With 0.2 the weather of an area changes about 5.6 times a day
  instead of 3.4 without fronts; with 0.5 it would be more than 7.
- `delay_hours` (2): game hours it takes to reach the next area; two areas away, double.
- `reach` (2): how many areas away it reaches.
- `kinds`: the weathers that travel (cloudy, rains, snowfalls, storm and wind; fog is local); all of them if missing.
- `neighbours`: the neighboring areas. Listing one side is enough: they count both ways.

It reaches an area `distance × delay_hours` hours after the start of the period in which it came up, and stays there
`period_hours` hours; while it is there, it is the area's weather. It only reaches an area that can have that weather at
that hour (weight greater than 0 with the season and the part of the day), so a snow front never enters the jungle or
the canyon. The origin area's own weather travels, never one that arrived from another. If there are several, the last
to arrive wins, then the closest, then the first area in the list.

The neighbors come from the exits between maps of different areas (the on-foot ones and the story passages, like the
one from the Native Forest to Beetle Land); the city's teleports do not count. They are written in the YAML and are not
read from the maps on load, because this way a pack can change them without touching the maps.

| Area | Neighbors |
|---|---|
| File City | Native Forest |
| Native Forest | File City, Mt. Panorama, Tropical Jungle, Beetle Land |
| Tropical Jungle | Native Forest, Great Canyon, Overdell, Ancient Regions |
| Mt. Panorama | Native Forest, Gear Savanna |
| Overdell | Tropical Jungle, Great Canyon |
| Great Canyon | Tropical Jungle, Overdell, Freezeland |
| Gear Savanna | Mt. Panorama, Factorial Town, Geko Swamp |
| Ancient Regions | Tropical Jungle, Ancient Glacier Region |
| Freezeland | Great Canyon, Misty Trees |
| Beetle Land | Native Forest, Factorial Town |
| Misty Trees | Geko Swamp, Freezeland, Toy Town |
| Geko Swamp | Gear Savanna, Misty Trees, Volume Villa |
| Factorial Town | Gear Savanna, Beetle Land |
| Ancient Glacier Region | Ancient Regions |
| Volume Villa | Geko Swamp |
| Toy Town | Misty Trees |

`frontAt` gives the front over an area at a given hour (weather, origin area, arrival hour, distance) and
`weatherAtHour` the weather that is seen: the front's or, with no front, the period's own (`weatherAt`). `areaSteps`
gives the distance between two areas through their neighbors.

## Determinism

`weatherAt(rules, area, seed, period)` is a pure function: the weather comes from a hash (splitmix64) of the weather
seed, the area and the period (`periodOf(rules, gameHour)`, hours since the start of the clock). Fronts are too
(`weatherAtHour(rules, area, seed, gameHour)`, which is what the field uses): one more roll per period and area, so
loading a game or crossing into another area does not change the weather. It never draws the game's random numbers, so
the order of rolls that the oracle compares is the same with or without weather.

The seed is the `WeatherSeed` session system (`packages/session`, `weather` section of the save, `{seed}`). In a new
game it comes from the session seed (`weatherSeedOf`); a game saved before weather existed has no such section and takes
the same derived seed, which is saved on the next save.

## Transitions

`WeatherLook` is the look of the weather: cloud cover, rain, snow, sun, fog (`fog`), lightning (`storm`) and wind
(`wind`), each from 0 to 1. The storm is full clouds and rain, lightning and some wind (0.55); wind alone, few clouds
and full wind; fog, some cloud and full fog. `WeatherBlend` moves it toward that of the weather that applies at
`1 / fade_seconds` per second. It changes at once (no fade) when entering another area or a map with no weather, when
the option is changed and with `--weather`.

## Drawing (`apps/game`)

- `FieldScene` (`field_scene_weather.cpp`) picks the area when each map loads, advances the fade every frame (the
  animation stops with pause) and passes the quads to `WeatherDrawer`.
- `weatherQuads` (`weather_particles.cpp`) generates the quads statelessly, by hash of the particle number and its
  cycle: a dark gray veil for cloudy and rain, shadows of passing clouds (a repeating noise texture), sun heat by day,
  slanted drops with splashes on the ground, flakes that fall with drift. Sizes and speeds are in pixels of the
  original, so they look the same at any scale; the density follows the width. At night rain and snow are fainter, and
  the sun and the cloud shadows go away.
- Fog: a layer of haze (the shader's `Fog` shape) over the background rectangle, thicker at the top (the far distance in
  the field's fixed cameras) than at the bottom, with wisps that move slowly (the same noise texture, larger and with
  some parallax). Its color is light by day and dark blue by night. It also dulls and brightens the scenery: the map
  color (`MapGrade`) loses 35% of saturation and rises a little (`lift`), on top of the season's; the prerendered
  background still shows through underneath, only paler.
- Storm: heavy rain slanted by its wind and lightning: `lightningStrike` decides by hash of each 6 s stretch whether
  one strikes (7 out of 10), when and how strongly; `lightningFlash` gives a half-second double flicker that brightens
  the whole map, background and characters (a bluish additive quad).
- Wind: yellowish, brown, light green and orange leaves (26 per original width) or specks of dust (70) that cross from
  left to right with some sway, faint streaks of air, and in the dust areas a sandy veil. It slants the rain (up to 0.85
  pixels to the right per pixel it falls) and pushes the snow and the season's petals and leaves sideways.
- `seasonQuads` (`weather_particles.cpp`) generates the petals (pinkish ovals) and the leaves (pointed, with a vein;
  orange, red, yellow and brown) that fall slowly with the wind, spin and flip. Few (18 petals or 14 leaves per
  original width), 40% under heavy rain or snow, fainter at night.
- The season's color is applied by `render::MapRenderer::setGrade` (`MapGrade`, shader `map.slang`) to the background,
  the depth layers and the ground objects, never to the models or the bands; the `lift` is softer at night. A neutral
  color draws the colors as they are.
- `WeatherDrawer` (`weather_drawer.cpp`, shader `weather.slang`) draws in the scene pass, without depth, clipped to the
  map's background rectangle (never over the bands), after the translucent models and before the fog, the flashes, the
  story fades and the UI.
- It is not drawn with the map hidden, in story camera scenes or with the scenery hidden.

## Sound

`WeatherAmbience` (`weather_ambience.cpp`) plays a rain loop, gusts of wind with the snow and with the wind, with its
own hash counter, and the storm's thunder. There are no recordings of rain, wind or thunder in the extracted banks; it
uses stand-ins that are read at run time (no audio in the repository): rain `sound/esall/008/sample_004` (a continuous
hiss, looped with a 0.25 s crossfade), wind `sound/esall/011/sample_004`, every 3 to 8 s (up to half that with strong
wind), and thunder `sound/esall/005/sample_006`, a long, low rumble, a bit slower. Each thunder comes from the same
lightning that is seen (`lightningStrike`), 0.4 to 3.5 s later: the close ones sooner, louder and less deep. It follows
the weather animation seconds (`FieldScene::weatherSeconds`), so it stops with pause and a jump (another map, a load)
does not release old thunder. The audio API does not change the volume of a voice that is playing, so the rain loop is
restarted in volume steps (`RainStep`). A pack can replace those clips with others.

## Interface for lighting

`FieldScene::weather()` returns this frame's faded `WeatherLook` (read only): `cloudCover`, `rain`, `snow`, `sunshine`,
`fog`, `storm`, `wind`, `intensity()`. A light pass can use it to lower the sun or turn off the shadows under clouds
without knowing the module or its rules. `FieldScene::weatherArea()` gives the area (`nullptr` with no weather).

## Option and debugging

| What | Where | Notes |
|---|---|---|
| "Weather" option | Title and pause menu options; `weather` setting of `settings.yaml` | Improvement, on by default; applies at once |
| `--weather <weather>` | Command line | Forces a weather (`clear`, `sunny`, `cloudy`, `rain`, `heavy_rain`, `snow`, `heavy_snow`, `fog`, `storm`/`thunderstorm`, `wind`, `none`) on maps with weather |
| "Seasons" option | Title and pause menu options, below "Weather"; `seasons` setting of `settings.yaml` | Improvement, on by default; applies at once; off = as without seasons |
| Season name | Pause menu, after the day (modern skin; UI setting `season`) | Empty with the option off; the original skin has no room for it |
| `--season <season>` | Command line | Forces the look and name of a season (`spring`, `summer`, `autumn`/`fall`, `winter`; the weather follows the calendar); `none` turns them off like the option |

## Tests

- `dwe_weather_tests`: the pack's areas and maps without weather, snow only in cold areas, pure function and periods,
  fade, names, bad rules. Seasons: season by day (3 days, spring first, negative days), snow in winter and more sun in
  summer in the forest, the areas with their own climate (Freezeland keeps snowing, the jungle and the canyon never
  snow), off = same weather in each period as without seasons, look fade, bad seasons.
- `dwe_weather_tests` (`day_parts_tests.cpp`): part of the day by hour (negative hours and following days), the factors
  multiply the weights and stack with the season, a part with no rain never draws it, without the block it is as
  before, bad parts.
- `dwe_weather_tests` (`fronts_tests.cpp`): the pack's neighbors go both ways and join all the areas; a front reaches the
  neighboring area after `delay_hours` hours and stays one period; two areas away it takes double, does not go past
  the reach and does not enter an area without that weather; pure function of seed, area and hour; no front carries
  snow to the jungle, the canyon or the savanna; bad fronts and neighbors.
- `dwe_weather_tests` (`kinds_tests.cpp`): names of fog, storm and wind; their look and their fade; in the pack, dust in
  the dry areas, no snow in the jungle and no storms in Freezeland, a lot of fog in the Misty Trees; more fog at dawn
  and more storms at dusk (weights and rolls); bad wind.
- `dwe_weather_tests` (`schema_tests.cpp`): the pack's `field/weather.yaml` meets its schema and loads.
- `dwe_session_tests` (`weather_seed_tests.cpp`): the game's and the partner's random numbers are the same with and
  without weather; save round trip; old save without the section; bad section.
- `dwe_game_tests` (`weather_particles_tests.cpp`): nothing in clear weather at night, drops and splashes, determinism,
  flakes, half fade, clouds; petals and leaves (how many, they move, fewer with rain, fainter at night); fog (a thicker
  haze at the top that moves), wind (leaves or dust, they move, slants the storm's rain), lightning (deterministic,
  within its stretch, thunder afterward, the flash's additive quad).

## Pending

- Seasons do not change the characters or the world map; there is also no snow piling up on the ground.

- Rain is not blocked under roofs or eaves (it is drawn over the whole background).
- Fog is a screen gradient (thicker at the top), it does not read the depth of the background layers.
- Real recordings of rain, wind and thunder, and per-voice volume in the audio API.
