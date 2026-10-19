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
