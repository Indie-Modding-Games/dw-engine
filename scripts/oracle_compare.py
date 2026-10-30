#!/usr/bin/env python3
"""Compares a state trace of the original game with one of dwe (docs/guides/traza-de-estado.md).

usage: oracle_compare.py <reference.jsonl> <dwe.jsonl> [--align input|move|battle|fight] [--json <report.json>]
                         [--tolerance <field>=<units>]... [--max-seconds <s>]

Both traces hold one JSON line per sampled frame (the reference per vblank, dwe per logic tick) with
positions in the original's axes and units and yaws in 4096 a turn. The traces are put on a common
clock from an alignment point found in each one:
  input   the first frame with a d-pad button held (default)
  move    the first frame in which the player moved
  battle  the first frame of a battle
  fight   the first frame in which the battle's fighters act (after its opening)
Then for every dwe frame the nearest reference frame in time is compared (in a fight, when the
reference records its battle frame, the reference's same battle frame), field by field, and the
first frame where a field differs beyond its tolerance is reported, with the largest difference and
how much of the run differs. Events are compared too: when the player and the partner start and stop
moving, their speeds, and in a battle when each fighter's HP and MP change.

Prints a short summary in Spanish and, with --json, writes the full report.
"""
import argparse
import json
import math
import sys

DPAD = 0x1000 | 0x2000 | 0x4000 | 0x8000

DEFAULT_TOLERANCE = {
    "player.pos": 24.0,
    "player.yaw": 160.0,
    "partner.pos": 48.0,
    "partner.yaw": 256.0,
    "partner.gap": 48.0,
    "npc.pos": 48.0,
    "fighter.pos": 48.0,
    "fighter.hp": 0.0,
    "fighter.mp": 0.0,
}


def load(path):
    meta, frames = {}, []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("meta"):
                meta = rec
                continue
            frames.append(rec)
    if not frames:
        sys.exit(f"{path}: no frames")
    hz = meta.get("hz", 60.0)
    for rec in frames:
        rec["_t"] = rec["f"] / hz
    return meta, frames


def fighters(rec):
    """The battle's fighters, partner first: from the battle record, or the partner and the NPCs."""
    battle = rec.get("battle") or {}
    if "fighters" in battle:
        return battle["fighters"]
    if rec.get("mode") != "battle":
        return []
    out = [rec["partner"]] if rec.get("partner") else []
    out += sorted(rec.get("npcs", []), key=lambda n: n.get("slot", 0))
    return out


def moved(a, b, eps=0.5):
    return a and b and (abs(a["x"] - b["x"]) > eps or abs(a["z"] - b["z"]) > eps)


def align_point(frames, how):
    if how == "input":
        for rec in frames:
            if rec.get("in", 0) & DPAD:
                return rec["_t"]
    elif how == "move":
        for prev, rec in zip(frames, frames[1:]):
            if moved(prev.get("player"), rec.get("player")):
                return prev["_t"]
    elif how == "battle":
        for rec in frames:
            if rec.get("mode") == "battle":
                return rec["_t"]
    elif how == "fight":
        # dwe says its phase; the reference's game state goes 3 (opening), 2 (the player steps back),
        # then 1 once the fighters move.
        for prev, rec in zip(frames, frames[1:]):
            if (rec.get("battle") or {}).get("phase") == "fight":
                return rec["_t"]
            if rec.get("mode") == "battle" and prev.get("game_state") == 2 and rec.get("game_state") == 1:
                return rec["_t"]
    return None


def dist(a, b):
    return math.hypot(a["x"] - b["x"], a["z"] - b["z"])


def yaw_diff(a, b):
    d = (a - b) % 4096
    return min(d, 4096 - d)


class Field:
    """One compared quantity: first divergence, largest difference, share of frames beyond tolerance."""

    def __init__(self, name, tolerance):
        self.name, self.tolerance = name, tolerance
        self.first = None
        self.max = 0.0
        self.max_at = None
        self.over = 0
        self.count = 0

    def add(self, t, diff, ref, got):
        self.count += 1
        if diff > self.max:
            self.max, self.max_at = diff, round(t, 3)
        if diff > self.tolerance:
            self.over += 1
            if self.first is None:
                self.first = {"t": round(t, 3), "diff": round(diff, 1), "reference": ref, "dwe": got}

    def report(self):
        return {"field": self.name, "tolerance": self.tolerance, "frames": self.count,
                "frames_over": self.over, "max_diff": round(self.max, 1), "max_at": self.max_at,
                "first_divergence": self.first}


JUMP = 150.0  # a move longer than this in one frame is a placement or a map change, not a step


def motion_events(frames, t0, who):
    """When `who` starts and stops moving (relative to t0, from t0 on), its jumps (placements, map
    changes) and its mean ground speed while moving. The original moves on 20 Hz logic ticks inside
    60 Hz frames, so stops shorter than 0.2 s are merged and speeds are taken over whole stretches."""
    frames = [r for r in frames if r["_t"] >= t0 and r.get(who)]
    raw, jumps = [], []
    moving = False
    for prev, rec in zip(frames, frames[1:]):
        a, b = prev[who], rec[who]
        step = dist(a, b)
        if step > JUMP or prev.get("map") != rec.get("map"):
            jumps.append({"t": round(rec["_t"] - t0, 3), "map": rec.get("map"), "x": b["x"], "z": b["z"]})
            continue
        now = step > 0.5
        if now != moving:
            raw.append({"t": round(rec["_t"] - t0, 3), "event": "start" if now else "stop", "x": b["x"], "z": b["z"]})
            moving = now
    events = merge_starts(raw)
    # Path and time inside the merged moving stretches.
    path, moving_time = 0.0, 0.0
    spans = []
    for i, e in enumerate(events):
        if e["event"] == "start":
            stop = next((x["t"] for x in events[i + 1:] if x["event"] == "stop"), round(frames[-1]["_t"] - t0, 3))
            spans.append((e["t"], stop))
    for lo, hi in spans:
        seg = [r for r in frames if lo - 1e-6 <= r["_t"] - t0 <= hi + 1e-6]
        for p, r in zip(seg, seg[1:]):
            d = dist(p[who], r[who])
            if d <= JUMP and p.get("map") == r.get("map"):
                path += d
        moving_time += hi - lo
    return {"events": events, "jumps": jumps, "path": round(path, 1),
            "mean_speed": round(path / moving_time, 1) if moving_time > 0 else 0.0}


def merge_starts(events, gap=0.2):
    """Start/stop events with stops shorter than `gap` seconds merged (20 Hz steps look like stops)."""
    out = []
    for e in events:
        if out and e["event"] == "start" and out[-1]["event"] == "stop" and e["t"] - out[-1]["t"] < gap:
            out.pop()
            continue
        out.append(e)
    return out


def stat_events(frames, t0):
    """Each fighter's HP and MP changes (relative to t0), by fighter index."""
    out = {}
    prev = None
    for rec in frames:
        if rec["_t"] < t0:
            continue
        fs = fighters(rec)
        if prev is not None:
            for i, f in enumerate(fs):
                if i >= len(prev):
                    continue
                for key in ("hp", "mp"):
                    if key in f and key in prev[i] and f[key] != prev[i][key]:
                        out.setdefault(str(i), []).append({"t": round(rec["_t"] - t0, 3), "stat": key,
                                                           "from": prev[i][key], "to": f[key]})
        prev = fs if fs else prev
    return out


def battle_frames(ref_frames):
    """The reference's fight by battle frame (BATTLE_FRAME_COUNT): the last sample of each frame,
    taken once the frame's logic has run."""
    out = {}
    for rec in ref_frames:
        frame = (rec.get("battle") or {}).get("frame")
        if rec.get("mode") == "battle" and rec.get("game_state") == 1 and frame is not None and frame >= 1:
            out[frame] = rec
    return out


def played_frame(rec):
    """The battle frame a dwe record shows the result of, in the fight: dwe counts the frames it has
    played (Battle::step adds one at its end), the original the one it is playing, so dwe's frame n
    is the original's n - 1. In a fight the traces pair by frame, not by time: the original's frame
    takes 3 vblanks or more, so time drifts from frame to frame."""
    battle = rec.get("battle") or {}
    if battle.get("phase") != "fight" or battle.get("frame") is None:
        return None
    return battle["frame"] - 1


def compare(ref_frames, dwe_frames, align, tol, max_seconds):
    t_ref = align_point(ref_frames, align)
    t_dwe = align_point(dwe_frames, align)
    if t_ref is None or t_dwe is None:
        sys.exit(f"no alignment point ({align}) in {'the reference' if t_ref is None else 'the dwe trace'}")
    fields = {}

    def field(name, kind):
        if name not in fields:
            fields[name] = Field(name, tol[kind])
        return fields[name]

    ref_by_frame = battle_frames(ref_frames) if align == "fight" else {}
    j = 0
    for rec in dwe_frames:
        t = rec["_t"] - t_dwe
        if t < 0 or (max_seconds and t > max_seconds):
            continue
        target = t + t_ref
        while j + 1 < len(ref_frames) and abs(ref_frames[j + 1]["_t"] - target) <= abs(ref_frames[j]["_t"] - target):
            j += 1
        ref = ref_frames[j]
        paired = ref_by_frame.get(played_frame(rec))
        if paired is not None:
            ref = paired
        elif abs(ref["_t"] - target) > 0.1:
            continue
        if ref.get("mode") != rec.get("mode"):
            field("mode", "player.pos").add(t, 1e9, ref.get("mode"), rec.get("mode"))
        else:
            field("mode", "player.pos").add(t, 0.0, None, None)
        same_map = ref.get("map") == rec.get("map")
        field("map", "player.pos").add(t, 0.0 if same_map else 1e9, ref.get("map"), rec.get("map"))
        battle = rec.get("mode") == "battle" and ref.get("mode") == "battle"
        for who in ("player", "partner"):
            if battle and who == "partner":
                continue
            a, b = ref.get(who), rec.get(who)
            if a and b:
                field(f"{who}.pos", f"{who}.pos").add(t, dist(a, b), [a["x"], a["z"]], [b["x"], b["z"]])
                field(f"{who}.yaw", f"{who}.yaw").add(t, yaw_diff(a["yaw"], b["yaw"]), a["yaw"], b["yaw"])
        if not battle and all(x.get(w) for x in (ref, rec) for w in ("player", "partner")):
            # How far the partner keeps from the player: an offset of the whole run does not count.
            gap_ref, gap_dwe = dist(ref["player"], ref["partner"]), dist(rec["player"], rec["partner"])
            field("partner.gap", "partner.gap").add(t, abs(gap_ref - gap_dwe), round(gap_ref), round(gap_dwe))
        if not battle:
            ref_npcs = {n["id"]: n for n in ref.get("npcs", []) if "id" in n}
            for n in rec.get("npcs", []):
                m = ref_npcs.get(n.get("id"))
                if m:
                    field(f"npc{n['id']}.pos", "npc.pos").add(t, dist(m, n), [m["x"], m["z"]], [n["x"], n["z"]])
        else:
            for i, (a, b) in enumerate(zip(fighters(ref), fighters(rec))):
                field(f"fighter{i}.pos", "fighter.pos").add(t, dist(a, b), [a["x"], a["z"]], [b["x"], b["z"]])
                for key in ("hp", "mp"):
                    if key in a and key in b:
                        field(f"fighter{i}.{key}", f"fighter.{key}").add(t, abs(a[key] - b[key]), a[key], b[key])
    ref_end = ref_frames[-1]["_t"] - t_ref
    dwe_end = dwe_frames[-1]["_t"] - t_dwe
    span = min(ref_end, dwe_end, max_seconds or 1e9)
    clip = lambda fr, t0: [r for r in fr if r["_t"] - t0 <= span]  # noqa: E731
    motion = {}
    for who in ("player", "partner"):
        r = motion_events(clip(ref_frames, t_ref), t_ref, who)
        d = motion_events(clip(dwe_frames, t_dwe), t_dwe, who)
        motion[who] = {"reference": r, "dwe": d}
    stats = {"reference": stat_events(clip(ref_frames, t_ref), t_ref),
             "dwe": stat_events(clip(dwe_frames, t_dwe), t_dwe)}
    return {"align": align, "align_reference_s": round(t_ref, 3), "align_dwe_s": round(t_dwe, 3),
            "compared_seconds": round(span, 3),
            "fields": [f.report() for f in sorted(fields.values(), key=lambda f: (f.first or {}).get("t", 1e9))],
            "motion": motion, "stats": stats}


def summary(report):
    lines = [f"Comparación (alineado por {report['align']}; {report['compared_seconds']} s comparados)"]
    diverged = [f for f in report["fields"] if f["first_divergence"]]
    if not diverged:
        lines.append("  Sin divergencias por encima de las tolerancias.")
    for f in diverged:
        d = f["first_divergence"]
        share = 100.0 * f["frames_over"] / max(1, f["frames"])
        lines.append(f"  {f['field']}: primera divergencia a {d['t']} s (dif {d['diff']}, tolerancia {f['tolerance']}): "
                     f"original {d['reference']} / dwe {d['dwe']}; máx {f['max_diff']} a {f['max_at']} s; "
                     f"{share:.0f}% de los frames fuera")
    for who in ("player", "partner"):
        m = report["motion"][who]
        r, d = m["reference"], m["dwe"]
        if not r["events"] and not d["events"]:
            continue
        name = "jugador" if who == "player" else "compañero"

        def starts(x):
            return [e["t"] for e in x["events"] if e["event"] == "start"][:4]

        lines.append(f"  {name}: arranques original {starts(r)} / dwe {starts(d)}; recorrido {r['path']} / {d['path']}; "
                     f"velocidad media {r['mean_speed']} / {d['mean_speed']} u/s")
        if r["jumps"] or d["jumps"]:
            jumps = lambda x: [(j["t"], j["map"]) for j in x["jumps"]][:3]  # noqa: E731
            lines.append(f"    saltos (cambio de mapa o recolocación): original {jumps(r)} / dwe {jumps(d)}")
    for i in sorted(set(report["stats"]["reference"]) | set(report["stats"]["dwe"])):
        r = [e for e in report["stats"]["reference"].get(i, []) if e["stat"] == "hp"]
        d = [e for e in report["stats"]["dwe"].get(i, []) if e["stat"] == "hp"]
        who = "compañero" if i == "0" else f"enemigo {i}"

        def fmt(es):
            return ", ".join(f"{e['t']}s {e['from']}->{e['to']}" for e in es[:5]) or "ninguno"

        lines.append(f"  HP {who}: original {fmt(r)} | dwe {fmt(d)}")
    return "\n".join(lines)


def timeline(ref_frames, dwe_frames, align, step, max_seconds):
    """Both traces side by side (t, buttons, player x z yaw, partner x z, fighters' HP)."""
    t_ref, t_dwe = align_point(ref_frames, align), align_point(dwe_frames, align)

    def at(frames, t):
        return min(frames, key=lambda r: abs(r["_t"] - t))

    def cell(rec):
        p, q = rec.get("player") or {}, rec.get("partner") or {}
        text = (f"in={rec.get('in', 0):5d} P({p.get('x')},{p.get('z')},{p.get('yaw')}) "
                f"C({q.get('x')},{q.get('z')})")
        fs = fighters(rec)
        if fs:
            text += " F" + "/".join(f"{f.get('x')},{f.get('z')}:{f.get('hp')}" for f in fs)
        return text

    end = min(ref_frames[-1]["_t"] - t_ref, dwe_frames[-1]["_t"] - t_dwe, max_seconds or 1e9)
    lines, t = [f"{'t':>6} | original | dwe"], -step
    while t <= end:
        lines.append(f"{t:6.2f} | {cell(at(ref_frames, t + t_ref))} | {cell(at(dwe_frames, t + t_dwe))}")
        t += step
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reference")
    ap.add_argument("dwe")
    ap.add_argument("--align", choices=("input", "move", "battle", "fight"), default="input")
    ap.add_argument("--json")
    ap.add_argument("--tolerance", action="append", default=[], metavar="FIELD=UNITS")
    ap.add_argument("--max-seconds", type=float, default=0.0)
    ap.add_argument("--timeline", type=float, default=0.0, metavar="SECONDS",
                    help="also print both traces side by side every SECONDS after the alignment point")
    args = ap.parse_args()
    tol = dict(DEFAULT_TOLERANCE)
    for item in args.tolerance:
        key, _, value = item.partition("=")
        if key not in tol:
            sys.exit(f"unknown tolerance {key!r}; known: {', '.join(tol)}")
        tol[key] = float(value)
    _, ref = load(args.reference)
    _, got = load(args.dwe)
    report = compare(ref, got, args.align, tol, args.max_seconds)
    report["tolerances"] = tol
    report["reference"], report["dwe_trace"] = args.reference, args.dwe
    if args.json:
        with open(args.json, "w") as fh:
            json.dump(report, fh, indent=1)
    print(summary(report))
    if args.timeline > 0:
        print(timeline(ref, got, args.align, args.timeline, args.max_seconds))
    return 1 if any(f["first_divergence"] for f in report["fields"]) else 0


if __name__ == "__main__":
    sys.exit(main())
