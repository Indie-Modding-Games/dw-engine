# 0023. Remaster by recipes, with interchangeable providers, content cache and local by default

- **Status:** Accepted
- **Date:** 2026-10-21
- **Note (2026-11-06):** accepted because the code already follows it: `dwx upscale <recipe.yaml>` applies the recipes.

## Context

Phase 4 includes high-resolution backgrounds and textures in the `remaster` layer. The user wants a parameterizable
chain (extract, adapt, enhance with any tool, cook) in which the disc is a dependency and the providers are
interchangeable: a local command or an image service in the cloud from any company.

Conditions: the enhanced output is derived from the original and cannot be distributed
([legal policy](../legal/assets.md)); there are hundreds of backgrounds and thousands of textures, so repeating calls
costs time and money; the backgrounds are indexes with a palette that the game chooses at play time and their depth
layers have to keep matching; `remaster` is an engine layer, and the code of its packs runs like the engine. The full
design is in [Remaster pipeline](../architecture/remaster-pipeline.md).

## Decision

- **Recipes, not outputs.** One YAML recipe per asset type describes a chain of steps (provider, model, parameters,
  seed, prompt). Recipes can be distributed; the outputs, the caches and the generated `remaster` packs, never. A
  recipe contains no commands, URLs, paths or keys: it names providers that each user defines in their
  `enhance-providers.yaml`.
- **A provider interface** (`ImageProvider`) with three classes: a local command with `{in}`/`{out}` templates without
  a shell, a configurable generic HTTP one without service names in the code, and built-in deterministic steps with
  integer arithmetic.
- **By default, only built-ins:** without configuration only the built-in steps exist, and the upscaler is
  `index_scale`; the repository's example recipes only use built-in steps. A local model with weights pinned by
  SHA-256 (mandatory) is a user option and the recommended enhancement. A remote service is an option of each user,
  with their own key and their own responsibility, which requires that they themselves declare having read the terms
  and that it uploads extracted art, and that they confirm uploads on **every run** (`--confirm-upload`), even if it is
  free. Weight licenses are declared with SPDX and an unknown one is rejected unless the user accepts it in their
  configuration. Nothing accepts terms on the user's behalf.
- **Mandatory cache by content and by step**, with key `<sha1>-v<EnhanceCacheVersion>` over the step's identity (with
  the command template, or the id and version of the model that the recipe configures and the HTTP request template,
  never the version that the service reports), its parameters, the tiling and padding limits and the canonical hash of
  its inputs, system advisory locks, entries written in `.partial-` folders and renamed, fingerprints that only grow
  for the built-in steps and paid outputs that are never deleted on their own.
- **Cost visible before spending:** a plan with calls, tiles, retries, pixels, cache hits and estimated cost; without
  a `--confirm-cost` equal to or greater than the worst case there are no paid calls; caps per run and per month;
  pessimistic reservations in a ledger with a lock that fails closed. An HTTP provider declares `pricing` or
  `paid: false`.
- **Bounded providers:** each provider declares its parameters with type and range (texts, with a list or with length
  and pattern); no value starts with `-`; substitution is single-pass. The HTTP transport only uses `https` (and
  `http` to loopback), with no redirects to another origin, no decompression and with size caps. Child processes have
  a time limit, an output cap, tree kill and no `DWE_ENHANCE_*` variable.
- **Secrets only in the environment or in the system store**, by reference, redacted in all output; `dwx validate`
  rejects recipes and configurations with anything shaped like a key.
- **Integer, uniform scale declared in `_meta.yaml` (`scale`)**; geometry always in logical units. The palette variants
  of a background are derived from an enhanced variant (`reference_transfer`), so the fade between aligned images
  gives the same as the palette fade; the depth layers are cut out of the enhanced background.
- **Cooking without palette losses:** the maps' backgrounds and pages always go through as PNG by their ID
  (`assets::PngOnlyImages`, a prerequisite on `main`, branch `p4-cook-maps`); W10b extends that list and the content
  check so that an image whose `_meta.yaml` declares `scale` and `color_space` without `palettes` is RGBA and can be
  KTX2; each indexed `_meta.yaml` of the remaster copies the base's sidecar fields and adds `scale`; only RGBA becomes
  KTX2, with `color_space: srgb` and UNORM textures in the map so the fade matches the palettes.
- **The remaster carries only data.** What `dwe-enhance` writes are images, their `_meta.yaml` and texture `.glb`
  files, in the content folder (origin `Engine`), and mounting rejects a `layer: remaster` pack with Lua, RML, RCSS or
  tables (it does accept audio; decision 10), because that layer runs with the engine's trust. It is generated by a
  separate executable, `dwe-enhance` (`apps/enhance`), owner of the providers and of all the network code; `dwx` still
  has no network and checks recipes and remaster packs with `dwx validate`, with the rules in a library without
  network (`apps/remaster_rules`). The review is a panel of the game's debug overlay that calls
  `dwe-enhance review`.

### Manager decisions

The design's open questions, "Decided by the manager, 2026-10-21":

1. **Backgrounds with `reference_transfer` by default:** one call per map and a clean fade between aligned variants.
2. **The viewer is an overlay panel in the development views**, not a new app: it reuses render, VFS and the real
   camera, and phase 4 has no `apps/editor`.
3. **The cache goes in `%LOCALAPPDATA%` on Windows and in `$XDG_CACHE_HOME` on Linux:** several GB must not travel
   with the profile or get mixed with the data; the spending ledger does go in the data folder.
4. **A separate executable, `dwe-enhance`, and `dwx` without network:** less attack surface, `dwx` stays
   deterministic and offline, and keys never reach the tool that modders run in their CI. **It departs from the user's
   original wording**, which spoke of `dwx enhance --recipe`. `dwx` does not offer `dwx enhance --plan`.
5. **The palette of the sprites with `clut` −1 is copied into the RGBA background's `_meta.yaml`:** the remaster is
   self-sufficient and `dwx extract` does not change.
6. **The only new ID a remaster can add is `<page id>_<variant>`:** the remaster replaces; it only adds what the
   per-variant cut needs.
7. **Secrets: environment first (`DWE_ENHANCE_*`) in W10d;** the system store (libsecret, Credential Manager) only if
   its vcpkg port builds in our Docker toolchain without installing anything on the host; if not, it is later work.
8. **Full mipmap chain in the atlases:** no regression compared with the base and no change to cooking.
9. **Memory warning per map from 128 MiB in BC7**, as a named constant (`RemasterMapTextureBudgetBytes`): `k = 4`
   fits with three variants.
10. **Mounting accepts audio in the `remaster` layer**, in addition to images, `_meta.yaml` and `.glb`, and keeps
    rejecting Lua, RML, RCSS and tables: HD music goes in that layer according to the asset pipeline, and audio is
    data, not code. Only in the cooked or extracted formats that the engine already accepts, with no new decoders. On
    mounting, only extension, size and header are checked; full decoding is left to `dwx validate` and
    `--check-content`. Caps per format as constants in `packages/engine/assets`: Opus/Ogg 64 MiB, WAV 128 MiB, SF2
    128 MiB and MIDI 1 MiB, each with its own mounting test; no total cap per pack for now. The future SF2 and MIDI
    readers are designed for untrusted input.
11. **HD music goes in its own pack, `remaster_audio`** (`layer: remaster`), not in the `dwe-enhance` one: that pack
    rejects files it did not write and deletes what it no longer produces.
12. **The output cap of a local command keeps the first N bytes and discards the rest, without killing the tool:** a
    lot of progress output is not a failure and draining the pipe avoids deadlocks. It is only killed on the time
    limit, and with `version_command` (whose output goes into the key) also when passing the cap.
13. **The UI's image limits move to `packages/engine/assets` as a prerequisite of W10b:** `remaster_rules` is born in
    W10b and needs them without depending on `ui`.

## Consequences

- Sharing a reproducible recipe amounts to sharing the result without distributing anything derived.
- No downloaded recipe can run programs or send art elsewhere.
- A second run, or a change in a step after the paid one, costs nothing.
- The engine changes additively: RGBA backgrounds with per-image fade, `scale` in `_meta`, sprite pages per variant,
  `pixelsPerTexel` per texture, mipmapped sampling in the map's RGBA path, rejection on mount of what is not data in
  `remaster`, and logical size in the UI. The image limits of `packages/engine/ui` move to `packages/engine/assets`
  before W10b. `runProcess` gains a time limit, an output cap, tree kill and an explicit environment: it is a change on
  `main` prior to W10b, like moving the indexed backgrounds to PNG when cooking.
- A remote service asks for confirmation on every run that uploads something; it cannot be left accepted. A loopback
  endpoint is treated as local: if it is a tunnel to the cloud, that is the user's responsibility.
- HD music is a derived work: only recipes or deltas are distributed, and it goes in its own `remaster_audio` pack
  (coordinator's decision, 2026-10-21, in the [legal policy](../legal/assets.md#enhanced-output-remaster)).
- `dwx` links nothing network-related: it only gains `apps/remaster_rules`. libcurl and the credential APIs only enter
  `dwe-enhance`. The [dependencies](../architecture/dependency-rules.md) table must be updated.
- There is one more executable to distribute and that the game looks for (like `dwx`) for the review panel.
- The cache can take up several GB; paid outputs are deleted only by hand.
- The derived variants are an approximation where the palette change is not smooth; whoever wants exact colors uses
  `per_variant`.

## Alternatives rejected

- **Distribute already generated `remaster` packs:** that is distributing a derived work; the legal policy forbids it.
- **Commands and endpoints inside the recipe:** a shared recipe could run code or exfiltrate art.
- **A cloud service by default:** it is not reproducible, it costs money and forces uploading extracted art.
- **Cache key per whole recipe:** changing the last step would repeat (and charge for) all the previous ones.
- **Re-quantize the output to the original palette:** it loses almost all the gain from the enhancement.
- **Enhance each palette variant separately as the rule:** it triples the calls and, with generative models, breaks
  the fade between times of day.
- **Scale factor in a KTX2 key of its own:** it is not standard and `_meta.yaml` already decides the cooking options.
- **A `packages/engine/enhance` module:** the runtime does not need it; network and credentials must not enter the
  game.
- **`dwx enhance` inside `dwx`:** it was the original wording; it brought network and keys into the tool that modders
  run in their CI.
