# 0006. Tres repositorios: código, diseño y arte

- **Estado:** Propuesta
- **Fecha:** 2026-10-18

## Contexto

Al proyecto contribuyen programadores, diseñadores y artistas. El arte fuente pesa mucho, y el riesgo legal del
contenido es distinto del riesgo del código.

## Decisión

| Repositorio | Contenido |
|---|---|
| `engine` | Código, herramientas, esquemas, paquetes en texto y documentación técnica |
| `Digimon-World-Project-Folder` | Diseño del juego: GDD, historia y personajes |
| `Digimon-World-Project-Folder-Assets` | Arte fuente del equipo, con Git LFS |

## Consecuencias

- Los artistas no clonan código y el repositorio de código sigue siendo ligero.
- Si llega una petición de retirada contra el contenido, el motor sobrevive.
- La documentación técnica y la de diseño se enlazan entre sí, sin duplicarse.
- El repositorio de assets hay que vaciarlo antes de reconvertirlo (ver [política legal](../legal/assets.md)).

## Alternativas descartadas

- **Un monorepo:** más simple de clonar, pero mezcla binarios pesados con el código y concentra todo el riesgo legal
  en un solo sitio.
