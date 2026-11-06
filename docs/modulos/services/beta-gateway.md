# beta-gateway

La pasarela de la beta: un Worker de Cloudflare (`apps/beta-gateway/worker.js`, un solo fichero de JavaScript sin
dependencias) entre el [lanzador](../apps/launcher.md) y las versiones del repositorio en GitHub. El lanzador nunca
habla con GitHub: pide las versiones, los paquetes del juego y sus propias actualizaciones a la pasarela, con la
**clave de la beta** en la cabecera. Sin clave, o con otra, la pasarela no enseña nada.

Cómo desplegarla, crear el token, generar y rotar la clave: el
[README de la pasarela](https://github.com/Indie-Modding-Games/dw-engine/blob/main/apps/beta-gateway/README.md).

```text
lanzador ──(Authorization: Bearer <clave>)──► pasarela ──(token de solo lectura)──► API de GitHub
    ▲                                            │
    └──── 302 a un enlace firmado y corto ◄──────┘   (los paquetes no pasan por el Worker)
```

## Qué contesta

Las peticiones de la API de releases de GitHub que usan el lanzador y su actualizador, para un solo repositorio:

| Petición | Respuesta |
|---|---|
| `GET /repos/<repo>/releases` | Las versiones publicadas (sin borradores), solo con los campos que lee el lanzador, `prerelease` incluido |
| `GET /repos/<repo>/releases/latest` | La versión estable más nueva (GitHub nunca da aquí una prerelease) |
| `GET /repos/<repo>/releases/tags/<tag>` | Una versión por su tag |
| `GET /repos/<repo>/releases/assets/<id>` | Un archivo: los JSON pequeños (`game-manifest.json`, `latest.json`) los sirve la pasarela; cualquier otro es un `302` al enlace firmado de GitHub |
| `GET /repos/<repo>/releases/download/<tag>/<nombre>` | Lo mismo, por versión y nombre |

Los dos canales del lanzador salen de estas respuestas: «Pruebas» toma la versión más nueva de la lista, prerelease o
no; «Estable», la estable más nueva de la lista o, si no está entre las 20 primeras, `releases/latest` (`404`
mientras no hay ninguna). Cada fusión en `main` publica una prerelease (`release.yml`) y `promote.yml` la convierte en
estable sin compilar nada ([publicar una versión](../apps/launcher.md#publicar-una-versión)).

## Reglas

- **La clave** se compara en tiempo constante con el secreto `BETA_KEY` (24 caracteres como mínimo; con menos, la
  pasarela lo rechaza todo). Sin ella: `401`, que el lanzador enseña como «Falta la clave de la beta o no es válida».
- **Las URL** de los archivos (también las de dentro de `latest.json`) se reescriben para apuntar a la pasarela. El
  lanzador rechaza un archivo de otro servidor, así que la clave nunca sale hacia otro sitio.
- **Las descargas grandes** no pasan por el Worker: la pasarela pide el archivo a GitHub sin seguir la redirección y
  devuelve ese `302`. Al seguirlo, el cliente HTTP del lanzador quita la cabecera `Authorization` porque cambia de
  servidor.
- **El token de GitHub** (secreto `GITHUB_TOKEN`, solo lectura del contenido) nunca aparece en una respuesta.
- **Caché:** listas y descripciones se guardan 60 s.
- **Errores:** si GitHub rechaza el token de la pasarela, el lanzador recibe `503` («no se pudo conectar»), no «clave
  incorrecta»: el problema es de la pasarela, no del probador.

## Ajustes

| Nombre | Tipo | Qué es |
|---|---|---|
| `BETA_KEY` | Secreto | La clave de la beta |
| `GITHUB_TOKEN` | Secreto | El token de GitHub de solo lectura |
| `GITHUB_REPO` | Texto | `Indie-Modding-Games/dw-engine` |
| `GITHUB_API` | Texto | Solo en las pruebas locales: la URL del servidor que hace de GitHub |

El lanzador lleva compilada la URL de la pasarela: `DWE_LAUNCHER_GATEWAY` al compilarlo a mano y la variable del
repositorio `DWE_BETA_GATEWAY` en `release.yml`, que no compila sin ella. Sin URL apunta a
`https://beta-gateway.invalid`, que no existe. `DWE_LAUNCHER_API` la cambia para una ejecución.

## Tests

```bash
node --test apps/beta-gateway/     # el Worker con fetch simulado (worker.test.js)
scripts/launcher.sh test               # incluye through_the_beta_gateway, de punta a punta
```

`through_the_beta_gateway` (`apps/launcher/src-tauri/tests/fake_release.rs`) arranca un servidor que hace de GitHub
(`apps/launcher/tools/fake_release_server.py`), la pasarela delante con `dev/serve.js` y el cliente del lanzador
contra ella: sin clave, con otra, la lista de versiones, la instalación del juego y `latest.json`. La CI corre las dos
cuando un pull request toca el lanzador, la pasarela o los scripts de publicación.

## Pendiente

- Frenar por IP los intentos con una clave incorrecta (una regla de límite de peticiones de Cloudflare basta).
