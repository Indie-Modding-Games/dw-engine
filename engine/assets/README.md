# assets

Contenido del juego por paquetes ([tubería de assets](../../docs/architecture/assets-pipeline.md)). `AssetId` es el nombre estable de un asset (`digimon/agumon/model`) y `PackManifest` lee el `pack.yaml` de cada paquete: id, versión, capa, prioridad y dependencias. El formato del manifiesto también está en [`content/schemas/pack.schema.json`](../../content/schemas/pack.schema.json). Solo depende de `core`.
