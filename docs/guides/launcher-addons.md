# Complementos del lanzador

El [lanzador](../modulos/apps/launcher.md) admite **complementos** de la comunidad: páginas HTML pequeñas que añaden
cosas a su interfaz (un icono en la barra lateral, una página en la pantalla principal, tarjetas en el panel de novedades y una sección de
ajustes). Esta guía explica cómo se hace uno, qué puede hacer (la API v1) y cómo se protege al jugador.

El ejemplo completo está en `apps/launcher/addons/example-notes/` («Notas»: una página donde el jugador apunta lo
que quiere, con su icono en la barra, una tarjeta de novedades y una sección de ajustes). Para empezar, cópialo.

## Qué es un complemento

Una carpeta (o un `.zip` de ella) con:

```text
mi-complemento/
├── addon.json      el manifiesto
├── index.html      la página (la que nombra "entry")
├── dwe-addons.js   el cliente del puente (cópialo de apps/launcher/addons/sdk/)
├── main.js, style.css, icon.svg…
```

### addon.json

```json
{
  "id": "org.ejemplo.notas",
  "name": "Notas",
  "version": "1.0.0",
  "author": "Tu nombre",
  "description": "Qué hace, en una o dos frases.",
  "api": 1,
  "entry": "index.html",
  "slots": ["sidebar", "tabs", "news", "settings"],
  "permissions": { "storage": true, "notify": true, "hosts": ["api.ejemplo.org"] }
}
```

| Campo | Qué es |
|---|---|
| `id` | Identificador único al estilo de un dominio al revés: dos o más partes separadas por puntos, con minúsculas, cifras y guiones (`org.ejemplo.notas`), de 3 a 64 caracteres. Es también el nombre de su carpeta. |
| `name` | Nombre visible, hasta 40 caracteres. |
| `version` | Versión semántica (`1.0.0`, `1.2.0-beta.1`). |
| `author` | Autor, hasta 60 caracteres. |
| `description` | Opcional, hasta 300 caracteres. Se enseña al instalarlo. |
| `api` | La versión de la API para la que está hecho. Hoy solo existe la `1`; el lanzador rechaza cualquier otra. |
| `entry` | La página HTML, dentro de la carpeta. |
| `slots` | Dónde aparece: `sidebar` (barra lateral), `tabs` (una página en el centro de la pantalla principal), `news` (novedades), `settings` (ajustes). Al menos una; `sidebar` necesita `tabs` (el icono abre la página). |
| `permissions` | Opcional. `storage` (guardar sus datos), `notify` (avisos breves) y `hosts` (los servidores a los que puede conectarse, hasta 8: solo el nombre y, si hace falta, el puerto, como `api.ejemplo.org` o `127.0.0.1:8779`; sin `https://`, rutas ni comodines). Un permiso desconocido invalida el manifiesto. |

## Cómo se ejecuta

- El lanzador guarda cada complemento en `<datos>/addons/<id>/` y sus datos en `<datos>/addon-storage/<id>.json`
  (fuera de su carpeta, así que una actualización los conserva).
- Sirve sus archivos con un esquema propio, `dweaddon://localhost/<id>/…` (en Windows,
  `http://dweaddon.localhost/<id>/…`), que solo entrega archivos de la carpeta de **ese** complemento, solo si está
  activo, solo de tipos conocidos (HTML, JS, CSS, JSON, SVG, PNG, JPEG, WebP, GIF, fuentes, texto) y nunca siguiendo
  un enlace que salga de la carpeta.
- Cada respuesta lleva una política de seguridad (CSP) estricta: scripts, estilos, imágenes y fuentes solo de su
  carpeta, `connect-src 'none'` (ni `fetch` ni WebSocket: la red va por `net.fetch`), sin marcos, sin formularios,
  sin `eval`. Los scripts en línea tampoco: pon el código en archivos `.js`. Los estilos en línea sí se permiten.
- La página se carga en un `<iframe sandbox="allow-scripts">`: corre en un origen opaco, sin acceso a la página del
  lanzador, a su IPC, a cookies ni a `localStorage`, sin ventanas emergentes ni navegación de la ventana principal.
- Carga `dwe-addons.js` con una etiqueta `<script>` clásica (no `type="module"`: los módulos no cargan en un origen
  opaco).
- Los complementos activos se cargan al abrir el lanzador. La misma página se abre en tres **vistas**, que el
  cliente indica en `DweAddons.view`:
  - `background`: siempre cargada e invisible. Aquí se registran las piezas de la interfaz.
  - `tab`: el contenido de su página (si registró una).
  - `settings`: su sección en Ajustes → Complementos (si registró una).

## La API v1

Todo pasa por el cliente `dwe-addons.js`, que define `window.DweAddons`. Cada llamada devuelve una promesa; si el
lanzador la rechaza, la promesa falla con un `Error` cuyo `code` dice por qué (`addon-denied`, `addon-host`,
`addon-storage-full`, `network`, `timeout`…).

```js
const dwe = window.DweAddons;
await dwe.connect();                         // { api: 1, view, id }
if (dwe.view === "background") {
  await dwe.ui.sidebar({ label: { es: "Notas", en: "Notes" }, icon: "icon.svg" });
  await dwe.ui.tab({ label: { es: "Notas", en: "Notes" } });
}
```

| Llamada | Qué hace | Requiere |
|---|---|---|
| `connect()` | Saluda al lanzador (reintenta unos segundos). Devuelve `{ api, view, id }`. | — |
| `context()` | Datos de solo lectura: `api`, el complemento (`id`, `version`, sus permisos), `launcher_version`, `game_version`, `game_installed`, `content_installed`, `language` (`es`/`en`) y `platform`. **Nunca** rutas, la clave de la beta, el servidor de versiones ni el disco. | — |
| `ui.sidebar({ label, icon })` | Un icono en la barra lateral que abre su página. `icon` es un `.svg`, `.png` o `.webp` de su carpeta (se muestra como imagen, nunca como HTML). | slot `sidebar` |
| `ui.tab({ label, page? })` | Su página en el centro de la pantalla principal (`page`, por defecto `entry`) en la vista `tab`. Se abre desde su icono o desde la píldora «Complementos». | slot `tabs` |
| `ui.settings({ label, page? })` | Una sección en Ajustes → Complementos con su página en la vista `settings`. | slot `settings` |
| `ui.news(items)` | Hasta 3 tarjetas de novedades: `{ title, body, date? }`, solo texto (el lanzador las pinta y les pone «Complemento «nombre»»). Cada llamada reemplaza las anteriores. | slot `news` |
| `ui.resize(height)`, `ui.autoResize()` | Cambia la altura de su marco (80 a 1600 px); `autoResize` la ajusta al contenido. Solo en las vistas `tab` y `settings`. | — |
| `storage.get(key)`, `.set(key, value)`, `.remove(key)`, `.keys()` | Su almacén clave-valor: valores JSON, claves de hasta 128 caracteres, **256 KB** en total. | `storage` |
| `notify(text, kind?)` | Un aviso breve (hasta 200 caracteres, como mucho uno cada 3 s) con su nombre. `kind`: `info` o `success`. | `notify` |
| `net.fetch(url, { method?, headers?, body? })` | Una petición `GET` o `POST` hecha por el lanzador. Devuelve `{ status, content_type, body }` (texto, hasta 1 MB). | `hosts` |
| `on(event, listener)` | Eventos: `game.status` (`{ running, busy, ready, gameVersion, language }`, cada vez que cambia) y `language` (`{ language }`). | — |

Las etiquetas (`label`) pueden ser un texto o `{ es, en }`; el lanzador elige el idioma del jugador.

### net.fetch

- Solo a los `hosts` declarados y aceptados, comparando nombre y puerto exactos (`api.ejemplo.org` no incluye
  `www.ejemplo.org`).
- Solo `https`; `http` únicamente a este mismo ordenador (`127.0.0.1`, `localhost`), para pruebas.
- Cada redirección se comprueba con la misma lista (y como mucho 3).
- La petición la hace un cliente aparte del lanzador: **nunca** lleva la clave de la beta ni cookies. Las únicas
  cabeceras que se pueden poner son `Accept`, `Content-Type` y `Accept-Language`; cualquier otra (como
  `Authorization`) hace fallar la llamada.
- Cuerpo de hasta 64 KB, respuesta de hasta 1 MB, 20 s como máximo.

### Lo que un complemento nunca puede hacer

No hay llamada para: la clave de la beta o el llavero, leer el disco o extraer, buscar o instalar actualizaciones,
volver a una versión anterior, arrancar el juego u otro programa, abrir carpetas, ni leer o escribir archivos fuera
de su almacén. El puente responde `addon-denied` a cualquier llamada que no esté en la tabla, y el backend vuelve a
comprobar cada una (que el complemento esté activo, sus permisos y sus servidores).

## Permisos y el jugador

- **Al instalar**, el lanzador enseña el nombre, la versión, el autor, la descripción, dónde aparece y qué podrá
  hacer. Si el jugador cancela, no se instala nada.
- **Al actualizar**, enseña lo mismo y marca como «Nuevo» lo que no pedía antes. Si cancela, se queda la versión
  anterior.
- Si alguien edita a mano el `addon.json` de un complemento instalado para pedir más, el lanzador no lo carga
  («Permisos por aceptar») hasta que el jugador lo vuelva a activar y acepte.
- Cada complemento se puede apagar o quitar en Ajustes → Complementos. Quitar uno borra su carpeta y su almacén.

## Modo seguro

En modo seguro el lanzador no carga ni sirve ningún complemento. Se activa:

- abriendo el lanzador con `--safe-mode` (`dwe-launcher --safe-mode`);
- desde Ajustes → Avanzado → Modo seguro (para esta sesión);
- con el botón «Usar el modo seguro» que aparece si el lanzador no se cerró bien la última vez con complementos
  activos.

## Instalar y probar

1. En el lanzador, Ajustes → Complementos.
2. Arrastra la carpeta, el `.zip` o el `addon.json`, o usa «Elegir un .zip…» / «Elegir una carpeta…».
3. Revisa los permisos y pulsa «Instalar».

Al crear uno, instala la carpeta directamente; para probar un cambio, vuelve a instalarla (es una actualización:
conserva sus datos). El lanzador escribe en su registro (Ajustes → Avanzado → Registros) por qué rechaza un
complemento.

El lanzador rechaza, con un mensaje en el idioma del jugador:

- un `addon.json` que falta, no es JSON o tiene un campo mal (dice qué campo y por qué);
- una versión de la API que no conoce;
- un `.zip` con rutas que salen de la carpeta (`../`), rutas absolutas o enlaces;
- un paquete de más de 20 MB comprimido, 40 MB descomprimido o 1000 archivos;
- una carpeta con enlaces simbólicos.

## Pruebas

- `scripts/launcher.sh test` ejecuta, entre otros, los tests del manifiesto, la seguridad de los `.zip`, los
  permisos, la lista de servidores de `net.fetch`, el límite del almacén y `tests/addons_bridge.rs`, que prueba que
  un complemento no puede nombrar ningún comando del lanzador, que `net.fetch` nunca lleva la clave ni cookies (ni
  siquiera hacia el mismo servidor de versiones que sí lo recibe), que no sigue redirecciones a otros servidores y
  que su contexto no contiene rutas ni secretos.
- `apps/launcher/addons/test-fetch/` es un complemento solo para pruebas: su tarjeta de novedades lee las versiones
  del servidor falso con `net.fetch` y su página «Aislamiento» intenta todo lo que un complemento **no** debe
  poder hacer (ver la página del lanzador, usar su IPC, llamar a sus comandos, saltarse `net.fetch`, leer cookies o
  `localStorage`, abrir ventanas, cargar archivos de otro complemento, navegar la ventana principal) y enseña una
  línea «PASA» por cada intento bloqueado. Necesita el servidor falso en el puerto que declara:

  ```bash
  apps/launcher/tools/fake_release_server.py <carpeta de versiones> --port 8779
  DWE_LAUNCHER_API=http://127.0.0.1:8779 build/launcher/release/dwe-launcher
  ```
