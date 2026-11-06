# Glosario

Términos del proyecto y su nombre en el código. Si un término no está aquí, se añade con el PR que lo introduce.

| Término | Significado | En el código |
|---|---|---|
| Acción | Lo que el jugador quiere hacer (`confirm`, `move`…), independiente del dispositivo ([ADR 0014](adr/0014-mandos-y-entrada.md)) | `dwe::input` |
| ADR | Registro de una decisión de arquitectura ([adr/](adr/index.md)) | — |
| Asset | Cualquier pieza de contenido: modelo, textura, mapa, texto, script, sonido | `dwe::assets` |
| Base | La capa de contenido que se genera a partir del disco del usuario | `content/base/` |
| Cocinar | Convertir un asset fuente al formato que carga la GPU | `dwx cook` |
| Contrato del Digimon | Esqueleto y lista de animaciones que todo Digimon debe tener | `dwx validate` |
| Decomp | El proyecto que reconstruyó el código C del juego original. Aquí es la referencia | — |
| Delta / receta | Lo que se distribuye en lugar de un asset derivado del original; se aplica sobre la base del usuario | — |
| ECS | Entidades, componentes y sistemas: el modelo del mundo ([ADR 0012](adr/0012-ecs-con-entt.md)) | `dwe::scene` |
| Ficha de referencia | Resumen de cómo funcionaba un sistema del original ([reference/](reference/index.md)) | `docs/reference/` |
| Fuente (estado) | Asset en formato abierto y editable | — |
| ID de asset | Nombre estable de un asset, como `map/file_city_01/background` | — |
| Modo de juego | Combinación de sistemas con sus propias reglas | `dwe::game::modes` |
| Paquete | Una capa de contenido con su manifiesto | `dwe::assets::Pack` |
| Partner | El Digimon que cría el jugador | `dwe::game::partner` |
| Pilar de diseño | Frase del GDD que dice qué de la esencia del juego no se toca | — |
| Remaster | La capa de contenido HD del equipo | — |
| Ruleset | Conjunto de reglas y valores del juego, como datos ([ADR 0010](adr/0010-reglas-propias-con-rulesets.md)) | `content/packs/` |
| Tamer | El personaje que controla el jugador | — |
| VFS | Sistema de ficheros virtual que apila los paquetes | `dwe::assets` |
| Warp | El punto de un mapa que lleva a otro | `dwe::game::field` |
