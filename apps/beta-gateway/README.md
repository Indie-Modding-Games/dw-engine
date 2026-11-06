# Pasarela de la beta cerrada

Un Worker de Cloudflare (`worker.js`, un solo archivo de JavaScript sin dependencias) entre el lanzador y las
versiones del repositorio. El repositorio es público desde el 2026-11-05, pero el lanzador sigue hablando solo con la
pasarela: es el servidor de versiones que lleva compilado. Quien prueba la beta solo necesita el lanzador y la
**clave de la beta**, una sola clave compartida que genera el responsable. Con ella el lanzador ve las versiones,
descarga el juego y recibe actualizaciones del juego y del propio lanzador; sin ella, nada. La clave decide quién
recibe la beta a través del lanzador; las versiones también se ven en la página de GitHub del repositorio.

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

- **La clave:** se compara en tiempo constante con el secreto `BETA_KEY`. Sin clave o con otra: `401` con un
  mensaje JSON, que el lanzador convierte en «Falta la clave de la beta o no es válida».
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
| `BETA_KEY` | Secreto | La clave de la beta (al menos 24 caracteres; con menos, la pasarela lo rechaza todo). |
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

Se pega en el secreto `BETA_KEY` y se envía a los probadores por un canal privado, junto con el lanzador. Ellos la
pegan en **Ajustes → Cuenta y acceso → Clave de la beta**; el lanzador la guarda en el llavero del sistema.

**Rotarla** (si se filtra, o cuando se quiera cortar el acceso a alguien):

1. Generar una nueva con `openssl rand -base64 32`.
2. **Configuración → Variables y secretos → `BETA_KEY` → Editar**, pegar la nueva y **Desplegar**. La anterior deja
   de servir al momento.
3. Enviar la nueva clave a los probadores que sigan en la beta. Su lanzador dirá «Falta la clave de la beta o no es
   válida» hasta que la peguen en Ajustes → Cuenta y acceso.

No hace falta recompilar ni publicar un lanzador nuevo.

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

## Probar en local

Sin instalar nada (node viene en el host o en la imagen `dwe-launcher-dev`):

```bash
node --test apps/beta-gateway/          # el Worker con fetch simulado
scripts/launcher.sh test                    # incluye la prueba de punta a punta (abajo)
```

La prueba `through_the_beta_gateway` de `apps/launcher/src-tauri/tests/fake_release.rs` arranca
`apps/launcher/tools/fake_release_server.py` como GitHub (que redirige cada archivo a otro servidor y rechaza ahí
cualquier petición con `Authorization`, como el almacenamiento de GitHub), la pasarela con `dev/serve.js` delante, y
el cliente del lanzador contra la pasarela: sin clave o con otra, la lista de versiones, la instalación del juego y
`latest.json` con su lanzador.

A mano:

```bash
python3 apps/launcher/tools/fake_release_server.py <carpeta> --port 8765 --token token-falso --repo Indie-Modding-Games/dw-engine &
BETA_KEY=clave-de-prueba-de-al-menos-24-caracteres GITHUB_TOKEN=token-falso GITHUB_REPO=Indie-Modding-Games/dw-engine \
  GITHUB_API=http://127.0.0.1:8765 node apps/beta-gateway/dev/serve.js --port 8787 &
DWE_LAUNCHER_API=http://127.0.0.1:8787 build/launcher/release/dwe-launcher
```

## Pendiente

- Frenar por IP los intentos con clave incorrecta (una regla de *rate limiting* de Cloudflare sirve si hace falta).
