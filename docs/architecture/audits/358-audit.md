# Audit of #358: interpolation and smoothing of remote states

Checked on 2026-11-27 against `wave/w135-integrate` (wave w146). "Before" is the state the wave found; "After" is the
state on its branch. Design: [multiplayer](../multiplayer.md) §5.7, «Interpolation (#358)».

Remote bodies are drawn in three places, all on the same `PoseTrack` (`packages/game/coop/src/authority.cpp`): the
host draws each guest's player (`CoopHostWorld::tracks_`) and partner (`CoopHostWorld::partnerTracks_`,
`CoopHostWorld::partnerAt`), and each guest draws every entity of the host's snapshots (`CoopSnapshotInterpolation`).
The fixes below are in `PoseTrack` and `CoopSnapshotInterpolation`, so all three get them.

## Acceptance criteria

| # | Criterion | Before | After | Evidence |
|---|---|---|---|---|
| 1 | A buffer of recent states is kept | Done | Done | `PoseTrack` keeps up to 32 timed states per remote body (`PoseTrackRules::capacity`); one track per guest player and partner on the host, one per snapshot entity on a guest |
| 2 | Remote positions are interpolated between updates | Done | Done | Played 100 ms behind the newest state on the sender's clock (`PoseTrackRules::delay`); tests "a pose track plays remote motion smoothly behind the newest state" (`authority_tests.cpp`), "client presentation plays each entity smoothly on the host's clock, shortest yaw" (`coop_snapshot_interpolation_tests.cpp`), "each game sees the other player's partner following them, interpolated" (`coop_gameplay_bridge_tests.cpp`) |
| 3 | Late packets are handled | Partial | Done | Before: a late state was always dropped, even one still ahead of the playback that would have filled a hole; a guest took any snapshot of another map as a map change, so a late one of the map before took its drawing back there and cleared every track; a track reset on any state of another map. After: a late state ahead of the playback fills its place, one already played is dropped, and only a newer state or snapshot changes map. Tests "a pose track takes a late state still ahead of its playback, and drops one played", "a pose track never goes back to a map for a straggler sent before the change" (`pose_track_tests.cpp`), "a late snapshot fills a track in time but never takes the guest back a map (#358)" (`coop_snapshot_interpolation_tests.cpp`) |
| 4 | Needless visual jumps are avoided | Partial | Done | Before: when states came back after a loss the body jumped ahead (a 600 ms loss of a run jumped about 400 units in one frame), and a run longer than 800 units across a loss was drawn as a jump. After: the track catches up from where it held within the 100 ms delay, and only a step faster than anything moves is a jump. Test "a pose track slides a long run across lost states and catches up without a jump" (`pose_track_tests.cpp`, under 100 units a frame) |
| 5 | A temporary loss of updates is handled | Partial | Done | Before: past the newest state it extrapolated 100 ms and held (done), but the way back was the jump of criterion 4. After: same hold, smooth catch-up. Tests as criterion 4, and "the host drives the guest's body from its valid states, smoothed and never stale" (`coop_world_tests.cpp`) |
| 6 | Sudden moves and legitimate teleports are drawn right | Partial | Done | Before: a map change started the track over and a host correction dropped it (`CoopHostWorld::forgetTracks`), but any step under 800 units slid, so a warp inside a map, a script putting a character somewhere or a partner put back next to its player slid there. After: a step farther than 2500 units a second (plus 64) on the sender's clock is a jump, with no extrapolation from it. Test "a pose track draws a short teleport as a jump, not a slide" (`pose_track_tests.cpp`) |
| 7 | Interpolation does not change the authority over the player's real state | Done | Done | `WorldAuthority` takes the raw state (`accept`) and the track only feeds the drawn body; test "the host drives the guest's body from its valid states, smoothed and never stale" checks the authority holds x 1000 while the body is drawn at 925. A guest's presentation never writes its own field (`CoopSnapshotInterpolation`) |

Before: 3 Done, 4 Partial, 0 Missing. After: 7 Done.

## Description

| Point | State | Evidence |
|---|---|---|
| Keep recent samples instead of applying each received position | Done | `PoseTrack` |
| Interpolation uses the update's timestamp or sequence | Done | Sequence for order and repeats, the sender's clock (`PlayerState::sequence` ticks, snapshot world tick × 50 ms) for timing; the smallest delivery offset maps it to the local clock |

## Still open

- A teleport hidden inside a long loss cannot be told from a run (the step is within what the sender could have run in
  that time), so it is drawn as the 100 ms catch-up, not a jump. A sender-side teleport counter in the state would fix
  it at the cost of a protocol change; not done.
- A sender whose clock paused (a long load) is picked up again by resetting the delay; the first frame after it can
  step back by up to the 100 ms extrapolation.
- Outside #358: a guest's own in-map teleport farther than `AuthorityRules` allows (2000 units a second plus 300) is
  refused by `WorldAuthority::accept`, and after three refusals the host corrects the guest back. That is the
  authority's speed check (#353, #354), not the drawing.
- Only tried headless (tests); two real machines over the Internet were not tried in this wave.
