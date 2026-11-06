# Metadatos del árbol de evolución

`data/evolution_chart.yaml` conserva posiciones y referencias de iconos de Chart. El extractor los obtiene de la copia local del ejecutable; el motor no distribuye la tabla original ni imágenes. El esquema es `content/schemas/evolution_chart.schema.json`.

El documento contiene `image` (identificador de la textura local) y `entries`. Cada entrada identifica `digimon` y su `index`, posición `x/y` en el lienzo nativo, origen del icono `u/v` y `palette`. Los iconos ocupan 16×16 píxeles; su segundo fotograma está 16 píxeles a la derecha. Los nombres de especie usan los mismos identificadores que el resto de las tablas.

La extracción normal del disco incluye este documento cuando se conoce su ubicación. Para actualizar únicamente estos metadatos, sin regenerar todos los assets:

```sh
dwx ui-chart /ruta/al/ejecutable-original /ruta/nueva/evolution_chart.yaml
```

El comando reconoce la versión por SHA-1, comprueba los límites de las referencias y rechaza sobrescribir un archivo existente. Solo se ha localizado la tabla US; otras versiones se rechazan sin adivinar direcciones. El archivo de salida pertenece a los datos privados del usuario, fuera del repositorio del motor.

Se leen las 61 entradas declaradas por el símbolo y su tamaño, sin extender la lectura a la entrada adicional que intenta recorrer el código original. La tabla indica posiciones e iconos; el estado «criado» viene de la historia de la partida. La pestaña Chart de la ficha del jugador la usa (`apps/game/src/player_chart.cpp`), y un ruleset le puede añadir celdas (`species_extra`).

## Geometría del detalle

`data/evolution_chart_detail` es un documento separado y opcional, extraído del ejecutable reconocido. No cambia el contrato de `data/evolution_chart`. Se puede obtener sin regenerar el contenido:

```sh
dwx ui-chart-detail /ruta/al/ejecutable-original /ruta/nueva/evolution_chart_detail.yaml
```

`lines` contiene 20 registros de ocho coordenadas: primero los cuatro valores X y después los cuatro Y de cada línea. Los grupos consecutivos corresponden a orígenes pares (4), orígenes impares (5), destinos pares (6) y destinos impares (5). `sprites` mantiene esos mismos grupos, con dos coordenadas por registro. Sus posiciones son relativas al centro del lienzo de 320×240. `colors` contiene 12 colores RGB, en pares de trazo principal y borde para los destinos. `insets` contiene seis ventanas `[x, y, ancho, alto]` con origen en la esquina del lienzo.

El lector comprueba los límites del ejecutable y de las coordenadas. Solo hay ubicación US confirmada. La pantalla de detalle de Chart lo combina con las rutas de `data/evolution`, las especies criadas de la sesión y la localización. No hay tablas originales incrustadas en los fuentes ni imágenes en este documento.

El motor valida las dimensiones exactas de las matrices, los rangos de color y los límites de cada inset antes de publicar la geometría. Las relaciones de origen/destino se obtienen de `data/evolution` por identificador estable, preservando los huecos de cada ruta. La interfaz distingue si hay geometría y si hay rutas (`player.chart.detail.layout_ready` y `player.chart.detail.paths_ready`); sin ellas el detalle no se abre.

Para comprobar la lectura sin modificar el pack ni los guardados, la demo acepta `--ui-demo-chart <vista.yaml> --ui-demo-chart-detail <detalle.yaml>`. Las cinco paletas de iconos de detalle coinciden con las de la vista general en la versión US comprobada, aunque sus tablas están en direcciones distintas. Esta coincidencia debe verificarse de nuevo antes de habilitar otras versiones.

La demo admite además `--ui-demo-chart-all-raised` para revisar iconos y distribuciones de rutas pares, impares y vacías. Esta opción afecta únicamente al escenario aislado; no modifica la historia ni los guardados del jugador.
