# Checking training

The gym courses break without it showing in a short test: it is enough for a change in the field animation to make
every clip move the actor, or for a course to push the partner as well as its clip, for the partner to go through the
rock or run twice as fast. The training check looks at every course of every gym on two levels.

## When it is mandatory

Before every PR that touches the gym (`packages/game/gym`, the gym screen, the `gym.yaml` rules), the field
animation, the root motion of the clips (`clipRootStep`, `applyClipRootMotion`, the hold-in-place mark) or the
training rules. Both parts have to pass; the result of the second goes in the PR description.

## Level 1: unit tests (CI)

`packages/session/tests/gym_course_tests.cpp` plays an entire session of each course on a real field with made-up
data: each partner clip advances 10 units per frame, so a course that does not hold it where the original holds it,
or that moves it as well as its clip, goes out of its limits. For each course it checks:

- that the session ends and lasts the original's frames;
- the partner's farthest distance from its spot and the path it travels, within the original's limit;
- that the course raises its stat and no other goes down.

One more test compares the pack's list of courses with the tests' list: a new course needs its test and its route.
They run with the rest of the `dwe_session_tests` tests, in the CI of every PR, without a disc:

```sh
scripts/dev.sh build && scripts/dev.sh test                                  # all, like the CI
build/ci/packages/session/dwe_session_tests --test-case='training check*'        # only these
```

## Level 2: the real content (local)

It needs a content folder extracted with `dwx`. Each route in `tests/autotest/training` starts a new game on the gym
map, opens a course with `gym("<course>")`, plays the entire session and checks with `expect_range` the settings the
gym screen publishes when it gives the gains:

| Setting | What it is |
|---|---|
| `gym.session.frames` | frames of the session, from the partner arriving at its spot until the gains |
| `gym.session.max_offset` | the farthest the partner gets from its spot, in original units |
| `gym.session.path` | the ground it covers (not counting the race's laps, which reposition it) |
| `gym.session.gain.<stat>` | how much each stat rises: `hp`, `mp`, `offense`, `defense`, `speed`, `brain` |

```sh
scripts/dev.sh training-check --content <folder>          # all
scripts/dev.sh training-check --content <folder> --only 'trai00-*'
```

Each course runs without a window in its own Xvfb, four at a time (`TRAINING_JOBS`), without the validation layer.
When it finishes it writes a table with the result, the time and the `gym: session` line of each course, and the
total; it exits with 1 if any fails. Each course's logs are left in `build/training-check/<course>/run.log`.

It takes about 12 minutes (11 min 44 s on 2026-11-06): icsa03's defense alone plays 11 minutes and starts first; the
other courses take about two minutes each. With `--only` only the one that failed is repeated.

Both parts were tested by breaking the engine on purpose: if the field stops holding the clip in place, eight unit
tests and the three HP routes fail (the partner ends up 18000 units from its spot, going through the rock); if the
root step counts double, fourteen unit tests fail.

## Courses and limits

Sixteen courses in seven gyms: trai00 (all six), gias09 (offense and speed), mist05 (brains), icsa03 (HP and
defense), betl03 (HP and offense), betl04 (the speed and defense belt) and gomi02 (MP). The limits for trai00 come
from the original, measured in DuckStation with the reference partner; those of the other gyms come from the engine's
session, because there is no saved game of the original on those maps, and they rely on the unit tests, which already
look at where the original holds the partner. In parentheses, what the engine gave.

The rules: the frames, the reference value with a margin of 2 below and 3 above (1 % in icsa03's defense, and from
1340 to 1410 in trai00's, which covers the original and the engine); the distance and the path, at most the greater of
1.25 times the reference and 40 units more, and at least half when it goes over 50; in the race, 90 % to 110 %. Each
gain from half the reference to 1.5 times it, and the other stats, 0. This way a partner that does not move fails, as
does one that runs twice as fast, and so does a gain that does not arrive or that is added twice.

| Course | Limits from | Frames | Distance from its spot | Path | Gains |
|---|---|---|---|---|---|
| betl03 hp | engine | 1198–1203 (1200) | 49–139 (99) | 49–138 (98) | hp 60–180 |
| betl03 offense | engine | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | offense 6–18 |
| betl04 defense | engine | 1198–1203 (1200) | 102–257 (205) | 102–257 (205) | defense 6–18 |
| betl04 speed | engine | 1198–1203 (1200) | 0–70 (30) | 0–70 (30) | speed 6–18 |
| gias09 offense | engine | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | hp 5–15 offense 4–12 |
| gias09 speed | engine | 1198–1203 (1200) | 2731–3339 (3035) | 47925–58576 (53250) | hp 5–15 speed 4–12 |
| gomi02 mp | engine | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | mp 75–225 |
| icsa03 defense | engine | 13039–13303 (13171) | 0–40 (0) | 0–40 (0) | defense 6–18 speed 1–3 |
| icsa03 hp | engine | 1198–1203 (1200) | 49–138 (98) | 48–137 (97) | hp 60–180 defense 1–3 |
| mist05 brain | engine | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | mp 5–15 brain 4–12 |
| trai00 brain | original | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | mp 5–15 brain 4–12 |
| trai00 defense | original | 1340–1410 (1357) | 110–275 (220) | 10990–13434 (12395) | defense 4–12 brain 1–3 |
| trai00 hp | original | 1198–1203 (1200) | 40–120 (98) | 40–120 (99) | hp 40–120 offense 1–3 |
| trai00 mp | original | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | mp 40–120 defense 1–3 |
| trai00 offense | original | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | offense 4–12 speed 1–3 |
| trai00 speed | original | 1199–1204 (1200) | 2754–3368 (3042) | 47916–58565 (53250) | hp 5–15 speed 4–12 |

What was measured in the original (trai00): HP 1200 frames, 80 of distance and of path, HP +80 and offense +2; MP
1200, 0, MP +80 and defense +2; offense 1200, 0, offense +8 and speed +2; defense 1391 frames, 220 of distance, 12212
of path, defense +8 and brains +2; speed 1201 frames, 3061 of distance, 53240 of path in 10 laps, HP +10 and speed +8;
brains 1200, 0, MP +10 and brains +8.

Differences that remain with the original, within the limits: in HP the partner moves away 98 instead of 80; in
defense the session lasts 1357 frames instead of 1391 (the path, 12395 against 12212). icsa03's defense lasts 13171
frames (about 11 minutes) because, as in `trn2_def_map99.c`, only the object's fall frames count and each hit waits
30 frames.

When a change moves one of these figures on purpose (a fidelity fix measured in the original), the route's limit and,
if needed, the unit test's are changed in the same commit, saying where the new figure comes from.
