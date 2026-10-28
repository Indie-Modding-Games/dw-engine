# Metadatos del árbol de evolución

`data/evolution_chart.yaml` conserva posiciones y referencias de iconos de Chart. El extractor los obtiene de la copia local del ejecutable; el motor no distribuye la tabla original ni imágenes. El esquema es `content/schemas/evolution_chart.schema.json`.

El documento contiene `image` (identificador de la textura local) y `entries`. Cada entrada identifica `digimon` y su `index`, posición `x/y` en el lienzo nativo, origen del icono `u/v` y `palette`. Los iconos ocupan 16×16 píxeles; su segundo fotograma está 16 píxeles a la derecha. Los nombres de especie usan los mismos identificadores que el resto de las tablas.

La extracción normal del disco incluye este documento cuando se conoce su ubicación. Para actualizar únicamente estos metadatos, sin regenerar todos los assets:

```sh
dwx ui-chart /ruta/al/ejecutable-original /ruta/nueva/evolution_chart.yaml
```

El comando reconoce la versión por SHA-1, comprueba los límites de las referencias y rechaza sobrescribir un archivo existente. Solo se ha localizado la tabla US; otras versiones se rechazan sin adivinar direcciones. El archivo de salida pertenece a los datos privados del usuario, fuera del repositorio del motor.

Se leen las 61 entradas declaradas por el símbolo y su tamaño, sin extender la lectura a la entrada adicional que intenta recorrer el código original. La tabla indica posiciones e iconos; el estado «criado» debe venir de la historia de la partida. Extraerla no implementa por sí solo la navegación, el detalle ni la presentación de Chart.
