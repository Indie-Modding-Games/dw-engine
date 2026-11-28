# Audit of #365: training in a shared world

Audited on 2026-11-27 on `wave/w135-integrate` (protocol 14), with the #411 fix that makes a session last its hour of
the world's clock. Design: [training in a shared world](../coop-training.md).

Status: **Done** (the doc or the code meets it, with a test or a clear section), **Partial**, **Missing**. The issue
is research and design; its out-of-scope list (no new growth formulas, no lifecycle or clock speed change, no new
facilities) holds.

## Deliverables

| Deliverable | Before | After | Evidence |
|---|---|---|---|
| Analysis of the original training | Partial | Done | §1: the hour's costs and the clock jump were there; start, end, gains, roulette, rest and what co-op must keep were added, from `trn_reward.c`, script 108 and the [care sheet](../../reference/care.md#training). |
| Comparison of the multiplayer models | Done | Done | §3: parallel, synchronised and decoupled, with waits, clock, fidelity and cost; A + C chosen, B rejected with the reason. |
| Proposal for 2 to 4 players | Partial | Done | §4 held for any size and a four-player test existed; §4.6 adds the 1 to 4 table, one session per player and the shared machine. |
| Integration with the global clock | Done | Done | §4.1 (one clock, the host's; follow and tell) and §4.2 (the partner's hour), plus the #411 rule that a session lasts its world hour. Tests: `shared_clock_tests.cpp`, `gym_course_tests.cpp` ("in co-op a course lasts at least the world's hour it costs"). |
| Authority and synchronisation | Partial | Done | §4.3 had who trains and the link's player id; §4.7 adds who computes the gains, that the host does not validate them, and why nothing is counted twice. |
| Individual progression rules | Done | Done | §4.2 (same gains and costs as alone, days from the world's midnight); §4.7 (progress goes home, #383). Test: `training_tests.cpp` (partner). |
| Flow diagrams | Partial | Done | §4.4 had the message sequence; a state diagram of one player's session (script, course, award, count, hour wait, done, link lost) was added. |
| Risks and edge cases | Partial | Done | §5 had leaving, day change, sleep and battles; the table of the issue's nine cases was added, and §7 lists the limits. |
| Technical implementation proposal | Done | Done | §6 (code map) and the built pieces (#381, #420, #424, #411). |
| Ticket breakdown | Missing | Done | §9: six done tickets and four open ones (T7 host dialogs, T8 shared machines, T9 host local screens, T10 host validation). |

## Acceptance criteria

| Criterion | Before | After | Evidence |
|---|---|---|---|
| The original's training time is documented | Done | Done | §1. |
| Alternatives for simultaneous training are evaluated | Done | Done | §3. |
| One authoritative clock is defined | Done | Done | §4.1; `WorldClockTeller`, `WorldClockFollower` (`packages/session/src/shared_clock.cpp`). |
| Simultaneous sessions do not multiply time | Done | Done | Test `four players training at once never move the world's clock more than its own time` (`shared_clock_tests.cpp`); `four players train at once…` (`apps/game/tests/coop_training_tests.cpp`). |
| Individual gains are computed and synchronised | Partial | Done | Computed per game (§4.2); how they reach the others and who owns them was undocumented, now §4.7. |
| Disconnections and interruptions are documented | Partial | Done | §5 and its table (a guest or the host leaving mid-course, latency, a course waiting on a stopped host clock). |
| Sessions vs events and day changes | Done | Done | §4.2 (no day from a session), §5 (only the world's midnight; timed events). |
| A viable proposal for up to four players | Done | Done | §4.6 and the four-player tests. |
| A plan that keeps Classic Enhanced intact | Partial | Done | Rule zero, `ClockShare::Own` paths untouched (test `alone a course stands the clock still and its award jumps one hour`), and the §9 tickets. |

## Code items done here

- A co-op session that ended early (cancel, or the bonus try's reels) charged the partner an hour while the world ran
  seconds. The course now waits for the world's hour (`GameSession::courseHourRuns`, `GymPorts::hourRuns`), shared with
  #411 §5. Tests: `gym_course_tests.cpp`, `course_flow_tests.cpp` ("a session cut short holds its actors…").

## Remaining

- T7 to T10 in §9 of the design (host dialogs and local screens still stand the world's clock, shared machines draw two
  partners on one spot, optional host validation). None is needed for the issue's criteria.
- Not verified on two real machines: the hour wait was measured with real sessions, fields and courses in tests only.
