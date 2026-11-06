# Comprobar el entrenamiento

Los cursos del gimnasio se rompen sin que se note en una prueba corta: basta con que un cambio en la animación del
campo haga que todos los clips muevan al actor, o que un curso empuje al partner además de su clip, para que el
partner atraviese la roca o corra el doble. La comprobación del entrenamiento mira cada curso de cada gimnasio en
dos niveles.

## Cuándo es obligatoria

Antes de cada PR que toque el gimnasio (`game/gym`, la pantalla del gimnasio, las reglas `gym.yaml`), la animación
del campo, el movimiento de raíz de los clips (`clipRootStep`, `applyClipRootMotion`, la marca de sujetar en el
sitio) o las reglas del entrenamiento. Las dos partes tienen que pasar; el resultado de la segunda va en la
descripción del PR.

## Nivel 1: pruebas unitarias (CI)

`apps/session/tests/gym_course_tests.cpp` juega una sesión entera de cada curso sobre un campo de verdad con datos
inventados: cada clip del partner avanza 10 unidades por cuadro, así que un curso que no lo sujete donde el original
lo sujeta, o que lo mueva además de su clip, se sale de sus límites. Para cada curso comprueba:

- que la sesión acaba y dura los cuadros del original;
- la distancia más lejana del partner a su sitio y el camino que recorre, dentro del límite del original;
- que el curso sube su estadística y ninguna baja.

Una prueba más compara la lista de cursos del paquete con la de las pruebas: un curso nuevo necesita su prueba y su
ruta. Corren con el resto de pruebas de `dwe_session_tests`, en el CI de cada PR, sin disco:

```sh
scripts/dev.sh build && scripts/dev.sh test                                  # todas, como el CI
build/ci/apps/session/dwe_session_tests --test-case='training check*'        # solo estas
```

## Nivel 2: el contenido real (local)

Necesita una carpeta de contenido extraída con `dwx`. Cada ruta de `tests/autotest/training` empieza una partida
nueva en el mapa del gimnasio, abre un curso con `gym("<curso>")`, juega la sesión entera y comprueba con
`expect_range` los ajustes que publica la pantalla del gimnasio al dar las ganancias:

| Ajuste | Qué es |
|---|---|
| `gym.session.frames` | cuadros de la sesión, desde que el partner llega a su sitio hasta las ganancias |
| `gym.session.max_offset` | lo más lejos que llega el partner de su sitio, en unidades del original |
| `gym.session.path` | el suelo que recorre (sin contar las vueltas de la carrera, que lo recolocan) |
| `gym.session.gain.<stat>` | lo que sube cada estadística: `hp`, `mp`, `offense`, `defense`, `speed`, `brain` |

```sh
scripts/dev.sh training-check --content <carpeta>          # todas
scripts/dev.sh training-check --content <carpeta> --only 'trai00-*'
```

Cada curso corre sin ventana en un Xvfb propio, cuatro a la vez (`TRAINING_JOBS`), sin la capa de validación. Al
acabar escribe una tabla con el resultado, el tiempo y la línea `gym: session` de cada curso, y el total; sale con 1
si alguno falla. Los registros de cada curso quedan en `build/training-check/<curso>/run.log`.

Tarda unos 12 minutos (11 min 44 s el 2026-11-06): la defensa de icsa03 sola juega 11 minutos y empieza la primera;
los demás cursos tardan unos dos minutos cada uno. Con `--only` se repite solo el que falló.

Las dos partes se probaron rompiendo el motor a propósito: si el campo deja de sujetar el clip en el sitio, fallan
ocho pruebas unitarias y las tres rutas de HP (el partner acaba a 18000 unidades de su sitio, atravesando la roca);
si el paso de raíz cuenta el doble, fallan catorce pruebas unitarias.

## Cursos y límites

Dieciséis cursos en siete gimnasios: trai00 (los seis), gias09 (ataque y velocidad), mist05 (inteligencia), icsa03
(HP y defensa), betl03 (HP y ataque), betl04 (la cinta de velocidad y defensa) y gomi02 (MP). Los límites de trai00
salen del original, medido en DuckStation con el compañero de referencia; los de los otros gimnasios salen de la
sesión del motor, porque no hay partida guardada del original en esos mapas, y se apoyan en las pruebas unitarias,
que ya miran dónde sujeta el original al partner. Entre paréntesis, lo que dio el motor.

Las reglas: los cuadros, el valor de referencia con 2 de margen por abajo y 3 por arriba (1 % en la defensa de
icsa03, y de 1340 a 1410 en la de trai00, que cubre el original y el motor); la distancia y el camino, como mucho lo
mayor entre 1,25 veces la referencia y 40 unidades más, y como poco la mitad cuando pasa de 50; en la carrera, del
90 % al 110 %. Cada ganancia de la referencia, de la mitad a 1,5 veces, y las demás estadísticas, 0. Así falla tanto
un partner que no se mueve como uno que corre el doble, y una ganancia que no llega o que se suma dos veces.

| Curso | Límites de | Cuadros | Distancia a su sitio | Camino | Ganancias |
|---|---|---|---|---|---|
| betl03 hp | motor | 1198–1203 (1200) | 49–139 (99) | 49–138 (98) | hp 60–180 |
| betl03 offense | motor | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | offense 6–18 |
| betl04 defense | motor | 1198–1203 (1200) | 102–257 (205) | 102–257 (205) | defense 6–18 |
| betl04 speed | motor | 1198–1203 (1200) | 0–70 (30) | 0–70 (30) | speed 6–18 |
| gias09 offense | motor | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | hp 5–15 offense 4–12 |
| gias09 speed | motor | 1198–1203 (1200) | 2731–3339 (3035) | 47925–58576 (53250) | hp 5–15 speed 4–12 |
| gomi02 mp | motor | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | mp 75–225 |
| icsa03 defense | motor | 13039–13303 (13171) | 0–40 (0) | 0–40 (0) | defense 6–18 speed 1–3 |
| icsa03 hp | motor | 1198–1203 (1200) | 49–138 (98) | 48–137 (97) | hp 60–180 defense 1–3 |
| mist05 brain | motor | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | mp 5–15 brain 4–12 |
| trai00 brain | original | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | mp 5–15 brain 4–12 |
| trai00 defense | original | 1340–1410 (1357) | 110–275 (220) | 10990–13434 (12395) | defense 4–12 brain 1–3 |
| trai00 hp | original | 1198–1203 (1200) | 40–120 (98) | 40–120 (99) | hp 40–120 offense 1–3 |
| trai00 mp | original | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | mp 40–120 defense 1–3 |
| trai00 offense | original | 1198–1203 (1200) | 0–40 (0) | 0–40 (0) | offense 4–12 speed 1–3 |
| trai00 speed | original | 1199–1204 (1200) | 2754–3368 (3042) | 47916–58565 (53250) | hp 5–15 speed 4–12 |

Lo medido en el original (trai00): HP 1200 cuadros, 80 de distancia y de camino, HP +80 y ataque +2; MP 1200, 0, MP
+80 y defensa +2; ataque 1200, 0, ataque +8 y velocidad +2; defensa 1391 cuadros, 220 de distancia, 12212 de camino,
defensa +8 e inteligencia +2; velocidad 1201 cuadros, 3061 de distancia, 53240 de camino en 10 vueltas, HP +10 y
velocidad +8; inteligencia 1200, 0, MP +10 e inteligencia +8.

Diferencias que quedan con el original, dentro de los límites: en HP el partner se aleja 98 en vez de 80; en defensa
la sesión dura 1357 cuadros en vez de 1391 (el camino, 12395 contra 12212). La defensa de icsa03 dura 13171 cuadros
(unos 11 minutos) porque, como en `trn2_def_map99.c`, solo cuentan los cuadros de caída del objeto y cada golpe
espera 30 cuadros.

Cuando un cambio mueve una de estas cifras a propósito (un arreglo de fidelidad medido en el original), se cambian
el límite de la ruta y, si hace falta, el de la prueba unitaria, en el mismo commit y diciendo de dónde sale la
cifra nueva.
