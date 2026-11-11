#!/usr/bin/env python3
"""Fork and manage DWE UI skins (docs/architecture/ui.md#aspectos).

A DWE *skin* (aspecto) is one folder `content/packs/game/ui/skins/<id>/` with a `skin.yaml`
and per-screen `.rcss` style sheets. The documents (ui/<pantalla>/) never change with a skin:
only the look does. A mod adds one by dropping a folder; the game starts with the skin named in
`ui/ui_skin.yaml` (`default`, optional `fallback`).

This tool makes it easy to derive a complete new skin from an existing one:

    new_skin.py list
    new_skin.py fork <new-id> --from <existing-id> [--flatten] [--set-default] [--label NAME]

`--flatten` folds the source's `extends:` chain into one self-contained skin: every asset
referenced through the chain (base + source) is copied under `<new-id>/` (base under
`_base/` so nothing collides), the theme and each screen's style list merge in order, and
`extends:` is dropped. Use this to isolate the old interface as a single swappable skin.

`--set-default` edits `ui/ui_skin.yaml` so the game starts with the new skin and demotes the
previous default to `fallback`.

Generated `skin.yaml` files use ids relative to the skin's own folder (`theme: [theme]`,
`screens.<pantalla>.styles: [pause_menu/style]`, `document: main_menu/document`): `parseSkin`
prefixes them with `ui/skins/<id>/`. Use `--absolute` for the full `ui/skins/<id>/…` form.

Screens come from `ui/<folder>/document.rml`. Any screen the source doesn't style gets a stub
`.rcss` and a `screens.<name>.styles` entry, so the fork covers every screen of the UI and the
author has one obvious place to edit each. `dev_assets` is skin-neutral by design and is skipped
unless `--all-screens`.

No third-party deps: Python 3.11+ stdlib only (matches scripts/packages.py).
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GAME_PACK = REPO / "content/packs/game"
UI_ROOT = GAME_PACK / "ui"
SKINS_DIR = UI_ROOT / "skins"
SKIN_CHOICE = UI_ROOT / "ui_skin.yaml"
LOCALIZATION_ROOT = GAME_PACK / "localization"
SKIN_ID_RE = re.compile(r"^[a-z0-9_]{1,64}$")
SUPPORT_DIRS = {"common", "flags", "fonts", "skins"}
NEUTRAL_SCREENS = {"dev_assets"}  # its whole look lives in its structure.rcss (docs/architecture/ui.md)


# --------------------------------------------------------------------------- helpers


def screen_folders() -> list[str]:
    """Every screen of the UI: ui/<name>/ that has a document.rml, sorted."""
    return sorted(
        d.name
        for d in UI_ROOT.iterdir()
        if d.is_dir() and d.name not in SUPPORT_DIRS and (d / "document.rml").is_file()
    )


def skin_ids() -> list[str]:
    if not SKINS_DIR.is_dir():
        return []
    return sorted(d.name for d in SKINS_DIR.iterdir() if d.is_dir() and (d / "skin.yaml").is_file())


def skin_yaml_path(id_: str) -> Path:
    return SKINS_DIR / id_ / "skin.yaml"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


# --------------------------------------------------------------------------- yaml-lite parsing

# The existing skin.yaml files use a predictable shape (schema `ui_skin`). We only read the
# pieces we need to rewrite, and preserve every other line (including comments) verbatim.

SCREENS_HEADER_RE = re.compile(r"^screens:\s*$", re.M)


def top_scalar(text: str, key: str) -> str | None:
    m = re.search(rf"^{key}:\s*(\S.*?)\s*$", text, re.M)
    if not m:
        return None
    return m.group(1).strip().strip('"').strip("'")


def set_top_scalar(text: str, key: str, value: str) -> str:
    line = f"{key}: {value}"
    if re.search(rf"^{key}:", text, re.M):
        return re.sub(rf"^{key}:.*$", line, text, count=1, flags=re.M)
    # Insert after the leading comment block, at the top of the body.
    return f"{line}\n" + text


def drop_line(text: str, key: str) -> str:
    return re.sub(rf"^{key}:.*\n", "", text, count=1, flags=re.M)


def parse_theme(text: str) -> list[str]:
    """The entries of the top-level `theme:` block."""
    m = re.search(r"^theme:\s*\n((?:[ \t]+-.*\n)+)", text, re.M)
    if not m:
        return []
    return [re.sub(r"^\s*-\s*", "", line).strip() for line in m.group(1).splitlines() if line.strip()]


def parse_screens(text: str) -> dict[str, dict]:
    """Parse the `screens:` block into {name: {"styles":[...], "document": str|None}}.

    Styles are inline arrays like `[a, b]`; a `document:` line is a plain scalar.
    """
    m = SCREENS_HEADER_RE.search(text)
    if not m:
        return {}
    body_start = m.end()
    # Screens block ends at the next top-level key at column 0.
    rest = text[body_start:]
    nxt = re.search(r"^[A-Za-z_][A-Za-z_0-9]*:", rest, re.M)
    block = rest[: nxt.start()] if nxt else rest
    result: dict[str, dict] = {}
    cur: dict | None = None
    for raw in block.splitlines():
        if not raw.strip():
            continue
        if raw.startswith("  ") and not raw.startswith("    ") and raw.rstrip().endswith(":"):
            name = raw.strip().rstrip(":")
            cur = {"styles": [], "document": None}
            result[name] = cur
            continue
        if cur is None:
            continue
        s = raw.strip()
        if s.startswith("#"):
            continue
        if s.startswith("styles:"):
            arr = s[len("styles:"):].strip()
            inner = arr.strip().lstrip("[").rstrip("]").strip()
            if inner:
                cur["styles"] = [x.strip().strip('"').strip("'") for x in inner.split(",")]
        elif s.startswith("document:"):
            cur["document"] = s[len("document:"):].strip().strip('"').strip("'")
    return result


def parse_document_classes(text: str) -> dict[str, dict[str, list[str]]]:
    m = re.search(r"^document_classes:\s*\n((?:[ \t]+.*\n)+)", text, re.M)
    if not m:
        return {}
    out: dict[str, dict[str, list[str]]] = {}
    cur_doc = None
    for line in m.group(1).splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        if not line.startswith("      "):  # depth: two-space document, four-space element
            if line.startswith("  ") and s.endswith(":"):
                cur_doc = s.rstrip(":")
                out[cur_doc] = {}
                continue
        if cur_doc and ":" in s:
            key, val = s.split(":", 1)
            val = val.strip().lstrip("[").rstrip("]").strip()
            out[cur_doc][key.strip()] = [x.strip() for x in val.split(",") if x.strip()]
    return out


# --------------------------------------------------------------------------- rewrite


def rewrite_asset_paths(text: str, replacements: list[tuple[str, str]]) -> str:
    """Every occurrence of `ui/skins/<id>/...` and `ui/skins/<id>` (no slash) → new prefix."""
    for old, new in replacements:
        # Rewrite both forms: bare and followed by path separator.
        text = re.sub(rf"\b{re.escape(old)}(?=[/_\s\]\)'\"]|\b)", new, text)
    return text


def relativize_paths(text: str, new_id: str) -> str:
    """Write theme sheets, per-screen styles and document overrides relative to the skin's own
    folder (`parseSkin` prefixes them with `ui/skins/<id>/`). Sprite-sheet images are left alone:
    they name base assets, not the skin's."""
    prefix = f"ui/skins/{new_id}/"
    # `- ui/skins/<id>/X` in the theme list.
    text = re.sub(rf"^(\s*- ){re.escape(prefix)}(\S+)$", r"\1\2", text, flags=re.M)
    # `document: ui/skins/<id>/X`.
    text = re.sub(rf"^(\s*document: ){re.escape(prefix)}(\S+)$", r"\1\2", text, flags=re.M)
    # `styles: [ui/skins/<id>/A, ui/skins/<id>/B]` — drop the prefix from each entry.
    def _strip_styles(m: re.Match) -> str:
        items = [p.strip() for p in m.group(2).split(",")] if m.group(2).strip() else []
        items = [p[len(prefix):] if p.startswith(prefix) else p for p in items]
        return m.group(1) + "[" + ", ".join(items) + "]"

    return re.sub(rf"^(\s*styles: )\[([^\]]*)\]", _strip_styles, text, flags=re.M)


def copy_tree(src: Path, dst: Path) -> None:
    if not src.is_dir():
        return
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


# --------------------------------------------------------------------------- commands


def cmd_list(_: argparse.Namespace) -> int:
    print(f"screens: {len(screen_folders())}")
    for s in screen_folders():
        marker = "  (skin-neutral)" if s in NEUTRAL_SCREENS else ""
        print(f"  - {s}{marker}")
    print(f"skins: {skin_ids()}")
    default = top_scalar(read(SKIN_CHOICE), "default") if SKIN_CHOICE.is_file() else None
    print(f"default: {default}")
    return 0


def _flatten(new_id: str, source_yaml: str, base_id: str, source_id: str) -> tuple[str, dict[str, list[str]]]:
    """Fold `base_id`'s look into a fork of `source_id`. Returns the new skin.yaml text.

    Base sheets go under `_base/` so nothing collides; the source's sheets stay at their natural
    path under the new id. `theme:` merges base-then-source; each screen's `styles:` list merges
    the same way. Any screen in the base but not the source is appended at the end of the block.
    """
    # Step 1. Rewrite source paths: ui/skins/<source_id>/... → ui/skins/<new_id>/...
    text = rewrite_asset_paths(source_yaml, [(f"ui/skins/{source_id}", f"ui/skins/{new_id}")])
    # Step 2. Drop the `extends:` line so the skin is self-contained.
    text = drop_line(text, "extends")
    # Step 3. Merge the base's theme sheets in FRONT of the source's.
    base_yaml_path = skin_yaml_path(base_id)
    if not base_yaml_path.is_file():
        raise SystemExit(f"base skin '{base_id}' not found")
    base_yaml = read(base_yaml_path)
    base_theme_rewritten = [
        p.replace(f"ui/skins/{base_id}", f"ui/skins/{new_id}/_base") for p in parse_theme(base_yaml)
    ]
    if base_theme_rewritten:
        entries_block = "".join(f"  - {p}\n" for p in base_theme_rewritten)
        m = re.search(r"^theme:[ \t]*\n", text, re.M)
        if m:
            # Insert the base's sheets right under `theme:`, keeping the source's own entries.
            text = text[: m.end()] + entries_block + text[m.end():]
        else:
            # No `theme:` block yet: create one right after the top scalars (name / id).
            nm = re.search(r"^name:.*\n", text, re.M)
            pos = nm.end() if nm else 0
            text = text[:pos] + "theme:\n" + entries_block + text[pos:]
    # Step 4. Merge the base's per-screen styles in FRONT of the source's, per screen.
    base_screens = parse_screens(base_yaml)
    src_screens = parse_screens(text)  # AFTER the rewrite, so paths use <new_id>.
    for screen, spec in base_screens.items():
        rewritten = [p.replace(f"ui/skins/{base_id}", f"ui/skins/{new_id}/_base") for p in spec["styles"]]
        if not rewritten:
            continue
        existing = src_screens.get(screen, {}).get("styles") or []
        if existing:
            new_arr = "[" + ", ".join(rewritten + existing) + "]"
            pat = re.compile(
                rf"^(  {re.escape(screen)}:[ \t]*\n(?:[ \t]+#[^\n]*\n)*[ \t]+styles:[ \t]*)\[[^\]]*\]",
                re.M,
            )
            text = pat.sub(lambda mm: mm.group(1) + new_arr, text, count=1)
        else:
            entry = f"  # inherited from {base_id}\n  {screen}:\n    styles: [{', '.join(rewritten)}]\n"
            m = re.search(r"^screens:[ \t]*\n", text, re.M)
            if not m:
                text = text.rstrip() + "\n\nscreens:\n" + entry
            else:
                nxt = re.search(r"^[a-zA-Z_][a-zA-Z_0-9]*:", text[m.end():], re.M)
                end = m.end() + (nxt.start() if nxt else len(text) - m.end())
                text = text[:end] + entry + text[end:]
    return text, base_screens


def _copy_flatten_base_rcss(new_dir: Path, base_id: str, new_id: str) -> None:
    base_dir = SKINS_DIR / base_id
    if not base_dir.is_dir():
        return
    dst = new_dir / "_base"
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)
    for src in base_dir.rglob("*.rcss"):
        rel = src.relative_to(base_dir)
        write(dst / rel, read(src))  # contents don't reference skin paths; copy verbatim


def _stub_missing_screens(new_dir: Path, new_id: str, text: str, screens: list[str]) -> tuple[str, list[str]]:
    """For any screen the fork lacks, add a stub `screens.<name>.styles` entry and an empty .rcss.
    Returns the (possibly modified) skin.yaml text and the list of stubs added."""
    covered = set(parse_screens(text).keys()) | NEUTRAL_SCREENS
    missing = [s for s in screens if s not in covered]
    if not missing:
        return text, []
    entry_lines = "".join(f"  {s}:\n    styles: [ui/skins/{new_id}/{s}/style]\n" for s in missing)
    for s in missing:
        stub = new_dir / s / "style.rcss"
        stub.parent.mkdir(parents=True, exist_ok=True)
        write(
            stub,
            f"/* Style for {s} in the {new_id} skin. Forked from an existing skin by scripts/new_skin.py;"
            f"\n   add your rules here (docs/architecture/ui.md#aspectos). */\n\n",
        )
    # Append the block after `screens:` if any, or create it before the first top-level key.
    m = re.search(r"^screens:\s*$", text, re.M)
    if m:
        # Find the true end of the screens block (line ending before next top-level key at col 0).
        rest = text[m.end():]
        nxt = re.search(r"^[A-Za-z_][A-Za-z_0-9]*:", rest, re.M)
        end = m.end() + (nxt.start() if nxt else len(rest))
        insert_at = end
        while insert_at > m.end() and text[insert_at - 1] == "\n":
            insert_at -= 1
        text = text[:insert_at] + "\n" + entry_lines.rstrip() + "\n" + text[insert_at:].lstrip("\n")
    else:
        text = text.rstrip() + "\n\nscreens:\n" + entry_lines
    return text, missing


def _append_localization(new_id: str, label: str) -> None:
    for lang in ("en", "es"):
        loc = LOCALIZATION_ROOT / lang / "ui_skin.yaml"
        if not loc.is_file():
            continue
        text = read(loc)
        line = f'{new_id}: "{label}"'
        if re.search(rf"^{re.escape(new_id)}:", text, re.M):
            text = re.sub(rf"^{re.escape(new_id)}:.*$", line, text, count=1, flags=re.M)
        else:
            text = text.rstrip() + "\n" + line + "\n"
        write(loc, text)


def _set_default(new_id: str) -> None:
    if not SKIN_CHOICE.is_file():
        raise SystemExit(f"{SKIN_CHOICE} not found")
    text = read(SKIN_CHOICE)
    old_default = top_scalar(text, "default")
    text = set_top_scalar(text, "default", new_id)
    if old_default and old_default != new_id:
        text = set_top_scalar(text, "fallback", old_default)
    write(SKIN_CHOICE, text)


def cmd_fork(args: argparse.Namespace) -> int:
    new_id = args.new_id
    if not SKIN_ID_RE.match(new_id):
        print(f"error: id {new_id!r} must match {SKIN_ID_RE.pattern}", file=sys.stderr)
        return 2
    if args.from_id not in skin_ids():
        print(f"error: source skin {args.from_id!r} not in {skin_ids()}", file=sys.stderr)
        return 2
    new_dir = SKINS_DIR / new_id
    if new_dir.exists() and not args.force:
        print(f"error: {new_dir.relative_to(REPO)} exists (use --force to overwrite)", file=sys.stderr)
        return 2

    source_yaml = read(skin_yaml_path(args.from_id))
    extends = top_scalar(source_yaml, "extends")

    if args.flatten and extends:
        text, _ = _flatten(new_id, source_yaml, extends, args.from_id)
    else:
        text = rewrite_asset_paths(source_yaml, [(f"ui/skins/{args.from_id}", f"ui/skins/{new_id}")])
        if args.flatten and not extends:
            # Nothing to flatten; drop the line if present.
            text = drop_line(text, "extends")

    # Identity.
    text = set_top_scalar(text, "id", new_id)
    text = set_top_scalar(text, "name", f"ui_skin.{new_id}")

    # Copy the source's .rcss files into the new folder (paths inside are already rewritten).
    # The cursor (if any) is the skin's own image: copy the .png next to the .rcss files so a fork
    # keeps it. Sprite-sheet `image:` entries point at base assets (e.g. `texture/etcdat/*`) and are
    # not skin-local files, so they don't need copying.
    if new_dir.exists():
        shutil.rmtree(new_dir)
    new_dir.mkdir(parents=True)
    src_dir = SKINS_DIR / args.from_id
    for asset in list(src_dir.rglob("*.rcss")) + list(src_dir.rglob("*.png")):
        rel = asset.relative_to(src_dir)
        write(new_dir / rel, read(asset))
    if args.flatten and extends:
        _copy_flatten_base_rcss(new_dir, extends, new_id)

    # Fill gaps so every screen has a place to edit.
    screens = screen_folders() if args.all_screens else [s for s in screen_folders() if s not in NEUTRAL_SCREENS]
    text, stubs = _stub_missing_screens(new_dir, new_id, text, screens)

    if not args.absolute:
        # Emit the shorter relative form now that parseSkin prefixes it with `ui/skins/<id>/` itself.
        text = relativize_paths(text, new_id)

    write(new_dir / "skin.yaml", text)

    _append_localization(new_id, args.label or new_id)
    if args.set_default:
        _set_default(new_id)

    rel = new_dir.relative_to(REPO)
    print(f"created {rel} (forked from {args.from_id}{', flattened ' + extends if args.flatten and extends else ''})")
    rcss_count = sum(1 for _ in new_dir.rglob("*.rcss"))
    print(f"  skin.yaml + {rcss_count} .rcss files")
    if stubs:
        print(f"  stubbed {len(stubs)} missing screens: {', '.join(stubs[:8])}{'…' if len(stubs) > 8 else ''}")
    if args.set_default:
        print(f"  set ui/ui_skin.yaml default: {new_id}")
    print("now edit each .rcss and set ui/ui_skin.yaml; run scripts/dev.sh test --filter ...screens to check.")
    return 0


# --------------------------------------------------------------------------- main


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    ls = sub.add_parser("list", help="Show the canonical screens and the skins the pack offers")
    ls.set_defaults(fn=cmd_list)

    fk = sub.add_parser("fork", help="Fork an existing skin into a new, complete, self-contained one")
    fk.add_argument("new_id", help="The id / folder name of the new skin")
    fk.add_argument("--from", dest="from_id", default="original", help="Skin to fork (default: original)")
    fk.add_argument(
        "--flatten",
        action="store_true",
        help="Fold the source's `extends:` chain into one self-contained skin (base copied under _base/)",
    )
    fk.add_argument("--set-default", action="store_true", help="Point ui/ui_skin.yaml at the new skin")
    fk.add_argument("--label", help="Player-facing name for the localization entry (default = the id)")
    fk.add_argument(
        "--all-screens",
        action="store_true",
        help="Also style the screens the docs say are skin-neutral (currently: dev_assets)",
    )
    fk.add_argument("--force", action="store_true", help="Overwrite if the folder already exists")
    fk.add_argument(
        "--absolute",
        action="store_true",
        help="Keep the full `ui/skins/<id>/…` asset ids instead of the relative form",
    )
    fk.set_defaults(fn=cmd_fork)

    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
