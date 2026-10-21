# Política legal de assets

> Esto no es asesoría legal. Es la política del proyecto para minimizar el riesgo. Las leyes cambian según el país.
> Si la situación del proyecto cambia (entra dinero, crece mucho la difusión), hay que consultar a un abogado de
> propiedad intelectual.

## Qué es este proyecto

Un proyecto de fans, **sin ánimo de lucro**, hecho por gusto, para volver a jugar el juego con reinterpretaciones
propias. No compite con los titulares de la licencia ni pretende sustituir sus productos. Si un titular de derechos
pide retirar algo, **se retira**.

## Qué protege la ley

- **Copyright:** el código del juego, el arte, los modelos, la música, los textos y el diseño de los personajes.
- **Marcas:** el nombre de la franquicia y sus logos.

## El modelo: "trae tus propios datos"

Es el mismo modelo que usan proyectos como OpenMW, OpenRCT2, ScummVM o Ship of Harkinian. El proyecto publica un motor
que **no contiene nada del original**, y cada usuario aporta su propia copia del juego. La extracción ocurre en su
máquina.

## Qué se puede distribuir

| Categoría | Ejemplos | ¿Se distribuye? |
|---|---|---|
| **Motor** | `engine/`, `game/`, herramientas | ✅ Con la licencia del proyecto |
| **Base** | Modelos, fondos, música, textos y tablas de stats del disco | ❌ Nunca. Se genera en la máquina del usuario |
| **Original del equipo** | Personajes nuevos, música nueva, mapas pintados desde cero | ✅ Con licencia propia |
| **Derivado** | Fondos escalados, retexturas, traducciones del texto original, modelos nuevos de Digimon existentes | ⚠️ Solo como **recetas o deltas** que se aplican sobre la base del usuario |

Lo que hay que tener claro:

- **Un modelo nuevo de un Digimon existente sigue siendo fan art:** el diseño del personaje pertenece a sus titulares.
  Lo mismo vale para el mod en su conjunto. Se tolera, pero no es del todo legal. El objetivo es reducir el riesgo,
  no eliminarlo.
- **El motor no es un "clean room".** Se escribe con el decomp como referencia. Por eso no se copia nada del decomp al
  repositorio: ni código ni **tablas de datos** (stats, evoluciones, textos). Todo eso sale del disco del usuario.
- **Reglas sí, tablas no.** Los parámetros de reglas y mecánicas (topes, umbrales, divisores, temporizadores,
  probabilidades) pueden estar en git, en los rulesets y con su cita al decomp; las tablas por entidad (por Digimon,
  objeto, técnica o mapa) salen del disco del usuario (ejecutable y overlays) con `dwx`. «Decidido por el coordinador
  (2026-10-21)». Detalle y clasificación de cada valor: [auditoría de los rulesets](auditoria-rulesets.md).
- **Los ids que vienen del nombre de un objeto o una técnica** (`bandage`, `giga_hand`) **y los códigos internos de
  archivo de los mapas** (`twna01`, `ROOM08`) se pueden usar en rulesets, tests, fixtures y sustitutos; el nombre de la
  franquicia y sus logos no (nunca se pudieron), ni las tablas de datos (stats, efectos, textos), y los datos que
  acompañan a un id versionado son inventados, nunca copiados del disco ni del decomp (decisión del usuario,
  2026-10-21). Un id real nunca va en su número del original: donde un test necesita una entrada en ese número, usa un
  id inventado. Las categorías y los comportamientos (categoría de orden, color del objeto, tipo de manejador, banderas
  de uso en combate, si se puede tirar) pueden coincidir con el original.
- **Los nombres de especies y de personajes nunca son ids en git.** El paquete base que dwx genera en la máquina del
  usuario lleva los nombres reales y nunca va a git. En git, un id de especie solo aparece donde un ruleset o el
  paquete del juego nombra una entrada del paquete base, y en los fixtures y sustitutos que cargan esos rulesets, que
  necesitan ese id (con datos inventados y nunca en su número del original). Todo otro test, fixture o ejemplo de la
  documentación usa ids neutros (`species_a`, `species_b`…), y ningún fixture lleva el nombre de una especie o un
  personaje en un nombre visible o un texto.
- **Los Digimon de otros juegos** solo pueden llegar a través de un importador que lea el disco de ese juego, con la
  misma regla.

## Reglas

1. **Sin dinero.** Ni venta, ni anuncios, ni donaciones ligadas al mod.
2. **Sin marcas.** El nombre del proyecto, el ejecutable y el logo no usan el nombre de la franquicia ni sus logos.
   El README lleva un aviso de no afiliación.
3. **Sin tiendas.** No se publica en Steam ni en tiendas de aplicaciones.
4. **Nada original en git.** Ningún fichero extraído del disco, ningún binario compilado con código original, ningún
   volcado de memoria ni capturas de pantalla con arte original dentro de los repositorios.
5. **Repositorios separados.** Si llega una petición de retirada contra el contenido, el motor sobrevive.

## Mecanismos

| Mecanismo | Qué hace |
|---|---|
| **DCO** (`git commit -s`) | El autor certifica que tiene derecho a aportar ese código o ese asset |
| **`provenance.yaml`** | Cada asset del equipo declara su autor, su licencia y su origen (original o derivado) |
| **Chequeo de hashes en la CI** | Rechaza cualquier fichero cuyo hash coincida con uno extraído del disco |
| **`.gitignore`** | `content/base/` y las cachés de cocinado nunca se versionan |
| **Recetas y deltas** | Lo derivado se distribuye como instrucciones o diferencias que se aplican sobre la base local |

## Salida mejorada (remaster)

Las reglas de la [tubería del remaster](../architecture/remaster-pipeline.md#legal)
([ADR 0023](../adr/0023-tuberia-del-remaster.md)):

- **Lo mejorado es derivado.** Un fondo escalado o una textura repintada a partir del disco siguen siendo el
  original. Se distribuyen **solo las recetas**; nunca las salidas, las cachés ni los paquetes `remaster` generados.
- **Por defecto, solo lo integrado:** sin configuración solo funciona el escalador integrado, sin red y bit a bit
  igual en todas las plataformas. Un modelo local con pesos fijados (mismo hash de los pesos, misma semilla, mismos
  parámetros) es una opción tuya y es reproducible; en otra plataforma o GPU el resultado puede variar un poco.
- **La nube es cosa tuya.** Un servicio remoto no es reproducible y obliga a subir arte extraído de tu disco, algo que
  sus términos pueden no permitirte. Solo funciona si tú lo configuras con tu propia clave y declaras en tu
  configuración que has leído sus términos, y **cada ejecución** que suba algo necesita tu confirmación explícita
  (`--confirm-upload`), aunque el servicio sea gratis. El proyecto no lo recomienda.
- **Licencias de los pesos:** cada receta declara la de sus pesos; una desconocida se rechaza salvo que tú la aceptes.
- **Nadie acepta términos por ti:** ni las herramientas ni los agentes aceptan términos, guardan claves ni confirman
  gastos ni subidas en tu nombre.
- **Las claves** viven en el entorno o en el almacén de credenciales del sistema, nunca en git ni en una receta.
- **Música:** «La música HD rehecha a partir de la original es obra derivada: solo se distribuyen recetas o deltas. La música del juego original nunca se distribuye. La música nueva compuesta por el equipo, o con licencia, declara autor, licencia y origen en provenance.yaml. Decidido por el coordinador (2026-10-21).»

## Licencias propuestas

- **Código:** por decidir entre MIT (máxima libertad) y GPL-3.0 (obliga a que los forks sigan abiertos).
- **Assets originales del equipo:** CC BY-SA 4.0.

Ver [ADR 0011](../adr/0011-politica-legal-de-assets.md).

## Pendiente en los repositorios actuales

- `Digimon-World-Project-Folder-Assets`: sacar `dist/windows/dw1.exe` (código del juego compilado) y
  `port/android/design/vram_menu.bin` (volcado de VRAM con gráficos originales). La copia del decomp contiene datos
  del juego: puede quedarse en un repositorio privado, pero nunca pasar a uno público.
- Los `Dialog.*` y `Game.*` generados desde los discos (ver `port/HANDOFF.md`) son textos originales y no deben
  entrar en git. El `localization/` público solo tiene textos propios (`Port.*` y los mensajes de guardado
  reescritos), así que está bien.
