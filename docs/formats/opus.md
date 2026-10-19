# Sonido Ogg Opus de `dwx cook`

`dwx cook` convierte cada WAV de un paquete en un fichero [Ogg Opus](https://www.rfc-editor.org/rfc/rfc7845) con el
mismo id (`sound/sb/sample_001.wav` → `sound/sb/sample_001.opus`). Lo escribe `apps/dwx/src/audio_cooker.cpp` con
libopusenc y lo lee `engine/assets` (`dwe::assets::decodeOpus`) con libopus. La decisión está en el
[ADR 0017](../adr/0017-cocinado-y-archivos-pak.md).

## Qué escribe

- Un flujo lógico Ogg con un solo flujo Opus, de uno o dos canales (familia de mapeo 0), como el WAV.
- Opus siempre trabaja a 48 kHz: libopusenc remuestrea el WAV (en la extracción US van de 2756 Hz a 44100 Hz) y
  guarda la frecuencia original en el campo `input_sample_rate` de `OpusHead`, solo como información.
- Ajustes fijos, para que el mismo WAV dé los mismos bytes: 64 kbps por canal, tasa variable (VBR), complejidad 10,
  sin relleno en los comentarios y el número de serie Ogg `0x64776531` (`dwe1`; libopusenc pondría uno al azar).
  El texto del proveedor dice las versiones de libopus y libopusenc, fijadas por el baseline de vcpkg.
- Los comentarios (`OpusTags`), en fotogramas del sonido decodificado a 48 kHz:

| Comentario | Qué es |
|---|---|
| `LOOPSTART` | Primer fotograma del bucle |
| `LOOPLENGTH` | Fotogramas del bucle |
| `DWE_UNITY_NOTE` | La nota MIDI que toca el sonido a su altura original (del chunk `smpl`), para el sampler |

`LOOPSTART` y `LOOPLENGTH` son los nombres que usan muchos motores para los bucles en Ogg. El bucle del WAV (`smpl`,
fin incluido) se pasa a 48 kHz redondeando cada extremo al fotograma más cercano y sin salir del sonido, que
decodificado tiene `ceil(fotogramas × 48000 / frecuencia)` fotogramas. Un WAV sin bucle no lleva esos dos
comentarios.

## Qué comprueba el lector

`decodeOpus` lee las páginas Ogg con un lector propio (`engine/assets/src/ogg.cpp`, sin libogg ni opusfile) y rechaza
el fichero, con un mensaje que dice qué falla, si:

- una página no empieza por `OggS` versión 0, no cuadra con su CRC, no es la siguiente del flujo (número de serie y
  de secuencia) o está cortada; la primera no abre el flujo o la última no lo cierra; hay algo detrás del final; o un
  paquete partido no sigue en la página siguiente;
- el primer paquete no es un `OpusHead` de versión 0.x con uno o dos canales y familia 0, o el segundo no es un
  `OpusTags` bien formado;
- la posición final (granule) es menor que el pre-skip, o el sonido pasa de una hora (`MaxSoundFrames`), para que un
  fichero falsificado no pida memoria sin límite;
- un paquete no decodifica, o los paquetes dan menos fotogramas de los que dice la última página, o demasiados más;
- `LOOPSTART`, `LOOPLENGTH` o `DWE_UNITY_NOTE` no son números, falta uno de los dos del bucle o el bucle se sale del
  sonido.

Devuelve PCM de 16 bits a 48 kHz, intercalado por canal, sin el pre-skip del codificador ni el relleno del último
paquete, con el bucle, la nota y la frecuencia original.

## Lo que queda

- Opus es con pérdida y trabaja en paquetes de 20 ms: un bucle muy corto de un instrumento puede sonar con un
  pequeño salto en el punto de vuelta. Los mismos sonidos están sin pérdida dentro del `bank.sf2` de cada banco, que
  se copia tal cual.
- 62 de los 642 WAV de la extracción US salen más grandes en Opus (159 KB en total): son sonidos a muy baja
  frecuencia (2756 a 5513 Hz) que en PCM ocupan menos de 64 kbps. Un bitrate que dependa de la frecuencia original
  lo evitaría.
- libopus elige en tiempo de ejecución rutinas SIMD según la CPU. El mismo binario en la misma máquina da siempre los
  mismos bytes; en CPUs distintas el codificador en coma flotante podría no dar exactamente los mismos.
