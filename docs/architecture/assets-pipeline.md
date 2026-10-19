# Tubería de assets

Cómo pasa el contenido del disco original a algo que el equipo puede editar y que el motor carga. La decisión está en
el [ADR 0005](../adr/0005-tuberia-de-assets.md) y las reglas de qué se puede publicar en la
[política legal](../legal/assets.md).

## Tres estados

```mermaid
flowchart LR
    original[(Original<br/>disco del usuario)] -->|dwx extract| source[Fuente<br/>glTF, PNG, YAML, Lua…]
    team[Equipo y modders] -->|editan| source
    source -->|dwx cook| cooked[Cocinado<br/>KTX2, mallas, Opus, .pak]
    cooked -->|carga| game[El juego]
    source -.->|desarrollo: carga directa<br/>con recarga en caliente| game
```

| Estado | Qué es | Dónde vive |
|---|---|---|
| **Original** | El disco de cada usuario. Solo se lee | En su máquina |
| **Fuente** | Formatos abiertos que se abren con Blender, Krita o un editor de texto. Es lo que se edita | La base, en la máquina de cada usuario; lo del equipo, en los repositorios |
| **Cocinado** | Optimizado para la GPU y para cargar rápido | Caché local y releases del equipo |

La extracción **solo va en un sentido**. No reconstruimos el disco ni buscamos que el resultado sea igual byte a byte.
El criterio de calidad es que cada fichero del disco se extraiga y el motor cargue el resultado, con un informe de
cobertura que lista lo que falte.

## Qué pasa con cada tipo

| Tipo | Original | Fuente | Cocinado |
|---|---|---|---|
| Modelos y animaciones | TMD/MMD | glTF 2.0 (malla, esqueleto y animaciones) | Malla binaria + paquete de animaciones |
| Texturas | TIM con paletas de 4/8 bits | PNG, con la paleta guardada como metadato | KTX2 (hoy RGBA8 sin comprimir; BC7 o Basis cuando se apruebe) |
| Fondos de mapa | Imagen prerenderizada de día, atardecer y noche | PNG por capa + máscara de profundidad | KTX2 |
| Datos de mapa | Colisión, warps, NPCs, cámara | YAML | Binario |
| Tablas del juego | Datos del ejecutable: stats, evoluciones, técnicas, objetos | YAML; siembran el primer ruleset | Binario |
| Textos | Por región, en su codificación | UTF-8, un fichero por idioma | Igual |
| Eventos e historia | Bytecode de scripts | Lua | Bytecode de Lua |
| Música | VAB + SEQ | SF2 + MIDI | Igual, u Opus si es un remaster |
| Efectos de sonido | VAB | WAV/FLAC | Opus |
| Vídeos | STR (MDEC) | AV1/WebM | Igual |
| Fuentes tipográficas | Bitmap | PNG + métricas, o TTF para idiomas nuevos | Atlas de glifos |

## Capas de contenido

El motor monta un sistema de ficheros virtual con capas apiladas. Cada capa es un **paquete** con su manifiesto
(id, versión, dependencias y prioridad). Si dos capas definen el mismo asset, gana la de arriba. Los detalles
(manifiesto, IDs, orden de la pila y errores) están en [Paquetes y sistema de ficheros virtual](vfs.md).

| Prioridad | Capa | Quién la hace |
|---|---|---|
| 4 (gana) | Usuario | El propio jugador: sus ajustes |
| 3 | Mods | El equipo o la comunidad: historia, Digimon y modos nuevos |
| 2 | Remaster | El equipo: texturas, fondos y música en HD |
| 1 | Base | El extractor, a partir del disco del usuario |

- **IDs estables por nombre:** `digimon/agumon/model`, `map/file_city_01/background`. Nunca índices numéricos. Un
  paquete **sobrescribe** un ID que ya existe o **añade** uno nuevo, y por eso no hay límite de Digimon ni de mapas.
- **Clásico/HD por categoría:** en los ajustes se elige, por ejemplo, fondos HD con música original. Si falta el
  asset HD, se usa el de la base.

## Cómo se mejora cada cosa

- **Texturas:** se escalan o se repintan a más resolución con las mismas UV.
- **Modelos:** se pueden sustituir por otros con más detalle si cumplen el **contrato del Digimon**: un esqueleto
  compatible y la lista completa de animaciones que el juego necesita (reposo, andar, atacar, comer, dormir…), o sus
  propias animaciones si las trae. `dwx validate` lo comprueba.
- **Fondos:** se escala el original, se amplían los bordes para panorámico o se recrea la escena en 3D y se renderiza
  con la cámara exportada. La máscara de profundidad tiene que coincidir con la colisión.
- **Música:** un tema remasterizado sobrescribe el original por ID.

## Herramienta `dwx`

| Comando | Qué hace |
|---|---|
| `dwx extract <disco>` | Comprueba el SHA-1 del disco y genera la base en formato fuente |
| `dwx validate <paquete>` | Esquemas, referencias rotas y contratos (por ejemplo, las animaciones de cada Digimon) |
| `dwx cook <paquete> -o <carpeta>` | Fuente → cocinado, en otra carpeta |
| `dwx pack <paquete> -o <fichero.pak>` | Genera un paquete distribuible en un solo fichero, con su manifiesto |

La primera vez que se abre el juego, el launcher ejecuta `extract` y `cook` sobre el disco del usuario y guarda el
resultado en caché.

## Cocinar

`dwx cook` lee un paquete en formato fuente y escribe otro con el mismo `pack.yaml` y los mismos IDs, pero cada
fichero pasado por el **cocinero de su extensión**. Lo decide el [ADR 0017](../adr/0017-cocinado-y-archivos-pak.md).

| Fuente | Cocinado | Cocinero |
|---|---|---|
| PNG | KTX2 RGBA8 sin comprimir, con todos los mipmaps, sRGB o lineal ([KTX2](../formats/ktx2.md)) | `texture` |
| GLB | El mismo GLB, con la cabecera comprobada | `glb` |
| Cualquier otro (YAML, WAV, MIDI, SF2…) | Copiado tal cual | — |

- Un tipo nuevo es una entrada más en el registro (`apps/dwx/src/cook.cpp`) y la función que cocina.
- **Caché:** la carpeta de salida guarda en `.dwx-cook` (oculto, así no es un asset), por cada fichero, el SHA-1 de
  su fuente, el cocinero con su versión y el SHA-1 de lo que escribió. Al volver a cocinar en la misma carpeta, lo que
  no cambió se deja como está, lo que cambió (o se tocó a mano) se cocina otra vez y lo que ya no tiene fuente se
  borra. Dos assets con los mismos bytes se cocinan una sola vez.
- **Determinista:** el mismo paquete da los mismos bytes, con caché o sin ella.
- Se niega a escribir en una carpeta con ficheros que no escribió `dwx cook`, y en una que esté dentro del paquete o
  lo contenga.
- Lo que falta, porque necesita dependencias que el equipo tiene que aprobar: supercompresión Basis o BC7 para las
  texturas y Opus para el audio. Están en las propuestas del ADR 0017.

## Empaquetar

`dwx pack` guarda un paquete (fuente o cocinado) en un [archivo `.pak`](../formats/pak.md): el `pack.yaml` y todos
sus ficheros, ordenados por ID, con el SHA-1 de cada uno. No comprime todavía. Al terminar abre el archivo y lee cada
entrada para comprobar que coincide con la carpeta.

El motor monta un `.pak` igual que una carpeta (`PakFileSystem`), así que en una misma pila puede haber paquetes de
las dos clases: por ejemplo, la base en `.pak` y un mod en su carpeta mientras se edita.

```text
dwx extract disco.cue --out content/base
dwx cook content/base -o content/cooked
dwx pack content/cooked -o base.pak
```

## Dónde vive cada cosa

| Qué | Dónde |
|---|---|
| Base extraída (`content/base/`) y cocinada (`content/cooked/`) | Solo en la máquina de cada usuario; están en `.gitignore` |
| Datos y Lua creados por el equipo | `content/packs/` en este repositorio |
| Arte fuente del equipo (`.blend`, `.kra`, glTF, audio) | Repositorio de assets con Git LFS |
| Fixtures para tests y CI | `content/fixtures/`, hechos por el equipo |
