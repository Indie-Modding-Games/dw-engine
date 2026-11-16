# localization

The texts the player sees in their language ([localization](../../architecture/localization.md)). `Catalog` reads
from the VFS the engine's tables (`localization/<language>/*`) and those of the original game (`text/<language>/*`) for a language and
for English, gives the text of a key with its placeholders filled in (`text`, and `tr` for the catalog the app sets),
falls back to English with a warning when something is missing and changes language on the fly (`setLanguage`). The tables' format
and their checking are in `assets` (`dwe/assets/text_table.hpp`).
