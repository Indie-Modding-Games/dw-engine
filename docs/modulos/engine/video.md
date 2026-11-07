# video

Las películas del juego: lee el vídeo que `dwx extract` deja en `movie/<nombre>/` frame a frame y da la imagen que toca
en cada momento. No dibuja ni suena nada: eso lo hace quien lo usa (`MovieScreen` en `apps/game`). Es un módulo de la
capa de sistemas y solo depende de `core` y `assets`. El diseño completo, con el flujo de las películas en la partida,
está en [películas](../../architecture/peliculas.md); el formato en disco, en [vídeo](../../formats/video.md).

## Piezas

| Pieza | Qué hace |
|---|---|
| `y4m_reader.hpp` | `Y4mReader`: lee un vídeo YUV4MPEG2 (`.y4m`) de un `assets::FileReader`, frame a frame (`read`) o saltándolos (`skip`); `Y4mStream` es lo que dice su cabecera |
| `av1_reader.hpp` | `Av1Reader`: lo mismo para el vídeo cocinado, AV1 en un IVF (`.ivf`), decodificado con libaom; `skip` también decodifica, porque los frames siguientes se construyen sobre él |
| `picture.hpp` | `YuvPicture` (un frame 4:2:0 en planos, rango completo) y `toRgba`, que lo pasa a RGBA de 8 bits con la conversión de color del decodificador del original |
| `movie_player.hpp` | `readMovieInfo` (lee `movie.yaml`), `MovieInfo` y `MoviePlayer`: abre una película, avanza con el reloj y da la imagen del momento |

## Una película

Cada película es una carpeta del paquete base con tres ficheros: `video.y4m`, `audio.wav` y `movie.yaml` tal como los
escribe `dwx extract`, o `video.ivf` (AV1) y `audio.opus` en un paquete cocinado (`dwx cook`). `MoviePlayer` elige el
lector por la extensión del fichero que resuelve el sistema de ficheros virtual. El YAML dice
el tamaño, los frames por segundo (`video.fps.numerator` y `denominator`), cuántos frames tiene el fichero
(`video.frames`), el número del primero (`video.first_frame`) y, si el original la corta antes, en qué frame para
(`playback.frame_count`). `MovieInfo::shownFrames` son los frames que se ven: hasta ese corte, o todos si la tabla no
la corta o pide más de los que hay.

## Uso

```cpp
auto player = video::MoviePlayer::open(vfs, nombre);     // lee movie.yaml y abre video.y4m o video.ivf
auto sound = video::MoviePlayer::loadSound(vfs, nombre); // el WAV o el Opus entero, o nada si no tiene
// Cada frame del juego: el tiempo sale de la posición del sonido (o del reloj, sin sonido).
if (auto changed = player->advanceTo(seconds); changed && *changed) {
  subirTextura(player->picture());                       // ancho × alto × 4 bytes, filas desde arriba
}
if (player->finishedAt(seconds)) { /* sigue el flujo */ }
```

- **Solo un frame en memoria.** El vídeo de una película pesa cientos de MB: `Y4mReader` lee del fichero por trozos
  (`VirtualFileSystem::open`, también dentro de un `.pak`) y nunca lo tiene entero.
- **El sonido manda.** `advanceTo` salta los frames que llegan tarde, como el original, que nunca espera al vídeo.
  Volver atrás no se puede: un tiempo anterior no cambia la imagen.
- **Fin.** `finishedAt` es cierto cuando el último frame que se ve ya duró lo suyo.
- **Errores.** Una cabecera que no es 4:2:0 progresivo, un frame cortado o un `movie.yaml` sin un campo dan un
  `core::Error` con el nombre del fichero. Una película que el paquete no trae la decide quien llama: `MovieScreen`
  avisa en el log y sigue.

## Tests

El módulo no tiene tests propios: lo prueban las rutas de `apps/game` que ven películas (`movies: true` en una ruta,
[formato de las rutas](../../guides/prueba-vertical.md#formato-de-una-ruta)) y `openAsset`, que comprueba la cabecera
Y4M de cada película al validar un paquete ([assets](assets.md)).
