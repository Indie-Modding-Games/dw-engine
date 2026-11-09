# Pasarela de la beta cerrada

Un Worker de Cloudflare (`worker.js`, un solo archivo de JavaScript sin dependencias) entre el lanzador y las
versiones del repositorio. El repositorio es público desde el 2026-11-05, pero el lanzador sigue hablando solo con la
pasarela: es el servidor de versiones que lleva compilado. Quien prueba la beta solo necesita el lanzador: la
**clave de la beta**, una sola clave compartida que genera el responsable, va **dentro del lanzador** desde que se
publica, y el probador nunca la escribe. Con ella el lanzador ve las versiones, descarga el juego y recibe
actualizaciones del juego y del propio lanzador; sin ella, nada.

**Es una barrera ligera, no un secreto.** El repositorio y sus versiones son públicos, y la clave va en el programa
que se reparte: quien tenga el lanzador puede sacarla. Solo evita que la pasarela conteste a cualquiera.

```
lanzador ──(Authorization: Bearer <clave de la beta>)──► pasarela ──(token de solo lectura)──► API de GitHub
    ▲                                                        │
    └──── 302 a un enlace firmado y de corta vida ◄──────────┘   (los paquetes no pasan por el Worker)
```

## Qué hace

Contesta, para un único repositorio, las peticiones de la API de GitHub que usan el lanzador y su actualizador:

| Petición | Respuesta |
|---|---|
| `GET /repos/<repo>/releases` | Las versiones (sin borradores), solo con los campos que lee el lanzador. |
| `GET /repos/<repo>/releases/latest`, `/releases/tags/<tag>` | Una versión, igual. |
| `GET /repos/<repo>/releases/assets/<id>` con `Accept: application/octet-stream` | Un archivo: los JSON pequeños (`game-manifest.json`, `latest.json`) los sirve la pasarela; cualquier otro es un **302** al enlace firmado de GitHub. |
| `GET /repos/<repo>/releases/download/<tag>/<nombre>` | Lo mismo, por versión y nombre. |

- **La clave:** se compara en tiempo constante con el secreto `BETA_KEY` y, si está puesto, con
  `BETA_KEY_PREVIOUS` (la clave anterior, mientras dura un cambio de clave); siempre con las dos, así que el tiempo
  no dice cuál coincide. Sin clave o con otra: `401` con un mensaje JSON, que el lanzador convierte en «El servidor
  de la beta ya no acepta este lanzador. Actualiza el lanzador o vuelve a instalarlo desde la última versión
  publicada.».
- **Las URL:** todas las URL de archivos (`url`, `browser_download_url` y las que van dentro de `latest.json`) se
  reescriben para que apunten a la propia pasarela. El lanzador rechaza un archivo que esté en otro servidor, y su
  actualizador también, así que la clave nunca sale hacia otro sitio.
- **Las descargas grandes:** la pasarela pide el archivo a GitHub sin seguir la redirección y devuelve ese `302`.
  El enlace firmado no lleva ningún token; al seguirlo, el cliente HTTP del lanzador quita la cabecera
  `Authorization` porque cambia de servidor (probado en `apps/launcher/src-tauri/tests/fake_release.rs`).
- **El token de GitHub** solo vive en el secreto `GITHUB_TOKEN`: nunca aparece en una respuesta, ni en el cuerpo ni
  en las cabeceras (probado en `worker.test.js`).
- **Caché:** las listas y las descripciones de archivos se guardan 60 s (en memoria y, con un dominio propio, en la
  caché de Cloudflare), muy por debajo del límite de 5000 peticiones por hora del token.
- Si GitHub rechaza el token de la pasarela (caducado, revocado, límite), el lanzador recibe `503` («no se pudo
  conectar»), no «clave incorrecta»: el problema es de la pasarela, no del probador. El registro del Worker lo dice.

## Ajustes del Worker

| Nombre | Tipo | Valor |
|---|---|---|
| `BETA_KEY` | Secreto | La clave de la beta (al menos 24 caracteres; con menos, la pasarela lo rechaza todo). El mismo valor que el secreto `DWE_BETA_KEY` del repositorio. |
| `BETA_KEY_PREVIOUS` | Secreto, opcional | La clave anterior, solo durante un cambio de clave (abajo). Si está puesta, también 24 caracteres o más. |
| `GITHUB_TOKEN` | Secreto | El token de GitHub de solo lectura (ver abajo). |
| `GITHUB_REPO` | Variable de texto | `Indie-Modding-Games/dw-engine` |

`GITHUB_API` solo existe para las pruebas locales; en Cloudflare no se pone.

## Desplegarlo (panel de Cloudflare)

1. **Workers y Pages → Crear → Crear Worker** (*Workers & Pages → Create → Create Worker*, la plantilla «Hello
   World»). Nombre: `dwe-beta-gateway`. Desplegar (*Deploy*).
2. **Editar código** (*Edit code*): borrar el contenido de `worker.js` en el editor, pegar el de `apps/beta-gateway/worker.js`
   entero y pulsar **Desplegar**.
3. **Configuración → Variables y secretos → Añadir** (*Settings → Variables and Secrets → Add*), y después
   **Desplegar**:
   - `BETA_KEY`, tipo **Secreto**, la clave generada (abajo).
   - `GITHUB_TOKEN`, tipo **Secreto**, el token de GitHub (abajo).
   - `GITHUB_REPO`, tipo **Texto**, `Indie-Modding-Games/dw-engine`.
4. La dirección queda en **Configuración → Dominios y rutas** (*Domains & Routes*): `https://dwe-beta-gateway.<subdominio>.workers.dev`.
   Esa es la URL que se compila en el lanzador (abajo). Un dominio propio es opcional (activa la caché de Cloudflare
   además de la de memoria).
5. Comprobarlo sin dejar la clave en el historial de la terminal:

   ```bash
   read -rs CLAVE
   curl -s -o /dev/null -w '%{http_code}\n' https://dwe-beta-gateway.<subdominio>.workers.dev/repos/Indie-Modding-Games/dw-engine/releases
   curl -s -H "Authorization: Bearer $CLAVE" https://dwe-beta-gateway.<subdominio>.workers.dev/repos/Indie-Modding-Games/dw-engine/releases | head -c 300
   ```

   La primera da `401`; la segunda, la lista de versiones con URL de la pasarela. Una versión **borrador** no
   aparece hasta que se publica (el token de solo lectura no ve borradores).

El plan gratuito de Workers (100 000 peticiones al día) sobra para la beta: el lanzador hace unas pocas peticiones
cada vez que busca actualizaciones.

## La clave de la beta

Generarla en el ordenador del responsable, nunca en un archivo del repositorio:

```bash
openssl rand -base64 32
```

Se pega en **dos** sitios, con el mismo valor:

- el secreto `BETA_KEY` del Worker (arriba);
- el secreto del repositorio `DWE_BETA_KEY` (GitHub → **Settings → Secrets and variables → Actions → Secrets → New
  repository secret**). `.github/workflows/release.yml` lo compila dentro del lanzador y se niega a publicar si falta
  o tiene menos de 24 caracteres. Nunca se escribe en el registro de la ejecución.

A los probadores no se les envía nada: el lanzador ya la lleva. Los lanzadores antiguos que la guardaban en el
llavero del sistema la borran de ahí la primera vez que se actualizan.

**Cambiarla** (si se quiere cortar el acceso a quien tenga un lanzador viejo). Como la clave va dentro del lanzador,
un cambio necesita una versión nueva del lanzador, y la clave anterior sigue valiendo mientras los lanzadores se
actualizan solos:

1. Generar una nueva con `openssl rand -base64 32`.
2. En el Worker: copiar la clave actual de `BETA_KEY` a `BETA_KEY_PREVIOUS`, pegar la nueva en `BETA_KEY` y
   **Desplegar**. Las dos sirven.
3. En el repositorio: pegar la nueva en `DWE_BETA_KEY` y publicar una versión (una fusión en `main`, o
   *Actions → Release → Run workflow*). Los lanzadores se actualizan con la clave anterior y ya traen la nueva.
4. Cuando los probadores ya tengan el lanzador nuevo (unos días), borrar `BETA_KEY_PREVIOUS` y **Desplegar**. Un
   lanzador que no se actualizó dirá «El servidor de la beta ya no acepta este lanzador…» y habrá que reinstalarlo
   desde la última versión publicada.

Si la clave se filtra de verdad, el paso 2 puede saltarse (sin `BETA_KEY_PREVIOUS`): la vieja deja de servir al
momento y todos los probadores tienen que reinstalar el lanzador.

## El token de GitHub

Un *fine-grained personal access token* que solo puede leer el contenido de `dw-engine`. Con el repositorio público
la pasarela podría leer las versiones sin él, pero el token sube el límite de peticiones a la API (5000 por hora en
lugar de 60 por IP, que comparten todos los Workers de la misma salida de Cloudflare):

1. GitHub → **Settings → Developer settings → Personal access tokens → Fine-grained tokens → Generate new token**.
2. **Resource owner:** `Indie-Modding-Games` (si la organización pide aprobación de tokens, un propietario la da).
3. **Expiration:** la que permita la organización (por ejemplo 90 días); antes de que caduque se crea otro y se
   cambia el secreto `GITHUB_TOKEN` igual que la clave.
4. **Repository access:** *Only select repositories* → `dw-engine`.
5. **Permissions → Repository permissions → Contents: Read-only**. GitHub añade *Metadata: Read-only* solo. Ningún
   otro permiso.
6. Generar, copiar y pegarlo directamente en el secreto `GITHUB_TOKEN` del Worker. No se guarda en ningún otro sitio.

## La URL en el lanzador

El lanzador usa como servidor de versiones la URL con la que se compiló:

- `DWE_LAUNCHER_GATEWAY=https://dwe-beta-gateway.<subdominio>.workers.dev scripts/launcher.sh build` (o `bundle`).
- En `.github/workflows/release.yml`, la variable del repositorio `DWE_BETA_GATEWAY` (Settings → Secrets and
  variables → Actions → Variables); el flujo se niega a compilar sin ella.
- Sin esa variable, la compilación apunta a `https://beta-gateway.invalid`, que no existe: una compilación de
  desarrollo nunca habla con un servidor de verdad.
- `DWE_LAUNCHER_API=<url>` lo cambia para una ejecución (pruebas). El servidor no se guarda en `settings.json`.

La clave se compila igual: `DWE_LAUNCHER_BETA_KEY=<clave> scripts/launcher.sh build`, y en `release.yml` desde el
secreto `DWE_BETA_KEY`. `DWE_LAUNCHER_BETA_KEY=<clave>` al ejecutar el lanzador la cambia para esa ejecución
(pruebas). Una compilación a mano sin clave envía la que un lanzador antiguo guardó en el llavero, si la hay.

## Probar en local

Sin instalar nada (node viene en el host o en la imagen `dwe-launcher-dev`):

```bash
node --test apps/beta-gateway/          # el Worker con fetch simulado
scripts/launcher.sh test                    # incluye la prueba de punta a punta (abajo)
```

La prueba `through_the_beta_gateway` de `apps/launcher/src-tauri/tests/fake_release.rs` arranca
`apps/launcher/tools/fake_release_server.py` como GitHub (que redirige cada archivo a otro servidor y rechaza ahí
cualquier petición con `Authorization`, como el almacenamiento de GitHub), la pasarela con `dev/serve.js` delante, y
el cliente del lanzador contra la pasarela: sin clave o con otra, con la clave anterior (`BETA_KEY_PREVIOUS`), la
lista de versiones, la instalación del juego y `latest.json` con su lanzador.

A mano:

```bash
python3 apps/launcher/tools/fake_release_server.py <carpeta> --port 8765 --token token-falso --repo Indie-Modding-Games/dw-engine &
BETA_KEY=clave-de-prueba-de-al-menos-24-caracteres GITHUB_TOKEN=token-falso GITHUB_REPO=Indie-Modding-Games/dw-engine \
  GITHUB_API=http://127.0.0.1:8765 node apps/beta-gateway/dev/serve.js --port 8787 &
DWE_LAUNCHER_API=http://127.0.0.1:8787 DWE_LAUNCHER_BETA_KEY=clave-de-prueba-de-al-menos-24-caracteres \
  build/launcher/release/dwe-launcher
```

## Pendiente

- Frenar por IP los intentos con clave incorrecta (una regla de *rate limiting* de Cloudflare sirve si hace falta).
