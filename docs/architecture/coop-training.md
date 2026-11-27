# Training in a shared world (design document)

Status: **built** (2026-11-20, ticket #365). It builds on [online co-op](multiplayer.md) (§5.7 shared world, §6
decisions 4 and 5), [ADR 0024](../adr/0024-co-op-with-authoritative-host.md) and the gym screens of the
[partner](../modules/game/partner.md) module.

**Rule zero:** the single-player game does not change. Alone (or in versus), a training session moves the clock one
hour exactly as the original does, and no code path differs from before. Everything below applies only while a co-op
game runs.

## 1. How the original trains

`TRN_advanceTrainingTime` (`trn_reward.c`) runs once per session, after the course's animation and reward:

- **The partner pays:** tiredness + the course's cost, energy − cost, happiness − cost. With tiredness at 70 or more
  (overwork) also happiness − 3 and discipline − 3.
- **The clock jumps one hour:** it is set to the session's start time + 1200 frames (20 frames a game minute). While
  the course plays the clock stands still, so a session is always exactly one game hour.
- **That hour's care is paid in one go:** the unhappy-lifetime check when the new hour is a multiple of 4; sleepiness
  (sickness counter + 1, missed sleep hours + 1); hunger (starvation timer − 120 and a care mistake when energy is too
  low, else food level − 60); energy − the species' usage; toilet (pooping timer − 1200, else poop level − 6); lifetime
  − 1, evolution timer + 1, injury and sickness timers.
- **A session that crosses midnight** adds a day: age + 1, the day counter and the daily stat trigger. Day-time changes
  (16:00, 20:00, 6:00) follow from the clock.

One game minute is one real second (20 frames a second), so a game hour is a real minute. A player that trains ten
sessions in a row moves the world ten hours in a few real minutes; that is the original's pacing, and fine alone.

## 2. What co-op did before this ticket

Measured in the code (`GameSession::advanceGameTime`, `Partner::passTrainingHour`, the co-op bridge):

| What | Before | Why it breaks |
|---|---|---|
| The world's clock | Every game in a co-op room ran **its own** `GameClock`, copied from the host's save at join. Nothing sent it afterwards (the snapshot's clock in §5.5 was never built). | Two players see different times of day, shops and NPC schedules after a few minutes. |
| The host trains | The course holds the field (`holdForScript`), which stops the host's clock; the session then jumps it one hour. | The host's partner and the guests live in different hours; ten host sessions skip ten hours for the shared world but not for the guests. |
| A guest trains | Only the guest's own clock jumps. | The guest's clock runs away from the host's; nothing brings it back. |
| Several train at once | Each session jumps its own clock. | With four players the world would jump four hours a minute if it were shared; with private clocks it simply drifts. |
| Others see it | Nothing tells the others that a player trains. | A trade or a battle ally ask goes to a player sitting in a gym screen. |

## 3. Options

| | A. Parallel sessions | B. Synchronised sessions | C. Decoupled partner hour |
|---|---|---|---|
| Idea | Each player trains when they want, alone, while the world goes on. | Sessions start together; everyone waits for a ready check, the clock jumps one hour for all. | The partner's hour is its own: it pays the hour's costs, the world's clock does not move. |
| Waits | None. | Everyone waits for the slowest; nobody can train alone. | None. |
| Clock | Must still decide who moves it. | One jump per round, agreed. | Never jumped by a session. |
| Fidelity | Same per session if paired with C. | Same hour per session. | Same costs per session; the day comes from the world. |
| Cost | Low. | A lobby state, ready checks, timeouts, cancel rules. | Low: one flag in the session request. |

**Chosen: A + C on one world clock, the host's.** Players train in parallel without waiting for anyone (A); a
session costs its partner exactly the hour the original charges, but in co-op it never moves the world's clock (C).
B was rejected: it makes training a group activity, which the issue explicitly does not want ("without waiting for
them"), and it still jumps the world for players who are not training.

## 4. The model

### 4.1 One clock, the host's

- The host's clock is the world's. It runs with real time (one game minute a real second) and is moved only by what
  moves it alone: its own sleep, its scripts' time skips, the debug commands. A training session no longer moves it.
- The host tells every guest its clock with `WorldClock` (`0xC0`, host → guests, reliable): elapsed frames, whether
  it runs, and its speed in thousandths. It tells it at once to a guest that joins, then about once a second, and at
  once when the clock stops, starts, changes speed or jumps more than a second (`session::WorldClockTeller`).
- A guest's session (`ClockShare::Guest`) follows it (`session::WorldClockFollower`): between two words it runs the
  world on at the host's speed for at most two seconds, then stands until the next word. Each tick it compares its
  clock with the world's:
  - **a small gap** (less than an hour) is played as frames, so the partner lives them as usual;
  - **whole hours** (the host slept or skipped time) are a time skip: the partner pays them with `skipHours`, as a
    script's time skip does, and the clock is set to the world's;
  - **a guest a little ahead** (less than five minutes) waits for the world;
  - **a guest far ahead** (its own sleep or its own skip) goes back to the world's time. Its partner already lived
    those hours, so it does not live them again: the session remembers how far the partner lived and gives it no
    frames until the world passes that point.
- Before the host's first word, a guest runs its own clock as before.

### 4.2 The partner's hour

`TrainingRequest::sharedClock` (set by the session in co-op, host or guest) makes `Partner::passTrainingHour` pay
every cost of §1 **without** the clock jump and without the day roll:

- the stat gains, the costs, the overwork penalty, the hour's hunger, sleep, toilet, lifetime and evolution timer:
  unchanged, so ten sessions give the same partner as ten sessions alone;
- the unhappy-lifetime check and the sleepiness use the hour the session would have reached (the clock's hour + 1);
- age, the day counter and the daily trigger come only from the world's midnights, as the partner lives them on the
  shared clock. A session at 23:30 does not add a day; the world's midnight does.

While a course runs in co-op the world's clock keeps running, but the partner's care is held (no frames of hunger or
energy), as the original's stands still for it. When the course ends the partner lives the world's time again. The
hold is `GameSession::holdForCourse`, set by the gym's field port next to the field hold.

### 4.3 Who trains

`TrainingStatus` (`0xC1`, both ways, reliable) carries a player and a course id (at most 32 characters, empty when the
player stops). A guest tells the host its own course when it changes; the host takes the link's player id, never the
one in the message, and tells every guest the others' courses as they change.

- The co-op tag over a player shows **Training** (`coop_hud.tag.training`, `coop_hud.tag.<n>.training`, UI API
  0.25.0).
- A player that trains is busy for trades and is not offered as a battle ally; a player in a gym screen cannot be
  pulled into a battle.
- A player's first course in a co-op game shows a notice once: the world's time keeps running and each session counts
  its own hour for the partner (`online.coop.training_clock`).

### 4.4 Flow

```mermaid
sequenceDiagram
  participant H as Host session
  participant N as Link
  participant G as Guest session
  H->>N: WorldClock (join, every second, on stop/jump)
  N->>G: WorldClock
  G->>G: follow: frames / skip hours / wait / go back
  G->>N: TrainingStatus(course)
  N->>H: TrainingStatus (player = link's)
  H->>N: TrainingStatus to the other guests
  Note over H,G: each session pays its partner one hour; neither clock jumps
```

### 4.5 Sleep in a shared world (#424, #411)

Sleeping skips the night, and the night is the world's. The rules:

- **Only the host moves the world's clock.** The host's sleep takes the world to its partner's morning; every guest
  skips those hours (§4.1).
- **A guest's sleep does not move the world, and the world does not undo it.** The partner sleeps as alone (the
  night's rest, lifetime and evolution hours, its next bedtime), then the guest's screen stays dark until the world's
  clock reaches that morning (`SleepScreen` waits while `GameSession::aheadOfWorld`), with a notice that says so
  (`online.coop.sleep_wait`). If the host sleeps meanwhile the guest wakes at once. The guest's clock never goes back
  to the night and the partner never lives those hours twice.
- **Nobody is forced to sleep.** The others keep playing; the world is never held for a sleeper.
- **No advantage from sleeping out of step.** A guest cannot rest and still use the night: its game waits with its
  partner. Its sleep is only possible when its partner is sleepy, as alone.
- **World events and day changes** follow the world's clock only (§5): a guest's sleep changes no day for anyone.

## 5. Disconnections and edge cases

- **A guest leaves:** the host clears its course (`CoopTraining::playerLeft`) and tells the others; a guest that comes
  back hears the clock and every course again.
- **The host leaves:** the guest goes back to the title as before; nothing of this ticket outlives the game.
- **Day change and events:** only the world's midnight changes the day, for every player at once (a guest that missed
  it by a skip pays it in `skipHours`). Day-time changes follow the shared clock.
- **Host menus and scripts:** the host's menus and Start's pause no longer hold the world's clock (#415); its scripts
  still do (§7), so the world stands for everyone while the host reads a dialog, and guests wait with it.
- **A guest sleeps:** its partner sleeps and its screen waits in the dark for the world's morning (§4.5); it no
  longer goes back to the night.
- **The host sleeps:** guests skip those hours (§4.1), as a script time skip.
- **Battles:** a battle still stops its player's clock while it runs; a guest catches up afterwards by frames or a
  skip.

## 6. Code

| Piece | Where |
|---|---|
| The partner's hour without a jump | `packages/game/partner` (`TrainingRequest::sharedClock`, `Partner::passTrainingHour`) |
| Clock sharing, follow and tell | `packages/session` (`shared_clock.hpp`, `GameSession::shareClock`, `hearWorldClock`, `worldTime`, `holdForCourse`) |
| Messages `0xC0`, `0xC1`, protocol 11 | `packages/game/coop` (`protocol.hpp`, `link.cpp` `directionAllowed`) |
| Routing and the game loop | `apps/game` (`coop_gameplay_bridge`, `coop_training`, `game.cpp`, `play_session`) |
| The tag | `content/packs/game/ui/coop_hud`, UI API 0.25.0 |

## 7. Limits and follow-ups

- **Co-op sleep** (§4.5): a guest that sleeps while the host stays awake waits up to a night in real time (about ten
  minutes); the host is not told that a guest sleeps.
- **Host dialogs and scripts still hold the world's clock** for everyone (its menus and pause no longer do, #415).
- **A guest's progress is not saved back** to its own game (phase C4).
- **The remote partner's training animation** is not drawn on the other machines; only the tag says it.

## 8. Tests

- `packages/game/partner/tests/training_tests.cpp`: a shared-clock session leaves the clock and pays the same gains and
  costs as one alone; one at 23:30 adds no age.
- `packages/session/tests/shared_clock_tests.cpp`: follow (gap, skip, wait, go back), the teller, alone vs co-op,
  four players training every second for ten seconds, the course hold, a guest's own skip not lived twice, the host's
  sleep.
- `packages/game/coop/tests/training_tests.cpp`: round trip, refusals and the direction rules through a real
  `CoopLink`.
- `apps/game/tests/coop_training_tests.cpp`: a host and three guests on loopback links training at once.
