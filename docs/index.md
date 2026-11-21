---
layout: home

hero:
  name: dwe
  text: Technical documentation
  tagline: A native C++26 engine that reimplements a classic raising RPG and serves as a base for mods.
  actions:
    - theme: brand
      text: Set up the environment
      link: /getting-started/setup
    - theme: alt
      text: Overview
      link: /architecture/overview
    - theme: alt
      text: How to contribute
      link: /contributing

features:
  - title: Architecture
    details: Layers, dependency rules, packs and the virtual file system, rendering, rulesets and saves.
    link: /architecture/overview
  - title: Modules
    details: What each module of the engine, the game and the apps does, and how to use its API.
    link: /modules/
  - title: Original formats
    details: How dwx reads the user's disc and which open formats it leaves each thing in.
    link: /formats/
  - title: Guides
    details: Concrete tasks, from the self-test routes and the training check to translation.
    link: /guides/vertical-slice
  - title: Releases and beta
    details: A test version for every merge that changes code, the stable one promoted by hand, and the launcher with its two channels.
    link: /modules/apps/launcher#publishing-a-release
---

This documentation is the technical source of truth for the project. It is treated like code: it is reviewed in PRs and
updated in the same PR that changes the code (see [How to contribute](contributing.md)).

The game design (GDD, story, characters) lives in the team's design repository. Only technical material goes here.

To get started: [Run the game](getting-started/play.md) · [Extensions and files](formats/extensions.md).

## Where to start by role

| Role | Read first |
|---|---|
| The whole team | [Overview](architecture/overview.md) · [Legal policy for assets](legal/assets.md) · [Original rules or tables](legal/ruleset-audit.md) · [Plan](roadmap.md) · [Glossary](glossary.md) |
| Programming | [Set up the environment](getting-started/setup.md) · [Repository structure](architecture/repository-layout.md) · [Dependency rules](architecture/dependency-rules.md) · [Modules](modules/index.md) · [Original formats and `dwx`](architecture/formats.md) · [Packs and the virtual file system](architecture/vfs.md) · [High definition](architecture/high-definition.md) · [World space](architecture/world-space.md) · [Compatibility with the team's tools](architecture/external-tools.md) · [JSON Schema](architecture/json-schema.md) · [Code conventions](conventions/code.md) · [Render](architecture/render.md) · [Models and animation](architecture/models.md) · [Rulesets](architecture/rulesets.md) · [Saved games](architecture/save.md) · [Controls](architecture/controls.md) |
| Art and audio | [Asset pipeline](architecture/assets-pipeline.md) · [Audio in the engine](architecture/audio.md) · [High definition](architecture/high-definition.md) · [Team tools](architecture/external-tools.md) · [Packs](architecture/vfs.md) · [Legal policy for assets](legal/assets.md) |
| Design and script | [Design cycle](conventions/design-cycle.md) · [Reference sheets](reference/index.md) ([care](reference/care.md), [evolution](reference/evolution.md), [battle](reference/battle.md)) · [Overview](architecture/overview.md) · [Asset pipeline](architecture/assets-pipeline.md) |
| Testing the beta and publishing | [Launcher](modules/apps/launcher.md) ([channels](modules/apps/launcher.md#update-channel), [publishing a version](modules/apps/launcher.md#publishing-a-release)) · [Beta gateway](modules/services/beta-gateway.md) · [Beta for Linux](guides/beta-linux.md) · [Beta for Windows and macOS](guides/beta-windows-macos.md) · [Custom images](guides/custom-images.md) |
| Anyone who wants to propose a big change | [How ADRs work](adr/index.md) |

## How it is organized

We follow the [Diátaxis](https://diataxis.fr/) framework, which separates documents by what their reader needs:

| Type | Purpose | Folder |
|---|---|---|
| Tutorial | Learn from scratch | [`getting-started/`](getting-started/setup.md) |
| How-to guide | Do a concrete task | `guides/`: the [asset viewer](guides/asset-viewer.md), the [vertical slice](guides/vertical-slice.md), [passing the gate](guides/gate.md), the [simulations](guides/simulations.md), the [state trace](guides/state-trace.md) for comparing with the original, the [training check](guides/check-training.md), the [remote control](guides/remote-control.md) of the running game, the [beta for Linux](guides/beta-linux.md), the [beta for Windows and macOS](guides/beta-windows-macos.md), the [launcher add-ons](guides/launcher-addons.md), the [custom images](guides/custom-images.md), [how evolutions work](guides/evolutions.md) and [translating the original's texts](guides/translate.md), with its glossaries: [Español](guides/spanish-glossary.md), [Deutsch](guides/de-glossary.md), [Français](guides/fr-glossary.md), [Português (Brasil)](guides/pt-glossary.md), [Русский](guides/ru-glossary.md), [简体中文](guides/zh-glossary.md), [한국어](guides/ko-glossary.md), [日本語](guides/ja-glossary.md) |
| Reference | Look up an exact detail | [`reference/`](reference/index.md) (the original and the [port coverage](reference/port-coverage.md)), [`packages/formats/`](formats/index.md) (the disc formats), [`modules/`](modules/index.md) (each module's API), `modding/` |
| Explanation | Understand why it is the way it is | [`architecture/`](architecture/overview.md), [`adr/`](adr/index.md) |

Also: `conventions/` (team rules: [code](conventions/code.md), [commits](conventions/commits.md),
[design cycle](conventions/design-cycle.md)), `legal/` (what can be published: [assets](legal/assets.md)),
[`roadmap.md`](roadmap.md) and [`glossary.md`](glossary.md).

## Rules

- All documentation lives in `docs/` and makes up this site (VitePress). Each module has its page in
  [`modules/`](modules/index.md); the README next to its code is short and links to it. The tools that are not part of
  the engine (`apps/studio`, `apps/beta-gateway`, `apps/versus-signal`, `apps/mapper`) keep a longer README with the
  steps to use or deploy them.
- Markdown, one idea per section and short sentences, in English. `docs/es/` has the Spanish translation, with the same
  file names and headings.
- Diagrams are written in [Mermaid](https://mermaid.js.org/), as text: the site and GitHub render them and they are reviewed
  in PRs.
- An accepted ADR is not rewritten: it is replaced with a new one.
- `scripts/dev.sh docs` builds the site and fails on a broken link; `scripts/dev.sh check-full` runs it, and a pull
  request that changes `docs/` builds it too (without publishing it). `scripts/dev.sh docs-dev` serves it locally while
  you write.
