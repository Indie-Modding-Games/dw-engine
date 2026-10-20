# localization

Los textos que ve el jugador en su idioma ([localización](../../docs/architecture/localizacion.md)). `Catalog` lee
del VFS las tablas del motor (`localization/<idioma>/*`) y las del juego original (`text/<idioma>/*`) de un idioma y
del inglés, da el texto de una clave con sus marcadores rellenos (`text`, y `tr` para el catálogo que fija la app),
cae al inglés con un aviso cuando falta algo y cambia de idioma en caliente (`setLanguage`). El formato de las tablas
y su comprobación están en `assets` (`dwe/assets/text_table.hpp`).
