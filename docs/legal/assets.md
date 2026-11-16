# Legal asset policy

> This is not legal advice. It is the project's policy to minimize risk. Laws change by country. If the project's
> situation changes (money comes in, its reach grows a lot), an intellectual property lawyer must be consulted.

## What this project is

A **non-profit** fan project, made for enjoyment, to play the game again with our own reinterpretations. It does not
compete with the license holders nor aim to replace their products. If a rights holder asks for something to be
removed, **it is removed**.

## What the law protects

- **Copyright:** the game's code, art, models, music, texts and character design.
- **Trademarks:** the franchise name and its logos.

## The model: "bring your own data"

It is the same model used by projects such as OpenMW, OpenRCT2, ScummVM or Ship of Harkinian. The project publishes
an engine that **contains nothing from the original**, and each user brings their own copy of the game. The
extraction happens on their machine.

## What can be distributed

| Category | Examples | Distributed? |
|---|---|---|
| **Engine** | `packages/engine/`, `packages/game/`, tools | ✅ Under the project's license |
| **Base** | Models, backgrounds, music, texts and stat tables from the disc | ❌ Never. Generated on the user's machine |
| **Team original** | New characters, new music, maps painted from scratch | ✅ Under its own license |
| **Derived** | Scaled backgrounds, retextures, translations of the original text, new models of existing Digimon | ⚠️ Only as **recipes or deltas** applied on top of the user's base |

What must be clear:

- **A new model of an existing Digimon is still fan art:** the character's design belongs to its holders. The same
  goes for the mod as a whole. It is tolerated, but it is not entirely legal. The goal is to reduce the risk, not to
  eliminate it.
- **The engine is not a "clean room".** It is written with the decomp as a reference. For that reason nothing from the
  decomp is copied into the repository: neither code nor **data tables** (stats, evolutions, texts). All of that
  comes from the user's disc.
- **Rules yes, tables no.** The parameters of rules and mechanics (caps, thresholds, divisors, timers, probabilities)
  may be in git, in the rulesets and with their citation to the decomp; the per-entity tables (per Digimon, item,
  technique or map) come from the user's disc (executable and overlays) with `dwx`. "Decided by the coordinator
  (2026-10-21)". Detail and classification of each value: [ruleset audit](ruleset-audit.md).
- **Ids that come from the name of an item or a technique** (`bandage`, `giga_hand`) **and the maps' internal file
  codes** (`twna01`, `ROOM08`) may be used in rulesets, tests, fixtures and stand-ins; the franchise name and its
  logos may not (they never could), nor may the data tables (stats, effects, texts), and the data that accompanies a
  versioned id is invented, never copied from the disc or the decomp (user decision, 2026-10-21). A real id never goes
  at its original number: where a test needs an entry at that number, it uses an invented id. The categories and
  behaviors (sort category, item color, handler type, battle use flags, whether it can be thrown away) may match the
  original.
- **Species and character names are never ids in git.** The base pack that dwx generates on the user's machine
  carries the real names and never goes to git. In git, a species id only appears where a ruleset or the game pack
  names an entry of the base pack, and in the fixtures and stand-ins that those rulesets load, which need that id
  (with invented data and never at its original number). Every other test, fixture or documentation example uses
  neutral ids (`species_a`, `species_b`…), and no fixture carries the name of a species or a character in a visible
  name or a text.
- **Digimon from other games** can only arrive through an importer that reads that game's disc, with the same rule.

## Rules

1. **No money.** No sales, no ads, no donations tied to the mod.
2. **No trademarks.** The project name, the executable and the logo do not use the franchise name or its logos. The
   README carries a non-affiliation notice.
3. **No stores.** It is not published on Steam or in app stores.
4. **Nothing original in git.** No file extracted from the disc, no binary compiled with original code, no memory
   dump and no screenshots with original art inside the repositories.
5. **Separate repositories.** If a takedown request arrives against the content, the engine survives.

## Mechanisms

| Mechanism | What it does |
|---|---|
| **`provenance.yaml`** | Planned: each team asset will declare its author, its license and its origin (original or derived). There is none yet: the game pack only carries YAML, Lua, the interface, two flags, the Noto Sans fonts, with their license (`noto_sans_license.txt`), and two public domain pixel fonts for the new look (`pixel_fonts_license.txt`) |
| **Git hooks** (`scripts/dev.sh setup-hooks`) | `pre-commit` and `commit-msg` reject the original's paths (disc images, game files, savestates, traces), another author identity and the words forbidden in the message (in files they only warn) (`scripts/hooks/policy.sh`) |
| **Packages without disc data** | Before publishing, `scripts/package/no_disc_data.sh` (and its macOS version) checks that no package carries the base pack or original files. Each package carries `LICENSE`, `THIRD_PARTY_NOTICES.txt` and the third-party licenses |
| **Hash check in CI** | Planned: reject any file whose hash matches one extracted from the disc. Today the CI does not do it |
| **`.gitignore`** | `content/base/` and the cook caches are never versioned |
| **Recipes and deltas** | What is derived is distributed as instructions or differences applied on top of the local base |

## Enhanced output (remaster)

The rules of the [remaster pipeline](../architecture/remaster-pipeline.md#legal)
([ADR 0023](../adr/0023-remaster-pipeline.md)):

- **What is enhanced is derived.** A scaled background or a texture repainted from the disc is still the original.
  **Only the recipes** are distributed; never the outputs, the caches or the generated `remaster` packs.
- **By default, only what is built in:** with no configuration only the built-in upscaler works, with no network and
  bit for bit the same on all platforms. A local model with pinned weights (same weights hash, same seed, same
  parameters) is your own option and is reproducible; on another platform or GPU the result may vary a little.
- **The cloud is your own business.** A remote service is not reproducible and forces you to upload art extracted
  from your disc, something its terms may not allow you. It only works if you set it up with your own key and declare
  in your configuration that you have read its terms, and **each run** that uploads anything needs your explicit
  confirmation (`--confirm-upload`), even if the service is free. The project does not recommend it.
- **Weight licenses:** each recipe declares that of its weights; an unknown one is rejected unless you accept it.
- **Nobody accepts terms for you:** neither the tools nor the agents accept terms, store keys, or confirm spending or
  uploads on your behalf.
- **Keys** live in the environment or in the system credential store, never in git or in a recipe.
- **Music:** "HD music remade from the original is a derived work: only recipes or deltas are distributed. The
  original game's music is never distributed. New music composed by the team, or licensed, declares author, license
  and origin in provenance.yaml. Decided by the coordinator (2026-10-21)."

## Licenses

- **Code:** MIT (see [`LICENSE`](https://github.com/Indie-Modding-Games/dw-engine/blob/main/LICENSE)). Dependencies keep their own licenses.
- **Team original assets:** CC BY-SA 4.0.

See [ADR 0011](../adr/0011-asset-legal-policy.md).

## Pending in the current repositories

- `Digimon-World-Project-Folder-Assets`: remove `dist/windows/dw1.exe` (compiled game code) and
  `port/android/design/vram_menu.bin` (VRAM dump with original graphics). The decomp copy contains game data: it may
  stay in a private repository, but never move to a public one.
- The `Dialog.*` and `Game.*` generated from the discs (see `port/HANDOFF.md`) are original texts and must not go
  into git. The public `localization/` only has our own texts (`Port.*` and the rewritten save messages), so it is
  fine.
