# 0023. Remaster por recetas, con proveedores intercambiables, caché por contenido y local por defecto

- **Estado:** Aceptada
- **Fecha:** 2026-10-21
- **Nota (2026-11-06):** aceptada porque el código ya la sigue: `dwx upscale <receta.yaml>` aplica las recetas.

## Contexto

La fase 4 incluye fondos y texturas en alta resolución en la capa `remaster`. El usuario quiere una cadena
parametrizable (extraer, adaptar, mejorar con cualquier herramienta, cocinar) en la que el disco sea una dependencia y
los proveedores sean intercambiables: un comando local o un servicio de imágenes en la nube de cualquier empresa.

Condiciones: la salida mejorada es derivada del original y no se puede distribuir
([política legal](../legal/assets.md)); hay cientos de fondos y miles de texturas, así que repetir llamadas cuesta
tiempo y dinero; los fondos son índices con una paleta que el juego elige al jugar y sus capas de profundidad tienen
que seguir casando; `remaster` es una capa del motor, y el código de sus paquetes corre como el motor. El diseño
completo está en [Tubería del remaster](../architecture/remaster-pipeline.md).

## Decisión

- **Recetas, no salidas.** Una receta YAML por tipo de asset describe una cadena de pasos (proveedor, modelo,
  parámetros, semilla, prompt). Las recetas se pueden distribuir; las salidas, las cachés y los paquetes `remaster`
  generados, nunca. Una receta no contiene comandos, URL, rutas ni claves: nombra proveedores que define cada usuario
  en su `enhance-providers.yaml`.
- **Un interfaz de proveedor** (`ImageProvider`) con tres clases: comando local con plantillas `{in}`/`{out}` sin
  shell, HTTP genérico configurable sin nombres de servicios en el código, y pasos integrados deterministas con
  aritmética entera.
- **Por defecto, solo lo integrado:** sin configuración solo existen los pasos integrados, y el escalador es
  `index_scale`; las recetas de ejemplo del repositorio solo usan pasos integrados. Un modelo local con pesos fijados
  por SHA-256 (obligatorio) es una opción del usuario y la mejora recomendada. Un servicio remoto es una opción de
  cada usuario, con su clave y su responsabilidad, que exige que él mismo declare haber leído los términos y que sube
  arte extraído, y que confirme las subidas en **cada ejecución** (`--confirm-upload`), aunque sea gratis.
  Las licencias de los pesos se declaran con SPDX y una desconocida se rechaza salvo que el usuario la acepte en su
  configuración. Nada acepta términos en nombre del usuario.
- **Caché obligatoria por contenido y por paso**, con clave `<sha1>-v<EnhanceCacheVersion>` sobre la identidad del paso
  (con la plantilla del comando, o el id y la versión del modelo que configura la receta y la plantilla de la petición
  HTTP, nunca la versión que dice el servicio), sus parámetros, los límites de troceo y relleno y el hash canónico de
  sus entradas, cerrojos de aviso del sistema, entradas escritas en carpetas `.partial-` y renombradas, huellas que solo
  crecen para los pasos integrados y salidas de pago que nunca se borran solas.
- **Coste visible antes de gastar:** plan con llamadas, trozos, reintentos, píxeles, aciertos de caché y coste
  estimado; sin `--confirm-cost` igual o mayor que el peor caso no hay llamadas de pago; topes por ejecución y por
  mes; reservas pesimistas en un libro con cerrojo que falla cerrado. Un proveedor HTTP declara `pricing` o
  `paid: false`.
- **Proveedores acotados:** cada proveedor declara sus parámetros con tipo y rango (los textos, con
  lista o con longitud y patrón); ningún valor empieza por `-`; la
  sustitución es de una pasada. El transporte HTTP solo usa `https` (y `http` a loopback), sin redirecciones a otro
  origen, sin descompresión y con topes de tamaño. Los procesos hijos tienen tiempo límite, tope de salida, muerte del
  árbol y ninguna variable `DWE_ENHANCE_*`.
- **Secretos solo en el entorno o en el almacén del sistema**, por referencia, redactados en toda salida;
  `dwx validate` rechaza recetas y configuraciones con algo con forma de clave.
- **Escala entera y uniforme declarada en `_meta.yaml` (`scale`)**; la geometría siempre en unidades lógicas. Las
  variantes de paleta de un fondo se derivan de una variante mejorada (`reference_transfer`), así el fundido entre
  imágenes alineadas da lo mismo que el fundido de paletas; las capas de profundidad se recortan del fondo mejorado.
- **Cocinado sin pérdidas de paleta:** los fondos y páginas de los mapas pasan siempre como PNG por su ID
  (`assets::PngOnlyImages`, requisito previo en `main`, rama `p4-cook-maps`); W10b extiende esa lista y la
  comprobación de contenido para que una imagen cuyo `_meta.yaml` declara `scale` y `color_space` sin `palettes` sea
  RGBA y pueda ser KTX2; cada `_meta.yaml` indexado del remaster copia los campos del lateral de la base y añade
  `scale`; solo lo RGBA pasa a KTX2, con `color_space: srgb` y texturas UNORM en el mapa para que el fundido case con
  las paletas.
- **El remaster solo lleva datos.** Lo que escribe `dwe-enhance` son imágenes, sus `_meta.yaml` y `.glb` de texturas, en
  la carpeta de contenido (origen `Engine`), y el montaje rechaza un paquete `layer: remaster` con Lua, RML, RCSS o
  tablas (sí admite audio; decisión 10), porque esa capa corre con la confianza del motor. Lo genera un ejecutable
  aparte, `dwe-enhance` (`apps/enhance`), dueño de los proveedores y de todo el código de red; `dwx` sigue sin red y
  comprueba recetas y paquetes remaster con `dwx validate`, con las reglas en una librería sin red
  (`apps/remaster_rules`). La revisión es un panel del overlay de depuración del juego que llama a
  `dwe-enhance review`.

### Decisiones del manager

Las preguntas abiertas del diseño, «Decidido por el manager, 2026-10-21»:

1. **Fondos con `reference_transfer` por defecto:** una llamada por mapa y fundido limpio entre variantes alineadas.
2. **El visor es un panel del overlay en las vistas de desarrollo**, no una app nueva: reusa render, VFS y cámara
   real, y la fase 4 no tiene `apps/editor`.
3. **La caché va en `%LOCALAPPDATA%` en Windows y en `$XDG_CACHE_HOME` en Linux:** varios GB no deben viajar con el
   perfil ni mezclarse con los datos; el libro de gastos sí va en la carpeta de datos.
4. **Un ejecutable aparte, `dwe-enhance`, y `dwx` sin red:** menos superficie de ataque, `dwx` sigue determinista y
   sin conexión, y las claves nunca llegan a la herramienta que los modders ejecutan en su CI. **Se aparta de la
   redacción original del usuario**, que hablaba de `dwx enhance --recipe`. `dwx` no ofrece `dwx enhance --plan`.
5. **La paleta de los sprites con `clut` −1 se copia en el `_meta.yaml` del fondo RGBA:** el remaster se basta solo
   y `dwx extract` no cambia.
6. **El único ID nuevo que puede añadir un remaster es `<id de página>_<variante>`:** el remaster sustituye; solo
   añade lo que el recorte por variante necesita.
7. **Secretos: primero el entorno (`DWE_ENHANCE_*`) en W10d;** el almacén del sistema (libsecret, Administrador de
   credenciales) solo si su port de vcpkg compila en nuestra cadena de Docker sin instalar nada en el host; si no, es
   un trabajo posterior.
8. **Cadena completa de mipmaps en los atlas:** sin regresión frente a la base y sin cambiar el cocinado.
9. **Aviso de memoria por mapa a partir de 128 MiB en BC7**, como constante con nombre
   (`RemasterMapTextureBudgetBytes`): cabe `k = 4` con tres variantes.
10. **El montaje admite audio en la capa `remaster`**, además de imágenes, `_meta.yaml` y `.glb`, y sigue rechazando
    Lua, RML, RCSS y tablas: la música HD va en esa capa según la tubería de assets, y el audio es dato, no código.
    Solo en los formatos cocinados o extraídos que el motor ya acepta, sin decodificadores nuevos. Al montar solo se
    comprueban extensión, tamaño y cabecera; la decodificación entera queda en `dwx validate` y `--check-content`.
    Topes por formato como constantes en `engine/assets`: Opus/Ogg 64 MiB, WAV 128 MiB, SF2 128 MiB y MIDI 1 MiB,
    cada uno con su test de montaje; sin tope total por paquete por ahora. Los futuros lectores de SF2 y MIDI se
    diseñan para entrada no confiable.
11. **La música HD va en su propio paquete, `remaster_audio`** (`layer: remaster`), no en el de `dwe-enhance`: ese
    paquete rechaza ficheros que no escribió y borra lo que ya no produce.
12. **El tope de salida de un comando local guarda los primeros N bytes y tira el resto, sin matar la herramienta:**
    mucho progreso no es un fallo y vaciar la tubería evita bloqueos. Solo se mata por tiempo límite, y con
    `version_command` (cuya salida entra en la clave) también al pasar del tope.
13. **Los límites de imagen de la UI pasan a `engine/assets` como requisito previo de W10b:** `remaster_rules` nace en
    W10b y los necesita sin depender de `ui`.

## Consecuencias

- Compartir una receta reproducible equivale a compartir el resultado sin distribuir nada derivado.
- Ninguna receta descargada puede ejecutar programas ni mandar arte a otro sitio.
- Una segunda ejecución, o un cambio en un paso posterior al de pago, no cuesta nada.
- El motor cambia de forma aditiva: fondos RGBA con fundido por imagen, `scale` en `_meta`, páginas de sprites por
  variante, `pixelsPerTexel` por textura, muestreo con mipmaps en el camino RGBA del mapa, rechazo al montar de lo que
  no son datos en `remaster` y tamaño lógico en la UI. Los límites de imagen de `engine/ui` se mudan a `engine/assets`
  antes de W10b. `runProcess` gana tiempo límite, tope de salida, muerte del árbol y entorno explícito: es un cambio en
  `main` previo a W10b, igual que el paso de los fondos indexados como PNG al cocinar.
- Un servicio remoto pide confirmación en cada ejecución que suba algo; no se puede dejar aceptado. Un endpoint de
  loopback se trata como local: si es un túnel a la nube, es responsabilidad del usuario.
- La música HD es obra derivada: solo se distribuyen recetas o deltas, y va en su propio paquete `remaster_audio`
  (decisión del coordinador, 2026-10-21, en la [política legal](../legal/assets.md#salida-mejorada-remaster)).
- `dwx` no enlaza nada de red: solo gana `apps/remaster_rules`. libcurl y las API de credenciales solo entran en
  `dwe-enhance`. Hay que actualizar la tabla de [dependencias](../architecture/dependency-rules.md).
- Hay un ejecutable más que distribuir y que el juego busca (como `dwx`) para el panel de revisión.
- La caché puede ocupar varios GB; las salidas de pago se borran solo a mano.
- Las variantes derivadas son una aproximación donde el cambio de paleta no es suave; quien quiera colores exactos
  usa `per_variant`.

## Alternativas descartadas

- **Distribuir paquetes `remaster` ya generados:** es distribuir obra derivada; lo prohíbe la política legal.
- **Comandos y endpoints dentro de la receta:** una receta compartida podría ejecutar código o exfiltrar arte.
- **Un servicio en la nube por defecto:** no es reproducible, cuesta dinero y obliga a subir arte extraído.
- **Clave de caché por receta entera:** cambiar el último paso repetiría (y cobraría) todos los anteriores.
- **Re-cuantizar la salida a la paleta original:** pierde casi toda la ganancia de la mejora.
- **Mejorar cada variante de paleta por separado como norma:** triplica las llamadas y, con modelos generativos,
  rompe el fundido entre horas del día.
- **Factor de escala en una clave propia del KTX2:** no es estándar y el `_meta.yaml` ya decide las opciones del
  cocinado.
- **Un módulo `engine/enhance`:** el runtime no lo necesita; red y credenciales no deben entrar en el juego.
- **`dwx enhance` dentro de `dwx`:** era la redacción original; metía la red y las claves en la herramienta que los
  modders ejecutan en su CI.
