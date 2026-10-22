# Soporte de tests

Lo que comparten los tests de todos los módulos, enlazado con `dwe_test_main`.

`<dwe/test_support/user_assets.hpp>` es el único sitio que lee las variables con los assets propios del usuario que
`scripts/dev.sh` monta en solo lectura: `userDisc()` (`DWE_DISC`, la imagen del disco), `userContent()`
(`DWE_CONTENT`, una carpeta extraída con `dwx`) y `userDecomp()` (`DWE_DECOMP`, una copia del decomp). La CI no tiene
ninguno: un test que los necesita se salta cuando no están y su nombre empieza por «the user's disc» para correr como
test lento.

`<dwe/test_support/temp_folder.hpp>` es `TempFolder`, una carpeta con nombre único bajo la carpeta temporal del
sistema que se borra con todo lo que tiene al final del test. El juego y las herramientas no leen estas variables: en desarrollo usan `--dev-assets`
([assets de desarrollo](../../docs/architecture/assets-de-desarrollo.md)).

`<dwe/test_support/script_warp_fixtures.hpp>` es el `field/script_warps` de un script sintético con una recursión: los
tests de `formats` comprueban que la extracción lo da y los de `field`, que el campo toma ese warp y lo registra una
vez.
