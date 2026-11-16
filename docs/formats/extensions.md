# Game extensions and files

A Digimon or a map does not correspond to a single extension: it combines a model, images, animations and data documents. This guide tells apart the files on the original disc, the editable files that `dwx` extracts and the ones it prepares for distribution.

To start the project, see [Running the game](../getting-started/play.md). For the binary details of each format, use the links in the tables.

## Digimon and character models

| File | What it contains | How it is used |
|---|---|---|
| `.glb` | Binary glTF 2.0 model; it can include mesh, materials, textures, skeleton and animation clips | The extractor writes one model per file. The engine loads this format; it can be inspected and edited with glTF-compatible tools, such as Blender. |
| `texture.png` | Extracted texture of the character | Editable image. If it uses palettes, it needs to keep its associated metadata. |
| `texture_meta.yaml` | Image mode, original location and palettes | It lets you interpret the indexed PNG and choose the right palette; it is not another texture. |
| `contract.yaml` | Model contract: structure and required animations | It is used to validate replacements and added species; it does not contain the mesh. |
| `models/<character>.yaml` | Reference to the model and mapping between actions and clips | It says which animation the character uses when standing, walking or running. |
| `data/digimon.yaml` | Species table and references to other data | It describes species attributes; it does not replace the model nor does it contain all the raising logic by itself. |

Example of how extracted content is organized:

```text
contenido/
└── base/
    ├── pack.yaml
    └── digimon/
        └── agum/
            ├── model.glb
            ├── texture.png
            ├── texture_meta.yaml
            └── contract.yaml
```

The short names are content identifiers, not special extensions. In the current format, the model's animations travel inside the `.glb`; there is no separate `.anim` file to look for. The current cooker validates and keeps the `.glb`, without converting it to a supposed mesh extension of its own.

Details: [the original's models](models.md) and [models and animation in the engine](../architecture/models.md).

## Maps and scenes

| File | What it contains | What is changed there |
|---|---|---|
| `map/<id>/data.yaml` | Camera, lights, entities, sprites, connections and collision configuration of the map, according to the schema | The spatial and presentation data. Story and rules can reference additional documents or scripts. |
| `background_day.png`, `background_dusk.png`, `background_night.png` | Prerendered backgrounds for different times of day | The look of the scene. Changing the image does not automatically change the collision, the steps or the camera. |
| `texture4/*.png`, `texture8/*.png` | Indexed textures of map elements | Their images; the suffixes 4 and 8 indicate the index depth of the original. |
| `*_meta.yaml` | Palettes and metadata of the images | It keeps the information needed to reproduce their colors and variants. |
| Other `.yaml` documents and `.lua` scripts | Rules, encounters, events or shared references | The map's behavior is composed from these systems; not everything lives in `data.yaml`. |

For example, `map/twna01/data.yaml` is a document and `map/twna01/background_day.png` is an image. A map YAML file is not a Blender 3D scene or a background image. See [Maps](maps.md) for the format and its sections.

## Other editable and runtime files

| Extension or name | Use |
|---|---|
| `.yaml` | Tables, rules, configuration, translations and manifests. The structure depends on the document, not only on the extension. |
| `pack.yaml` | Identity, version, dependencies, priority and schemas of a package. |
| `.schema.json` | JSON Schema to describe and validate data documents; it is not a save file. |
| `.lua` | Scripts for logic, events and UI behavior. They are loaded as text; the pipeline does not distribute Lua bytecode as a substitute. |
| `.rml` | RmlUi menu and HUD structure. It looks like HTML, but belongs to the engine's UI system. |
| `.rcss` | RmlUi styles: geometry, colors, typography and states. Its compatibility is not identical to that of a browser's CSS. |
| `.png` | Textures, backgrounds, icons or font atlases. An indexed PNG may need `*_meta.yaml`. |
| `.ttf` | Typeface loaded through FreeType. The classic font uses its own atlas and metrics extracted locally. |
| `.sf2` | SoundFont bank with instruments and samples to synthesize music. |
| `.mid` | MIDI sequence: notes and events; it needs the matching instruments to sound. |
| `.wav` | Extracted PCM audio, for example effects or the movies' audio. |
| `.y4m` | Extracted YUV4MPEG2 video. The movie comes with WAV audio and a YAML descriptor; it is not an MP4. |
| `.slang` | Shader source code that is compiled for the GPU. It is part of renderer development. |
| `.cpp`, `.hpp` | C++ implementation and headers of the engine and its tools. They are not content you edit to change a texture or a dialogue. |

Saves are also YAML documents, but they have their own version contract and are stored in the player's data location. They are not tables of the `base` package. See [saves](../architecture/save.md), [UI](../architecture/ui.md), [localization](../architecture/localization.md) and [audio](audio.md).

## Cooked files and packages

"Cooking" means preparing content to be loaded or distributed, keeping the source files for future edits.

| Source | Current output | Note |
|---|---|---|
| `.png` | `.ktx2`, when the texture plan allows converting it | GPU-ready texture. Images that need palette selection at runtime keep their PNG and metadata. |
| `.glb` | Validated `.glb` | Keeps the binary glTF format. |
| Sound `.wav` | `.opus` | Audio compressed in an Ogg container. A movie's WAV is kept with its video. |
| YAML, Lua and other files without a specific converter | Copy of the file | They are not automatically converted into a binary of their own. |
| Package folder | `.pak`, if packed | A file that gathers assets and manifest; it is not an executable nor a format of the original disc. |

Details: [KTX2](ktx2.md), [Opus](opus.md), [PAK](pak.md) and the [asset pipeline](../architecture/assets-pipeline.md). To edit, go back to the sources; do not modify caches or generated outputs as if they were the project's source.

## Original disc extensions

These are extractor inputs, not the new engine's editable formats.

| Format | Meaning | Reference |
|---|---|---|
| `.cue` + `.bin` | Track sheet and disc data. The CUE references its BIN; they must be kept together. | [Disc](disc.md) |
| `.iso` | Image of data sectors. It may lack the audio/video information needed to extract complete movies. | [Disc](disc.md), [video](video.md) |
| `.tmd` | Model in the original graphics format. | [Models](models.md) |
| `.mmd` | The game's model and animation package. | [Models](models.md) |
| `.tim` | Original image, often indexed and with CLUT palettes. | [TIM](tim.md) |
| `.map` | Camera, lights, images, entities and collision data of the original scene. | [Maps](maps.md) |
| `.tfs` | Data of the original prerendered background. | [Maps](maps.md) |
| `.scn` | Event data and bytecode, like `MAPHEAD.SCN` and `DG.SCN`. It is not a Lua script. | [Scripts](scripts.md) |
| `.vhb`, VAB and SEQ | Banks and sequences of the original sound system; some are grouped inside other files. | [Audio](audio.md) |
| `.str` | Movie with interleaved MDEC video and XA audio. | [Video](video.md) |
| `.efe` | Effect and technique data of the original. | [Effects](effects.md) |
| `*_REL.BIN` | Code and data overlay loaded by the original game. It is not a native library of the engine. | [Overlays](overlays.md) |

The same `.bin` extension can name a disc image or a different internal file. Some regional executables do not end in `.exe` either. The extractor identifies structures and versions; renaming the extension does not turn one format into another.

## Paths and identifiers

The engine usually references an asset **without an extension**: `digimon/agum/model` or `map/twna01/data`. The virtual file system resolves the file from its package, for example `model.glb` or `data.yaml`. This lets an identifier stay the same even if its representation changes when the content is cooked.

Not all YAMLs are interchangeable: `pack.yaml`, `contract.yaml`, a map, a translation and a save have different responsibilities and schemas. See the [VFS](../architecture/vfs.md) and the [schemas](../architecture/json-schema.md) before changing their structure.
