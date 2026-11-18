# State trace and comparison with the original

To find behavior differences from the original game without eyeballing them, both play the same input from the same
place, each writes a trace of its state tick by tick, and a script compares them and says where they first diverge.

- `dwe-game --trace <file>` writes the engine's trace.
- The original's trace is written by a tool outside the repository that reads the original game's memory while it
  runs. It writes the same format, at 59.94 frames per second.
- `scripts/oracle_compare.py` compares the two traces.

To read the same state while the game runs, without writing the whole trace, there is the
[remote control](remote-control.md).

## The engine's trace

With `--trace <file>`, the game writes one JSON line per logic tick (60 per second). It writes it after the tick,
with what the tick left. Without the option nothing is written. It is used with an autotest, so that the input is
always the same:

```sh
scripts/dev.sh run --content <content> --start twna01 --seed 1 \
  --autotest route.yaml --trace trace.jsonl
```

The first line is the header: `{"src":"dwe","meta":true,"hz":60}`. Each following line is a tick:

| Key | What it is |
|---|---|
| `f`, `time` | The tick and its seconds since startup |
| `in` | The buttons pressed that tick, with the original controller's bits: up `0x1000`, right `0x2000`, down `0x4000`, left `0x8000`, menu `0x10`, cancel `0x20`, confirm `0x40`, walk `0x80` |
| `map`, `mode` | The map and whether it is in the field (`field`) or in a battle (`battle`) |
| `player`, `partner` | `{x, y, z, yaw}` of the player and the partner |
| `npcs` | The map's NPCs: `{x, y, z, yaw, id}`, with `id` the NPC's script id |
| `battle` | Only during a battle: `phase` (`intro`, `fight`, `end`...), `frame`, `rand` (where the rolls stand: with `--original-rolls`, the state of the original's `rand()`) and `fighters` |

Each fighter in `fighters` is `{x, z, yaw, hp, mp, max_hp, max_mp, off, def, spd, brain, pose, pending, move}`.
`pending` is the damage received that has not yet been subtracted from `hp` (the original's `hpDamageBuffer`: a hit
lowers `hp - pending` at once) and `move`, the original's number of the technique it is using, or −1. The partner
comes first and then the enemies, in their order.

Positions and turns are in the original's axes and units, the same ones its memory keeps:

- x, -y, -z of the engine's world;
- the turn goes from 0 to 4095 per revolution, with 0 facing the original's -z.

This way the two traces are compared without converting anything.

## Starting in the same place

A new game in the engine does not start where the original is when it loads its state. To make them match, the route
places the characters with the `place` script command, in original units:

```yaml
steps:
  - wait: {map: twna01, seconds: 1, timeout: 60}
  - script: place("player 256 -1285 1024")
  - script: place("partner 256 -1085 0")
  - script: place("npc:6 243 2565 2109")
  - wait: {ticks: 30}
  - hold: {keys: [right], ticks: 60}
  - hold: {keys: [], ticks: 30}
```

`place` leaves the character exactly where requested: it does not move anyone aside or ask for the drop rule, so that
the distance between the player and the partner is the original's from the very first frame.
The partner can also be given the animation the trace catches it in: `place("partner 256 -1093 0 5 8 136
5272")` is the original's animation id, the frame whose step comes next and the fraction of a unit its position
carries in x and z (1/32768, `anim.c: locX`, `locZ`). In the oracle, the scenario's `place_clip`,
`place_after_settle` and `place_lead_ticks` generate it right before the inputs.

`hold` keeps the keys down exactly that many logic ticks, even if the frames are slow. This way an input of "right 60
frames" lasts the same in both games. `battle("story:2")` starts a story battle against NPC 2 of the map, like the
ones in the scripts: you cannot run away and the enemies do not spend MP.

## Comparing

```sh
scripts/oracle_compare.py original.jsonl engine.jsonl --align input --json report.json
```

`--align` chooses instant 0 of each trace:

- `input`: the first input;
- `move`: the player's first movement;
- `battle`: the entry into battle;
- `fight`: the start of the fight, after the introduction.

From there the records are matched by time. With `fight`, if the original's trace keeps the battle frame
(`battle.frame`, the original's `BATTLE_FRAME_COUNT`), they are matched by frame: the engine counts the frames already
played and the original the one that is playing, so the engine's `n` is the original's `n - 1` (the last sample of
each frame of the original is taken). An original frame lasts 3 vblanks or more, and by time the two drift apart. For
each field it says:

- the first time the difference exceeds its tolerance, with both values;
- the maximum difference;
- the share of frames that fall outside.

The fields are these:

- `mode`, `map`;
- `player.pos` and `player.yaw`;
- `partner.pos` and `partner.yaw`;
- `partner.gap`: the partner's distance to the player;
- `npc<id>.pos`;
- `fighter<n>.pos`, `.hp` and `.mp`.

It also summarizes the starts and stops, the distance traveled and the average speed, and the HP and MP changes of
each fighter.

Other options:

- `--tolerance field=units` changes a tolerance;
- `--max-seconds S` limits the compared stretch;
- `--timeline S` prints a table of the two states every S seconds.

It exits with 1 if anything diverges.

## Limits

- The original moves the characters at 20 Hz, in steps of about 50 units. The engine moves them at 60 Hz. That is why
  the position tolerances are half a step or more.
- The original's game and the engine's do not have the same story flags: a script that runs in one and not in the
  other changes the route. The scenarios avoid those tiles.
- Random numbers are not compared. The engine uses `--seed` and a battle repeats its result with the same seed, but
  not the original's. For a battle, `--original-rolls <state>` makes the engine roll with the original's generator
  from the state the original had when the fight began (the original's trace keeps it in `rand`, from `0x80009010`).
  With that option the engine also spends the original's rolls for sparks and particles and its effects roll from the
  same generator, so the decisions stay in the same order until the fight diverges for another cause (flight times,
  movement; `docs/modules/game/battle.md`). `battle.rand` gives the state of the engine's generator after each frame.
