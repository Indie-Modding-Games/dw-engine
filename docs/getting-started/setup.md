# Preparar el entorno

Todo se compila dentro de un contenedor Docker ([ADR 0015](../adr/0015-desarrollo-en-contenedor.md)). En tu máquina
solo necesitas Docker, git y el driver de tu GPU con soporte de Vulkan 1.3.

## 1. Instalar Docker (Ubuntu)

```bash
sudo apt update && sudo apt install -y docker.io docker-buildx
sudo usermod -aG docker $USER
```

Cierra sesión y vuelve a entrar para que el grupo `docker` se aplique. Comprueba que funciona:

```bash
docker run --rm hello-world
```

> Estar en el grupo `docker` equivale a tener permisos de root en la máquina. Es lo habitual en una máquina de
> desarrollo.

## 2. Compilar

```bash
scripts/dev.sh image       # crea la imagen de desarrollo (una vez, o cuando cambie el Dockerfile)
scripts/dev.sh configure   # la primera vez vcpkg compila las dependencias: tarda un rato
scripts/dev.sh build
scripts/dev.sh test
```

El preset por defecto es `linux-clang-debug`, con ASan y UBSan. Para otro: `PRESET=linux-clang-release
scripts/dev.sh configure`.

## 3. Jugar

```bash
scripts/dev.sh run
```

El juego se ejecuta en tu máquina, no en el contenedor. En debug activa las validation layers de Vulkan.

- Muévete con WASD, las flechas, la cruceta o el stick izquierdo de cualquier mando.
- `º`/`` ` `` o `F1` abre la consola de desarrollo. Ahí puedes escribir Lua: `marker.x = 100`.
- Edita `apps/game/scripts/main.lua` con el juego abierto: se recarga al guardar.
- Si tu mando no se reconoce, deja un `gamecontrollerdb.txt` de SDL en la carpeta desde la que lanzas el juego.

## Otros comandos

| Comando | Qué hace |
|---|---|
| `scripts/dev.sh shell` | Abre una terminal dentro del contenedor |
| `scripts/dev.sh format` | Formatea el código con clang-format (`--check` solo comprueba) |
| `scripts/dev.sh tidy` | Pasa clang-tidy (necesita un `configure` previo) |

## Editor

Abre la carpeta con VS Code o CLion y elige "Reopen in Container": usa el mismo `.devcontainer/`.
