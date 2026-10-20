# script

La máquina virtual de Lua 5.5 con sol2 ([ADR 0004](../../docs/adr/0004-lua-y-sol2.md)). Sandbox: sin `io`, `os`, `package` ni carga de ficheros, con límite de memoria y de instrucciones por llamada. Recarga en caliente los scripts editados. Expone la entrada como la tabla `input` y los textos del jugador como `t(clave[, valores])` y `texts.language()` ([localización](../../docs/architecture/localizacion.md)), y `bindCommand` añade funciones globales de un argumento de texto para los comandos de la consola de depuración (`text_font("original")`).
