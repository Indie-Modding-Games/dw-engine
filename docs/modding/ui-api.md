# API de la interfaz

Lo que el juego ofrece a los documentos de la interfaz (`ui/<pantalla>/`) y lo que acepta de ellos, pantalla por
pantalla. Un aspecto o un mod que rehace una pantalla trabaja contra esta lista: lo que está aquí es estable dentro de
una versión mayor, lo que no está puede cambiar sin aviso. Cómo se cargan y se pintan los documentos está en
[UI](../architecture/ui.md); el estilo de cada pantalla, en [aspectos](aspectos.md).

## Ajustes, comandos, estados, opciones y argumentos

- Un **ajuste** es un valor que el juego publica y el documento lee con `ui.setting("nombre")`. Cuando cambia, el
  documento recibe `on_setting(document, nombre)`.
- Un **comando** es lo que el documento manda con `ui.send("nombre", valor)` y el juego atiende: abrir un menú, usar
  un objeto, guardar. Un comando que el juego no conoce queda en el log como aviso, no se pierde en silencio.
- Un **estado** es lo que el documento cuenta con `ui.report("nombre", valor)` y el juego lee cuando lo necesita:
  por ejemplo si una página enseña ahora un modelo en 3D. Vale el último valor; no es una petición que el juego
  atienda una vez como un comando.
- Una **opción** es un ajuste del jugador (los de «Opciones»): el documento lo lee como un ajuste y lo cambia
  mandando su nombre con el valor nuevo.
- Un **argumento** es un valor con el que otro documento muestra este: `ui.show("nombre", modal, {clave = valor})`
  lo pasa y el documento lo lee en `on_show(document, args)`. `args` siempre es una tabla (vacía si no hay
  argumentos) y solo lleva sí/no, números y textos. Así un menú le dice al siguiente dónde empezar sin pasar por un
  ajuste del juego.

En un nombre, `<n>` es un número desde 0 y `<id>` un identificador (minúsculas, cifras y `_`). Así,
`hud.meter.<n>.value` cubre `hud.meter.0.value`, `hud.meter.1.value`...

## Funciones del motor y ayuda en el editor

- Una **función** es parte de la API propia del motor, igual en todos los documentos: la tabla `ui`, `hooks`, `t`,
  `document_name`, los manejadores que el documento define (`on_show`, `on_tick`...) y los métodos de sus elementos
  (`Element:set_class`) y eventos. Sin firma es un campo de solo lectura. Un test comprueba que lo declarado es justo
  lo que el motor da.
- `content/schemas/ui_api/ui_api.d.lua` tiene esas funciones y las de los espacios de la API con sus parámetros, para
  el servidor de Lua (LuaLS): añade la carpeta a `workspace.library` en tu `.luarc.json` y el editor completa nombres
  y tipos. Los tests lo generan junto con esta página.

## Espacios de la API: consultas, acciones y eventos

Además de los ajustes sueltos, la interfaz tiene espacios con nombre (`clock`, `field`, `partner`, `bag`, `player`,
`options`, `battle`, `versus`, `story`, `dialog`, `naming`) que cualquier documento usa igual, sea de la interfaz de
serie o de otra:

- Una **consulta** lee el juego y devuelve una tabla nueva: `partner.status()`, `bag.slot(3)`, `clock.now()`.
  Cambiar esa tabla no cambia nada del juego.
- Una **acción** pide algo al juego: `bag.use(3)`, `options.set("digital_time", false)`. Comprueba sus argumentos y
  devuelve `true`, o `false` y por qué; el juego vuelve a comprobarlo y responde con un evento.
- Un **evento** llega a las funciones que el documento registra con `hooks.on("bag.changed", función)`. La función
  recibe una tabla con `version` (la de la tabla del evento), `name` (el ajuste que cambió) y `value`, y el documento.
  `hooks.off` la quita; un documento registra como mucho 64. Los eventos llegan también a los documentos ocultos: una
  pantalla que solo dibuja mientras se ve lo comprueba con `ui.is_visible(document_name)`.

Los espacios viven en `ui/api/<espacio>.lua` del paquete del juego y se cargan una vez, antes que cualquier documento.
Lo que ocultan las «Mejoras» apagadas también lo ocultan aquí: `partner.extras()` da `nil` si el jugador tiene
apagados los extras del estado.

## Maquetación

Una pantalla que se maqueta de dos formas pregunta cuál quiere el aspecto con `ui.layout("opción")`, que da el valor
que el aspecto pone a esa opción en esa pantalla o `nil` (`skin.yaml`, `layout:`; ver [aspectos](aspectos.md)). Los
scripts no preguntan por el id del aspecto (`ui.setting("ui_skin")`) ni por sus clases para decidir la disposición.

## Versiones

La API de la interfaz lleva su propia versión, aparte de la [API de Lua](api-lua.md): una versión menor añade
nombres, una mayor quita alguno o cambia lo que significa. Mientras sea 0.x describe lo que las pantallas usan hoy y
puede moverse entre versiones del juego.

| Versión | Cambios |
|---|---|
| 0.15.0 | Llegan los espacios `story` (el cuadro de texto de la historia: `story.textbox()`, `story.choice()`, `story.input()`; las acciones `story.advance`, `story.choose` y `story.cancel`; los eventos `story.changed`, `story.text` y `story.input`), `dialog` (el diálogo de guardar: `dialog.state()`, `dialog.options()`, `dialog.answer`, `dialog.cancel`, el evento `dialog.changed`) y `naming` (la caja del nombre: `naming.state()`, `naming.cell`, `naming.write`, `naming.erase`, `naming.accept`, el evento `naming.changed`). Nuevos ajustes `story_textbox.speaker`, `speaker_kind`, `options`, `first_option`, `input` y `page`, y los comandos `story_advance`, `story_choose`, `story_cancel`, `naming_write`, `naming_erase` y `naming_accept`. Las pantallas `story_textbox`, `dialog` y `naming_box` solo leen por ellos. Un addon solo los manda mientras su cuadro espera al jugador |
| 0.14.0 | Llega el espacio `battle`: el estado del combate, los luchadores, la tira de órdenes, el golpe final, la apertura, los números, las barras de los enemigos, el grito, la caja del final y lo que enseña un combate de copa (`battle.match()`), con sus acciones (`battle.command`, `battle.charge`, `battle.open_bag`, `battle.close_bag`, `battle.close`, `battle.end_count`, `battle.end_counted`) y eventos (`battle.started`, `battle.ended`, `battle.phase`, `battle.changed`...). Las pantallas `battle` y `tournament_match` solo leen por él. Llega el espacio `versus`: cada vista del versus en línea (`versus.state()`, `versus.setup()`, `versus.lineup()`, `versus.fighters()`, `versus.score()`...), sus pasos (`versus.step`, `versus.pick`) y su evento (`versus.changed`); la pantalla `versus` solo lee por él y por `battle.intro()`. Un addon solo manda las órdenes del combate en su momento: las del combate en la fase `fight`, cerrar la bolsa con la bolsa abierta y las de la caja del final en la fase `end` |
| 0.13.0 | Llega la pantalla `battle` con la tira de órdenes (`battle.menu.<n>.*`, con `mp_cost`, los PM que cuesta cada técnica), las barras de los luchadores y lo que el combate manda de vuelta |
| 0.12.0 | Los avisos entran en el contrato: `pause_box`, `field_item_notice`, `award_notice`, `fishing_notice`, `save_indicator`, `money_box`, `lives_box` y `ui_notice`, con sus ajustes y comandos |
| 0.11.0 | Complementos (addons de tipo `plugin`, [interfaces](interfaces.md)): documentos sobre cualquier interfaz. Opción `ui_plugins` y ajuste `ui_plugins.count` |
| 0.10.0 | Addons de interfaz ([interfaces](interfaces.md)): `ui_addon`, `ui_addons`, `ui_addon.name.<id>`, `ui_addon.safe`, `ui_addon.problem` y `ui_addon_reload`; el aviso `ui_notice`. El `ui.send` de un addon solo deja pasar los comandos y opciones del contrato |
| 0.9.0 | La API propia del motor (`ui`, `hooks`, `t`, `document_name`, los manejadores `on_*` y los métodos de elementos y eventos) entra en el contrato como funciones. Se generan definiciones para el servidor de Lua en `content/schemas/ui_api/ui_api.d.lua` |
| 0.8.0 | Pantallas: el juego pide una pantalla y la interfaz activa decide qué documento la enseña. La interfaz por defecto se describe en `ui/addon.yaml` del paquete del juego |
| 0.7.0 | Llegan los estados (`ui.report`): los menús cuentan al juego qué retrato en 3D dibujar (`pause_menu.portrait`, `pause_menu.medal`, `partner_menu.portrait`) en vez de que el juego lea sus clases |
| 0.6.0 | Llegan los espacios `clock`, `field`, `partner`, `bag`, `player` y `options` con sus consultas, acciones y eventos, y `hooks.on`/`hooks.off`. El HUD y las opciones de los menús ya leen por ellos |
| 0.5.0 | El HUD lee del juego sus reglas: `hud.clock.night`, `hud.clock.phase` y `hud.meter.<n>.mood`. Su script se parte en `presenter.lua` (qué enseña) y `view.lua` (cómo lo dibuja) |
| 0.4.0 | Llega la pantalla `skin_picker`, la caja del primer arranque que pregunta el aspecto: `ui_skin`, `ui_skin_preview` y `skin_picker_done` |
| 0.3.0 | Llegan los argumentos de `ui.show` y `on_show(document, args)`. Se va `pause_menu.start_entry`: el menú del compañero le pasa `entry` al menú de pausa al volver |
| 0.2.0 | Llega `ui.layout`. Se van `pause_menu.evolutions` y `open_evolutions`, la pestaña de evoluciones del menú del compañero que ya no usa ningún aspecto, y la opción `ui_skin` del menú del compañero, que ahora pregunta por su maquetación |
| 0.1.0 | La lista de lo que usan hoy el HUD, el menú de pausa y el menú del compañero |

La lista sale de `apps/game/src/ui_contract.cpp`. Un test la compara con esta página, con
`content/schemas/ui_api/ui_api.json` (la misma lista para herramientas y editores) y con lo que leen y mandan los
scripts de cada pantalla; `DWE_WRITE_API_DOC=1` al correr los tests del juego reescribe las dos. De momento cubre el
HUD, el menú de pausa, el menú del compañero, la caja del aspecto de cada arranque, los avisos, el combate (con lo
que enseña un combate de copa), el versus en línea y las cajas de la historia (el cuadro de texto, el diálogo de
guardar y la caja del nombre); el resto de pantallas llega después.

## Referencia

<!-- ui-api:generated -->
Versión de la API de la interfaz: **0.15.0**.

### `ui`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `ui.setting(name: string): any` | función | número o texto | El valor de un ajuste que el juego publica; nil si no lo publica. |
| `ui.send(name: string, value: any?)` | función | — | Manda un comando al juego, que lo atiende una vez. |
| `ui.report(name: string, value: any?)` | función | — | Cuenta un estado al juego, que lee el último valor cuando lo necesita. |
| `ui.show(screen: string, modal: boolean?, args: table?)` | función | — | Enseña una pantalla; modal toma la navegación. args llega a su on_show. |
| `ui.hide(screen: string)` | función | — | Esconde una pantalla. |
| `ui.is_visible(screen: string): boolean` | función | sí/no | Si una pantalla se ve. |
| `ui.layout(option: string): string?` | función | texto | El valor que el aspecto da a una opción de disposición en esta pantalla; nil sin él. |
| `ui.prompt(action: string): string` | función | texto | El nombre del control que hace una acción en el dispositivo en uso. |
| `ui.device(): string` | función | texto | El dispositivo en uso: keyboard_mouse o gamepad. |
| `ui.gamepad_family(): string` | función | texto | La familia del mando en uso. |
| `ui.languages(): string[]` | función | tabla | Los idiomas que el jugador puede elegir. |
| `ui.skins(): string[]` | función | tabla | Los aspectos que el jugador puede elegir. |
| `ui.skin_name_key(id: string): string` | función | texto | La clave de texto del nombre de un aspecto; vacía si no existe. |
| `ui.cue(name: string): boolean` | función | sí/no | Suena box_open, box_close o confirm; false con otro nombre. |
| `t(key: string, args: table?): string` | función | texto | El texto de una clave en el idioma del jugador, con sus huecos rellenos. |
| `document_name` | función | texto | El nombre con el que la interfaz cargó el documento. |

### `hooks`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `hooks.on(event: string, handler: fun(event: table, document: Element))` | función | — | Llama a handler cada vez que llega el evento; hasta 64 por documento. |
| `hooks.off(event: string, handler: function)` | función | — | Deja de llamar a handler con ese evento. |

### `document`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `on_show(document: Element, args: table)` | función | — | Lo llama la interfaz al enseñar el documento. |
| `on_hide(document: Element)` | función | — | Lo llama la interfaz al esconder el documento. |
| `on_setting(document: Element, name: string)` | función | — | Lo llama la interfaz cuando cambia un ajuste, con el documento a la vista. |
| `on_action(document: Element, action: string): boolean?` | función | sí/no | Una acción de navegación (confirm, cancel, menu...); true si el documento la atiende. |
| `on_tick(document: Element, seconds: number)` | función | — | Cada frame con el documento a la vista. |
| `on_device(document: Element)` | función | — | Cambió el dispositivo en uso. |
| `on_language(document: Element)` | función | — | Cambió el idioma. |

### `Element`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `Element:valid(): boolean` | función | sí/no | Si el elemento existe. |
| `Element.id` | función | texto | Su id (campo de solo lectura). |
| `Element:get(id: string): Element` | función | tabla | El elemento con ese id dentro de este. |
| `Element:set_text(text: string)` | función | — | Pone su texto. |
| `Element:set_prompt(action: string, icon: boolean?)` | función | — | Lo convierte en el dibujo del control que hace la acción. |
| `Element:set_prompt_text(text: string, original: boolean?)` | función | — | Pone un texto con sus marcas {prompt:acción} dibujadas como controles. |
| `Element:text(): string` | función | texto | Su contenido. |
| `Element:set_class(name: string, on: boolean)` | función | — | Pone o quita una clase. |
| `Element:has_class(name: string): boolean` | función | sí/no | Si tiene una clase. |
| `Element:attribute(name: string): string?` | función | texto | El valor de un atributo; nil sin él. |
| `Element:set_attribute(name: string, value: string)` | función | — | Pone un atributo; no uno de evento (on...). |
| `Element:set_property(name: string, value: string)` | función | — | Pone una propiedad de estilo. |
| `Element:remove_property(name: string)` | función | — | Quita una propiedad que puso set_property. |
| `Element:rect(): number, number, number, number` | función | número | Su caja en dp desde la esquina de la ventana: izquierda, arriba, ancho y alto. |
| `Element:focus()` | función | — | Le da el foco. |
| `Element:has_focus(): boolean` | función | sí/no | Si tiene el foco. |
| `Element:click()` | función | — | Lo pulsa. |
| `Element:scroll_to_top()` | función | — | Vuelve una lista arriba. |

### `Event`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `Event.type` | función | texto | El tipo del evento (campo de solo lectura). |
| `Event.wheel` | función | número | Cuánto giró la rueda, positivo hacia abajo (campo de solo lectura). |
| `Event:stop()` | función | — | No deja que el evento siga subiendo. |

### `hud`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `hud.revision` | ajuste | número | Sube cada vez que cambia el estado del campo de abajo. |
| `hud.shown` | ajuste | sí/no | Si se ven los paneles de estado del campo. |
| `hud.clock.year` | ajuste | número | El año del reloj del juego. |
| `hud.clock.day` | ajuste | número | El día del año del reloj del juego. |
| `hud.clock.hour` | ajuste | número | La hora del reloj del juego, de 0 a 23. |
| `hud.clock.minute` | ajuste | número | El minuto del reloj del juego. |
| `hud.clock.night` | ajuste | sí/no | Si es de noche: antes de las 6:00 y desde las 17:00, cuando el reloj del original enseña la luna. |
| `hud.clock.phase` | ajuste | texto | La parte del día: morning, day, late o night (la regla del juego, no la de la interfaz). |
| `hud.meter.<n>.label` | ajuste | texto | La clave de texto de un medidor del compañero (felicidad, disciplina). |
| `hud.meter.<n>.value` | ajuste | número | El valor de un medidor del compañero. |
| `hud.meter.<n>.min` | ajuste | número | El valor más bajo de un medidor del compañero. |
| `hud.meter.<n>.max` | ajuste | número | El valor más alto de un medidor del compañero. |
| `hud.meter.<n>.mood` | ajuste | texto | low por debajo de la mitad de su rango, donde el original cambia el icono de la barra; si no, high. |
| `hud.meter.<n>.two_tone` | ajuste | sí/no | Si el medidor se llena hacia los dos lados desde su centro. |
| `hud.pool.<n>.label` | ajuste | texto | La clave de texto de los PV o los PM. |
| `hud.pool.<n>.value` | ajuste | número | Los PV o los PM del compañero ahora. |
| `hud.pool.<n>.max` | ajuste | número | Los PV o los PM máximos del compañero. |
| `hud.need.<n>.label` | ajuste | texto | La clave de texto de una necesidad (hambre, sueño, baño...). |
| `hud.need.<n>.active` | ajuste | sí/no | Si el compañero tiene esa necesidad ahora. |
| `hud.need.<n>.urgency` | ajuste | número | Su puesto entre las necesidades activas; 1 es la más urgente. |
| `hud.partner.area` | ajuste | número | 1 si al compañero le gusta la zona, 2 si no le gusta, 0 si ninguna de las dos. |
| `hud.partner.revision` | ajuste | número | Sube cada vez que cambia la posición del compañero de abajo. |
| `hud.partner.x` | ajuste | número | La posición del compañero en la ventana, en porcentaje de su ancho. |
| `hud.partner.y` | ajuste | número | La posición del compañero en la ventana, en porcentaje de su alto. |
| `hud.partner.visible` | ajuste | sí/no | Si el compañero está en la ventana. |
| `hud.partner.evolving` | ajuste | sí/no | Si el compañero está evolucionando. |
| `hud.minimap_allowed` | ajuste | sí/no | Falso mientras el minimapa no va en pantalla (combates, escenas). |
| `hud.minimap_covered` | ajuste | sí/no | Verdadero mientras una ventana tapa el minimapa. |
| `battle.active` | ajuste | sí/no | Si hay un combate en curso. |
| `screen_fade` | ajuste | número | Cuánto oscurece el fundido de pantalla, de 0 a 1. |
| `map_title_key` | ajuste | texto | La clave de texto del título del mapa que se muestra al llegar; vacía si no hay. |
| `shop_job.shown` | ajuste | sí/no | Si se ve la barra del trabajo en la tienda. |
| `shop_job.tally` | ajuste | número | La cuenta del trabajo en la tienda hasta ahora. |
| `shop_job.goal` | ajuste | número | La cuenta a partir de la cual el tendero se une. |
| `shop_job.better_pay` | ajuste | número | La cuenta a partir de la cual la paga es mejor. |
| `shop_job.customers` | ajuste | número | Los clientes atendidos. |
| `shop_job.customer` | ajuste | número | El cliente que se está atendiendo. |
| `shop_job.revision` | ajuste | número | Sube cada vez que cambia algo del trabajo en la tienda. |
| `minimap` | opción | texto | El minimapa: on u off. |
| `minimap_size` | opción | texto | El tamaño del minimapa. |
| `digital_time` | opción | sí/no | La hora en cifras. |
| `need_bubble_stack` | opción | sí/no | Todas las necesidades activas sobre el compañero, no solo la primera. |
| `open_menu` | comando | — | Abre el menú de pausa (el botón de menú del HUD). |

### `pause_menu`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `pause_menu.at_title` | ajuste | sí/no | El menú se abre sobre la portada: solo las opciones. |
| `pause_menu.start_page` | ajuste | texto | options cuando Esc o Select abrieron el menú en su página de opciones. |
| `pause_menu.preview_player` | ajuste | sí/no | Abre directamente la página del jugador (vistas previas). |
| `pause_menu.sleep_enabled` | ajuste | sí/no | Si el compañero puede irse a dormir ahora. |
| `pause_menu.fishing` | ajuste | número | 0 sin caña, 1 con caña pero sin agua, 2 el jugador puede pescar. |
| `hud.clock.year` | ajuste | número | El año del reloj del juego, para la cabecera de la fecha. |
| `hud.clock.day` | ajuste | número | El día del reloj del juego, para la cabecera de la fecha. |
| `field.player_x` | ajuste | número | La posición del jugador en la ventana en porcentaje, desde donde crece la caja del menú. |
| `field.player_y` | ajuste | número | La posición del jugador en la ventana en porcentaje, desde donde crece la caja del menú. |
| `season` | ajuste | texto | La estación actual, cuando las estaciones están activas. |
| `player.name` | ajuste | texto | El nombre del jugador. |
| `player.level` | ajuste | número | El nivel del jugador. |
| `player.bits` | ajuste | número | El dinero del jugador. |
| `player.seconds` | ajuste | número | El tiempo jugado, en segundos. |
| `player.raised` | ajuste | número | Los compañeros criados. |
| `player.medals` | ajuste | número | Las medallas conseguidas. |
| `player.medals.held` | ajuste | texto | Cada medalla, 1 si se tiene o 0, como lista de números. |
| `player.medal.<n>.name` | ajuste | texto | La clave de texto de una medalla. |
| `player.medal.<n>.line.<n>` | ajuste | texto | Una línea de la descripción de una medalla, como clave de texto. |
| `player.items.held` | ajuste | texto | Los objetos clave que se tienen, como lista de números. |
| `player.cards.copies` | ajuste | texto | Las copias de cada carta, como lista de números. |
| `player.card.<n>.palette` | ajuste | número | La paleta con la que se dibuja la imagen de una carta. |
| `player.chart.count` | ajuste | número | Las casillas de la tabla de evoluciones. |
| `player.chart.<n>.id` | ajuste | texto | La especie de una casilla de la tabla. |
| `player.chart.<n>.name` | ajuste | texto | El nombre de una casilla de la tabla. |
| `player.chart.<n>.level` | ajuste | número | El nivel de una casilla de la tabla. |
| `player.chart.<n>.source` | ajuste | texto | La imagen de una casilla de la tabla. |
| `player.chart.<n>.<id>` | ajuste | número | La posición de una casilla de la tabla y el rectángulo de su imagen (index, x, y, u, v). |
| `player.chart.<n>.from.<n>` | ajuste | número | Una casilla de la que evoluciona una casilla de la tabla; -1 si ninguna. |
| `player.chart.<n>.to.<n>` | ajuste | número | Una casilla a la que evoluciona una casilla de la tabla; -1 si ninguna. |
| `player.chart.detail.<id>.<n>.<n>` | ajuste | número | Las tablas de la vista de detalle (líneas, colores, sprites, márgenes). |
| `player.chart.detail.layout_ready` | ajuste | sí/no | Las tablas de la vista de detalle están publicadas. |
| `player.chart.detail.paths_ready` | ajuste | sí/no | Las listas from y to de las casillas están publicadas. |
| `player.chart.raised` | ajuste | texto | Las casillas criadas hasta ahora, como lista de números. |
| `player.chart.mega` | ajuste | texto | Las especies del paso Mega, separadas por comas. |
| `player.chart.preview` | ajuste | sí/no | Abre la página del jugador en la tabla (vistas previas). |
| `player.city.species` | ajuste | texto | Los habitantes de la ciudad, como lista. |
| `player.city.levels` | ajuste | texto | Los niveles de los habitantes, como lista. |
| `player.city.points` | ajuste | texto | Los puntos de prosperidad que aporta cada habitante, como lista. |
| `player.city.recruited` | ajuste | texto | Qué habitantes se han reclutado, como lista. |
| `player.city.residents` | ajuste | número | Los habitantes reclutados. |
| `player.city.total` | ajuste | número | Los habitantes que hay. |
| `player.city.prosperity` | ajuste | número | La prosperidad de la ciudad. |
| `evoreq.version` | ajuste | número | Sube cada vez que cambian los requisitos de evolución de abajo. |
| `evoreq.ready` | ajuste | sí/no | Los requisitos de evolución están publicados. |
| `evoreq.hours` | ajuste | texto | Las horas de cada paso de evolución. |
| `evoreq.partner` | ajuste | texto | Cómo va el compañero respecto a sus evoluciones. |
| `evoreq.partner.<id>` | ajuste | texto | Cómo cumple el compañero los requisitos de una evolución. |
| `evoreq.req.<id>` | ajuste | texto | Los requisitos de evolución de una especie, como rangos. |
| `open_partner_menu` | comando | texto | Abre el menú del compañero en una página: items o status. |
| `pause_menu.portrait` | estado | sí/no | Si el juego dibuja en 3D al jugador sobre su página (una mejora). |
| `pause_menu.medal` | estado | número | La medalla que el juego dibuja en 3D sobre la página de medallas, de 0 a 14; -1 ninguna. |
| `praise` | comando | — | Felicita al compañero. |
| `scold` | comando | — | Regaña al compañero. |
| `sleep` | comando | — | Cierra el menú y manda al compañero a dormir. |
| `fish` | comando | — | Cierra el menú y saca la caña. |
| `quick_save` | comando | — | Guarda la partida. |
| `to_title` | comando | — | Vuelve a la portada. |
| `quit` | comando | — | Sale del juego. |
| `title_options_back` | comando | — | Cierra las opciones abiertas sobre la portada. |
| `language` | opción | texto | El idioma. |
| `fullscreen` | opción | sí/no | Pantalla completa o ventana. |
| `vsync` | opción | sí/no | Sincronización vertical. |
| `ui_scale` | opción | número | La escala de la interfaz; 1 es la normal. |
| `ui_skin` | opción | texto | El aspecto de la interfaz. |
| `ui_addon` | opción | texto | La interfaz entera: default (la del juego) o el id de una de ui/addons; se aplica al cerrar el menú. |
| `ui_addons` | ajuste | texto | Las interfaces que se ofrecen, ids separados por espacios, default la primera. |
| `ui_addon.name.<id>` | ajuste | texto | El nombre de una interfaz, de su addon.yaml. |
| `ui_addon.safe` | ajuste | sí/no | Modo seguro (--safe-ui o Mayús al arrancar): solo la interfaz del juego. |
| `ui_plugins` | opción | sí/no | Los complementos de ui/addons (addons de tipo plugin), todos encendidos o apagados. |
| `ui_plugins.count` | ajuste | número | Cuántos complementos se ofrecen. |
| `ui_addon_reload` | comando | — | Vuelve a leer de sus archivos la interfaz puesta y los complementos. |
| `text_font` | opción | texto | La fuente del texto con aspecto original: modern u original. |
| `walk_control` | opción | texto | Caminar con el teclado o con el ratón: keys o mouse. |
| `autosave` | opción | sí/no | Guardado automático. |
| `world_map` | opción | sí/no | El mapa de la isla. |
| `minimap` | opción | texto | El minimapa: on u off. |
| `minimap_size` | opción | texto | El tamaño del minimapa. |
| `minimap_items` | opción | sí/no | Objetos en el minimapa. |
| `minimap_enemies` | opción | sí/no | Enemigos en el minimapa. |
| `bulk_trades` | opción | sí/no | Intercambios por lotes. |
| `long_names` | opción | sí/no | Nombres largos. |
| `evolution_requirements` | opción | sí/no | Los requisitos de evolución en la tabla. |
| `canonical_stages` | opción | sí/no | Nombres canónicos de las etapas. |
| `status_extras` | opción | sí/no | Los extras de la página del compañero. |
| `rare_items` | opción | sí/no | Objetos raros. |
| `battle_speed` | opción | texto | Velocidad de combate: normal, key o fast. |
| `enemy_hp_bars` | opción | sí/no | Barras de PV de los enemigos. |
| `gym_sessions` | opción | número | Sesiones de gimnasio seguidas. |
| `quick_battle_start` | opción | sí/no | Inicio rápido de combate. |
| `difficulty` | opción | texto | Dificultad: normal o easy. |
| `skip_evolution` | opción | sí/no | Saltar la escena de evolución. |
| `choice_cancel_closes` | opción | sí/no | Cancelar cierra una elección. |
| `fair_sickness_chance` | opción | sí/no | Probabilidad justa de enfermar. |
| `fair_evolution_average` | opción | sí/no | Media justa de evolución. |
| `fair_move_forgetting` | opción | sí/no | Olvido justo de técnicas. |
| `fair_card_stock` | opción | sí/no | Existencias justas de cartas. |
| `choose_egg_after_lives_lost` | opción | sí/no | Elegir el huevo al perder las vidas. |
| `generous_talk` | opción | sí/no | Conversación generosa. |
| `extra_toilets` | opción | sí/no | Baños extra. |
| `smooth_motion` | opción | sí/no | Movimiento suave. |
| `hd_scenery` | opción | sí/no | Escenarios HD. |
| `scenery_quality` | opción | texto | Calidad de los escenarios: upscaled o smooth. |
| `antialiasing` | opción | sí/no | Antialiasing. |
| `texture_filter` | opción | texto | Filtro de texturas: upscaled, smooth o nearest. |
| `model_characters` | opción | sí/no | Personajes como modelos. |
| `widescreen_pictures` | opción | sí/no | Imágenes panorámicas. |
| `enhanced_lighting` | opción | sí/no | Iluminación mejorada. |
| `vivid_colors` | opción | sí/no | Colores vivos. |
| `shadows` | opción | texto | Sombras: sun, round u off. |
| `chibi_style` | opción | texto | Estilo chibi: off o external. |
| `weather` | opción | sí/no | Clima. |
| `seasons` | opción | sí/no | Estaciones. |
| `music_volume` | opción | número | Volumen de la música. |
| `effects_volume` | opción | número | Volumen de los efectos. |
| `continuous_music` | opción | sí/no | Música continua. |
| `reverb` | opción | sí/no | Reverberación. |
| `translated_signs` | opción | sí/no | Carteles traducidos. |
| `digital_time` | opción | sí/no | La hora en cifras. |
| `save_notice` | opción | sí/no | El aviso de guardado. |
| `need_bubble_stack` | opción | sí/no | Todas las necesidades activas sobre el compañero. |
| `shop_job_bar` | opción | sí/no | La barra del trabajo en la tienda. |
| `name_spacing` | opción | sí/no | Espaciado de los nombres. |
| `free_exploration` | opción | sí/no | Exploración libre. |
| `shop_extras` | opción | sí/no | Extras de las tiendas. |
| `favorite_food` | opción | sí/no | La comida favorita del compañero. |
| `art_fixes` | opción | sí/no | Arreglos de arte. |
| `dev_tools` | opción | sí/no | Herramientas de desarrollo. |
| `entry` | argumento | texto | Al volver del menú del compañero: items o partner, la entrada que recibe el foco en la maquetación original. |

### `ui_notice`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `ui_addon.problem` | ajuste | texto | Por qué no se pudo usar la interfaz elegida; vacío si se pudo. |

### `field_item_notice`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `field_item_notice.text` | ajuste | texto | La clave del texto del aviso de un objeto en el suelo o en un cofre. |
| `field_item_notice.item` | ajuste | texto | La clave del nombre del objeto; vacía para un cofre vacío. |
| `field_item_notice.answer` | ajuste | texto | Si el aviso acepta confirmar ahora: sound, quiet (sin sonido) o vacío (todavía no). |
| `field_item_notice.phase` | ajuste | texto | open mientras se ve; closing cuando el jugador ya contestó y el aviso se cierra. |
| `field.player_x` | ajuste | número | Dónde está el jugador en la pantalla, de 0 a 1 en horizontal (el cuadro crece desde ahí). |
| `field.player_y` | ajuste | número | Dónde está el jugador en la pantalla, de 0 a 1 en vertical. |
| `field_item_notice_ok` | comando | — | El jugador confirma el aviso. |
| `field_item_notice_closed` | comando | — | El cuadro terminó de cerrarse (tras closing). |

### `award_notice`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `award_notice.lines` | ajuste | texto | La clave del texto del aviso de un premio o de un cambio del compañero. |
| `award_notice.color` | ajuste | número | El color del nombre, como número. |
| `award_notice.name` | ajuste | texto | El nombre que muestra el aviso. |
| `award_notice_ok` | comando | — | El jugador confirma el aviso. |

### `fishing_notice`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `fishing_notice.text` | ajuste | texto | La clave del texto de lo pescado. |
| `fishing_notice.item` | ajuste | texto | La clave del nombre de lo pescado. |
| `fishing_notice.size` | ajuste | número | El tamaño de lo pescado. |
| `fishing_notice_ok` | comando | — | El jugador confirma el aviso. |

### `save_indicator`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `save_indicator.text` | ajuste | texto | Lo que dice el indicador de guardado (guardando, cargando...). |
| `save_indicator.opacity` | ajuste | número | Cuánto se ve el indicador, de 0 a 1. |
| `save_indicator.lift` | ajuste | número | Cuánto ha subido el indicador al aparecer, de 0 a 1. |
| `save_indicator.figure` | ajuste | sí/no | Si el indicador enseña la figura que gira. |

### `money_box`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `money_box.money` | ajuste | número | El dinero del jugador. |
| `money_box.bits_key` | ajuste | texto | La clave del nombre de la moneda. |
| `money_box.revision` | ajuste | número | Sube cada vez que cambian los valores de arriba: el cuadro se vuelve a dibujar. |

### `lives_box`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `lives_box.outline` | ajuste | sí/no | Si el cuadro de vidas está abriéndose o cerrándose (solo el borde). |
| `lives_box.grow` | ajuste | número | Cuánto ha crecido el borde, de 0 a 1. |
| `lives_box.anchor_x` | ajuste | número | Desde dónde crece, de 0 a 1 en horizontal (el compañero). |
| `lives_box.anchor_y` | ajuste | número | Desde dónde crece, de 0 a 1 en vertical. |
| `lives_box.lives` | ajuste | número | Las vidas que le quedan al compañero. |
| `lives_box.slots` | ajuste | número | Cuántas vidas caben en el cuadro. |
| `lives_box.lost_full` | ajuste | sí/no | Si la vida que se pierde aún se dibuja llena (parpadea al perderse). |
| `lives_box.revision` | ajuste | número | Sube cada vez que cambian los valores de arriba. |
| `screen_fade` | ajuste | número | Cuánto oscurece el fundido de pantalla, de 0 a 1. |

### `partner_menu`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `partner_menu.start_page` | ajuste | texto | La página con la que se abre el menú: items o status. |
| `partner_menu.context` | ajuste | texto | battle para la bolsa del jugador en combate; vacío en el campo. |
| `partner_menu.extra_presses` | ajuste | número | Las pulsaciones del botón extra mientras el menú está abierto. |
| `partner.revision` | ajuste | número | Sube cada vez que cambian los ajustes del compañero de abajo. |
| `partner.name` | ajuste | texto | La clave del nombre de la especie. |
| `partner.nickname` | ajuste | texto | El nombre del compañero. |
| `partner.age` | ajuste | número | Su edad en días. |
| `partner.weight` | ajuste | número | Su peso. |
| `partner.lives` | ajuste | número | Las vidas que le quedan. |
| `partner.hp` | ajuste | número | Sus PV ahora. |
| `partner.max_hp` | ajuste | número | Sus PV máximos. |
| `partner.mp` | ajuste | número | Sus PM ahora. |
| `partner.max_mp` | ajuste | número | Sus PM máximos. |
| `partner.pool.<n>.<id>` | ajuste | número o texto | La barra de PV o PM: label, value, min, max. |
| `partner.stat.<n>.<id>` | ajuste | número o texto | La barra de una estadística: label, value, min, max. |
| `partner.meter.<n>.<id>` | ajuste | número o texto | La barra de un medidor: label, value, min, max, two_tone. |
| `partner.condition.count` | ajuste | número | Las condiciones de la lista. |
| `partner.condition.<n>.key` | ajuste | texto | La clave de texto de una condición. |
| `partner.tiredness.<id>` | ajuste | número o texto | La barra de cansancio (extras del estado): label, value, min, max. |
| `partner.care_mistakes` | ajuste | número | Errores de cuidado (extras del estado). |
| `partner.battles` | ajuste | número | Combates librados (extras del estado). |
| `partner.lifetime_hours` | ajuste | número | Horas vividas. |
| `partner.evolution.hours` | ajuste | número | Horas desde que cambió de nivel por última vez. |
| `partner.evolution.check_hours` | ajuste | número | Horas a partir de las cuales su nivel comprueba una evolución; -1 si nunca. |
| `partner.awake.<id>` | ajuste | número | Su hora y minuto de despertar y de sueño (extras del estado). |
| `partner.favorite_food` | ajuste | texto | La clave del nombre de su comida favorita. |
| `partner.profile.type` | ajuste | número | Su tipo. |
| `partner.profile.active` | ajuste | número | Su patrón de actividad. |
| `partner.profile.special.<n>` | ajuste | número | Un tipo especial; -1 si ninguno. |
| `partner.technique.<n>.<id>` | ajuste | número o texto | Una técnica equipada: empty, name, power, mp, range, effect. |
| `partner.technique.choice.<n>.<id>` | ajuste | número o texto | Una técnica de la cuadrícula: name, available, equipped, known, power, mp, range, effect, slot, special. |
| `partner.technique.can_finish` | ajuste | sí/no | Si la edición de técnicas puede acabar (queda una técnica de daño). |
| `partner.technique.result` | ajuste | texto | La clave del mensaje del último cambio; vacía si no hay. |
| `partner.care_result` | ajuste | texto | La clave del mensaje de la última felicitación o regañina. |
| `bag.revision` | ajuste | número | Sube cada vez que cambian los ajustes de la bolsa de abajo. |
| `bag.money` | ajuste | número | El dinero del jugador. |
| `bag.slot.count` | ajuste | número | Los huecos de la bolsa. |
| `bag.slot.<n>.<id>` | ajuste | número o texto | Un hueco: empty, name, description, description_species, count, dimmed, usable, discardable, item_color, icon, icon_rect, favorite. |
| `bag.result` | ajuste | texto | La clave del mensaje de la última acción de la bolsa. |
| `bag.result_item` | ajuste | texto | El objeto de la última acción de la bolsa. |
| `field.player_x` | ajuste | número | La posición del jugador en la ventana en porcentaje, desde donde crecen las cajas. |
| `field.player_y` | ajuste | número | La posición del jugador en la ventana en porcentaje, desde donde crecen las cajas. |
| `partner_menu.portrait` | estado | sí/no | Si el juego dibuja en 3D al compañero sobre la página de estado (una mejora). |
| `bag_use` | comando | número | Usa el objeto de un hueco. |
| `bag_drop` | comando | número | Tira el objeto de un hueco. |
| `bag_move` | comando | texto | Intercambia dos huecos: "desde hasta". |
| `bag_sort` | comando | número | Ordena la bolsa en un orden. |
| `partner_toggle_technique` | comando | número | Equipa o quita una técnica por su hueco en la especie. |
| `partner_praise` | comando | — | Felicita al compañero (la pestaña de cuidados). |
| `partner_scold` | comando | — | Regaña al compañero (la pestaña de cuidados). |
| `language` | opción | texto | El idioma, para el texto que la página maqueta a mano. |
| `status_extras` | opción | sí/no | Los extras de la página de estado. |
| `favorite_food` | opción | sí/no | La comida favorita del compañero. |
| `art_fixes` | opción | sí/no | Arreglos de arte. |

### `skin_picker`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `ui_skin` | opción | texto | El aspecto que se ve; al confirmar una fila, el elegido, que se guarda en los ajustes. |
| `ui_skin_preview` | comando | texto | Pone el aspecto de la fila enfocada sin guardarlo. |
| `skin_picker_done` | comando | — | El aspecto está elegido: la portada sigue con «Press Start» (la caja de idiomas ya salió antes). |

### `story`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `story_textbox.revision` | ajuste | número | Sube cada vez que cambia la caja de texto de abajo. |
| `story_textbox.open` | ajuste | sí/no | Si hay una caja de texto abierta. |
| `story_textbox.kind` | ajuste | texto | speech (la caja del que habla) o sized (una caja de su propio tamaño). |
| `story_textbox.columns` | ajuste | número | Las columnas de texto de la caja de su propio tamaño. |
| `story_textbox.rows` | ajuste | número | Las filas de texto de la caja de su propio tamaño. |
| `story_textbox.opening` | ajuste | número | Cuánto se ha abierto la caja, de 0 a 1; el texto se ve en 1. |
| `story_textbox.origin_x` | ajuste | número | Desde dónde crece la caja, en fracción del ancho de la ventana; -1 si aparece sin crecer. |
| `story_textbox.origin_y` | ajuste | número | Desde dónde crece la caja, en fracción del alto de la ventana; -1 si aparece sin crecer. |
| `story_textbox.prompt` | ajuste | sí/no | Si la página espera la confirmación y enseña su aviso. |
| `story_textbox.cursor_row` | ajuste | número | La fila en la que está el cursor de una elección, desde 0; -1 sin elección. |
| `story_textbox.cursor_columns` | ajuste | número | Los caracteres que abarca la barra de una elección; -1 sin elección. |
| `story_textbox.row_count` | ajuste | número | Cuántas filas tiene la página. |
| `story_textbox.top` | ajuste | sí/no | Si la caja va arriba de la ventana en vez de abajo. |
| `story_textbox.row.<n>.runs` | ajuste | número | Cuántos trozos de un color tiene una fila. |
| `story_textbox.row.<n>.run.<n>.text` | ajuste | texto | El texto de un trozo, ya en el idioma del jugador. |
| `story_textbox.row.<n>.run.<n>.color` | ajuste | número | El color de la fuente de un trozo, de 0 a 10. |
| `story_textbox.speaker` | ajuste | texto | El nombre del que habla (también en la primera fila); vacío si no habla nadie. |
| `story_textbox.speaker_kind` | ajuste | texto | Quién habla: none, player, partner, character o special. |
| `story_textbox.options` | ajuste | número | Cuántas opciones tiene la elección que se ve; 0 sin elección. |
| `story_textbox.first_option` | ajuste | número | La fila de la primera opción de la elección. |
| `story_textbox.input` | ajuste | texto | Lo que la caja espera del jugador: page, choice o vacío. |
| `story_textbox.page` | ajuste | número | Cuenta las páginas que han salido: cambia con cada página nueva. |
| `story_advance` | comando | — | Confirma, como el botón de confirmar, mientras la caja espera al jugador. |
| `story_choose` | comando | número | Elige una opción de la elección que se ve, desde 0, como un clic en ella. |
| `story_cancel` | comando | — | Cancela, como el botón de cancelar, mientras la caja espera al jugador. |
| `story.input(): string` | consulta | texto | Lo que la caja espera del jugador: page (su confirmación), choice (una opción) o "" (nada). |
| `story.textbox(): table` | consulta | tabla | La caja: open, kind, size (columns, rows), top, opening, origin, prompt, input, page, speaker (name, kind), rows (cada una con runs: text, color), cursor_row y cursor_columns. |
| `story.choice(): table?` | consulta | tabla | La elección que se ve: options (text y row de cada una) y cursor (desde 0); nil sin elección. |
| `story.advance(): boolean, string?` | acción | sí/no | Pide confirmar: la página sigue o la elección toma la opción del cursor. |
| `story.choose(option: integer): boolean, string?` | acción | sí/no | Pide elegir una opción de la elección, desde 0. |
| `story.cancel(): boolean, string?` | acción | sí/no | Pide cancelar, como el botón de cancelar. |
| `story.changed` | evento | tabla | Cambió la caja: se abrió, creció, otra página, el cursor. Sale de `story_textbox.revision`. |
| `story.text` | evento | tabla | Sale una página nueva. Sale de `story_textbox.page`. |
| `story.input` | evento | tabla | La caja empezó o dejó de esperar al jugador. Sale de `story_textbox.input`. |

### `dialog`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `dialog.revision` | ajuste | número | Sube cada vez que cambia el diálogo de abajo. |
| `dialog.text` | ajuste | texto | La clave de texto de lo que dice el diálogo. |
| `dialog.file` | ajuste | texto | El archivo de guardado del que habla, o vacío. |
| `dialog.question` | ajuste | sí/no | Si es una pregunta (sí o no); si no, un aviso (aceptar). |
| `dialog.start_on_yes` | ajuste | sí/no | Si la pregunta empieza en sí, como la de la partida nueva del original. |
| `dialog.at_title` | ajuste | sí/no | Si lo enseña la portada. |
| `dialog_answer` | comando | sí/no | La respuesta del diálogo: verdadero solo para sí. |
| `dialog.state(): table` | consulta | tabla | El diálogo: text_key, file, question, start_on_yes, at_title y options. |
| `dialog.options(): table` | consulta | tabla | Sus opciones en orden (id, label_key, yes): sí y no en una pregunta, aceptar en un aviso. |
| `dialog.is_open(): boolean` | consulta | sí/no | Si el diálogo se ve, esperando su respuesta. |
| `dialog.answer(option: integer): boolean, string?` | acción | sí/no | Responde con una de sus opciones, desde 0. |
| `dialog.cancel(): boolean, string?` | acción | sí/no | Cancela: no a una pregunta, aceptar a un aviso. |
| `dialog.changed` | evento | tabla | El juego puso el texto de un diálogo. Sale de `dialog.revision`. |

### `naming`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `naming_box.revision` | ajuste | número | Sube cada vez que cambia la caja de nombre de abajo. |
| `naming_box.title` | ajuste | texto | La clave de texto de a quién se le pone el nombre. |
| `naming_box.length` | ajuste | número | Las letras que puede tener el nombre. |
| `naming_box.name.<n>` | ajuste | texto | Una letra del nombre escrito; vacía si no hay. |
| `naming_box.rows` | ajuste | número | Las filas de la rejilla de letras. |
| `naming_box.columns` | ajuste | número | Las columnas de la rejilla de letras. |
| `naming_box.cell.<n>.<n>` | ajuste | texto | La letra de una celda de la rejilla; un espacio si está vacía. |
| `naming_box.cursor_row` | ajuste | número | La fila del cursor en la rejilla; -1 en un botón. |
| `naming_box.cursor_column` | ajuste | número | La columna del cursor en la rejilla; -1 en un botón. |
| `naming_box.cursor_button` | ajuste | número | El botón del cursor: 0 Atrás, 1 Aceptar; -1 en la rejilla. |
| `naming_write` | comando | texto | Escribe las letras de la rejilla que lleva, como el teclado. |
| `naming_erase` | comando | — | Quita la última letra, como cancelar. |
| `naming_accept` | comando | — | Aceptar: el juego toma el nombre si no está vacío. |
| `naming.is_open(): boolean` | consulta | sí/no | Si la caja de nombre se ve, esperando un nombre. |
| `naming.state(): table` | consulta | tabla | La caja: title_key, length, letters, name, grid (rows, columns) y cursor (row, column, button). |
| `naming.cell(row: integer, column: integer): string` | consulta | texto | La letra de una celda de la rejilla, desde 0; " " si está vacía. |
| `naming.write(letters: string): boolean, string?` | acción | sí/no | Pide escribir letras de la rejilla al final del nombre. |
| `naming.erase(): boolean, string?` | acción | sí/no | Pide quitar la última letra. |
| `naming.accept(): boolean, string?` | acción | sí/no | Pide aceptar el nombre; el juego no toma uno vacío. |
| `naming.changed` | evento | tabla | Cambió el nombre, el cursor o la rejilla. Sale de `naming_box.revision`. |

### `clock`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `clock.now(): table` | consulta | tabla | La hora: year, day, hour, minute, night, phase (morning, day, late, night) y running (si el tiempo del juego corre). |
| `clock.changed` | evento | tabla | Cambió el estado del campo, la hora incluida. Sale de `hud.revision`. |

### `field`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `field.fade(): number` | consulta | número | Cuánto oscurece el fundido del cambio de mapa, de 0 a 1. |
| `field.map_title(): string?` | consulta | texto | La clave de texto del nombre del mapa que se enseña al llegar, o nil. |
| `field.minimap(): table` | consulta | tabla | El minimapa: shown (la opción y el campo lo dejan), covered (una ventana lo tapa) y size. |
| `field.player(): table` | consulta | tabla | Dónde está el jugador en la ventana: x e y en porcentaje. |
| `field.shop_job(): table?` | consulta | tabla | La cuenta del trabajo en la tienda mientras dura (tally, goal, better_pay, customer, customers), o nil. |
| `field.fade` | evento | tabla | Cambió el fundido. Sale de `screen_fade`. |
| `field.map_title` | evento | tabla | Cambió el nombre del mapa que se enseña. Sale de `map_title_key`. |
| `field.minimap` | evento | tabla | Cambió si el minimapa puede verse o si algo lo tapa. Sale de `hud.minimap_allowed hud.minimap_covered battle.active`. |
| `field.shop_job` | evento | tabla | Cambió el trabajo en la tienda. Sale de `shop_job.revision`. |

### `partner`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `partner.status(): table` | consulta | tabla | Las barras (meters: id, label_key, value, min, max, two_tone, mood), los PV y PM (pools) y lo que le parece el mapa (area: liked, disliked o nil). |
| `partner.needs(): table` | consulta | tabla | Las necesidades en orden: id, label_key, active y urgency (1 la más urgente; 0 si no la tiene). |
| `partner.bubble(): table` | consulta | tabla | Dónde está su cabeza en la ventana: visible, x, y (porcentaje) y evolving (el cuadro de la burbuja de evolución, o nil). |
| `partner.extras(): table?` | consulta | tabla | Errores de cuidado, combates y horas vividas; nil si el jugador tiene apagados los extras del estado. |
| `partner.praise(): boolean` | acción | sí/no | Pide felicitarlo. |
| `partner.scold(): boolean` | acción | sí/no | Pide regañarlo. |
| `partner.toggle_technique(slot: integer): boolean, string?` | acción | sí/no | Pide equipar o quitar la técnica de ese hueco de la especie. |
| `partner.changed` | evento | tabla | Cambió algo del compañero; name dice qué ajuste. Sale de `hud.revision partner.revision hud.meter.0.label hud.meter.1.label hud.pool.0.label hud.pool.1.label hud.need.0.label hud.need.1.label hud.need.2.label hud.need.3.label hud.need.4.label hud.need.5.label`. |
| `partner.bubble_moved` | evento | tabla | Su cabeza se movió en la ventana. Sale de `hud.partner.revision`. |

### `bag`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `bag.count(): integer` | consulta | número | Los huecos de la bolsa. |
| `bag.money(): integer` | consulta | número | El dinero del jugador. |
| `bag.field(index: integer, name: string): any` | consulta | número o texto | Un dato de un hueco, o su valor por defecto. |
| `bag.slot(index: integer): table` | consulta | tabla | Un hueco: empty, name, description, description_species, count, dimmed, usable, discardable, item_color, icon, icon_rect, favorite. |
| `bag.result(): table?` | consulta | tabla | La respuesta a la última acción: key (clave del mensaje) e item; nil antes de ninguna. |
| `bag.use(index: integer): boolean, string?` | acción | sí/no | Pide usar el objeto de un hueco. |
| `bag.drop(index: integer): boolean, string?` | acción | sí/no | Pide tirar el objeto de un hueco. |
| `bag.move(from: integer, to: integer): boolean, string?` | acción | sí/no | Pide intercambiar dos huecos. |
| `bag.sort(order: integer): boolean, string?` | acción | sí/no | Pide ordenar la bolsa en un orden. |
| `bag.changed` | evento | tabla | Cambió la bolsa. Sale de `bag.revision`. |
| `bag.result` | evento | tabla | El juego respondió a una acción de la bolsa. Sale de `bag.result`. |

### `player`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `player.profile(): table` | consulta | tabla | El jugador: name, level, bits, seconds, raised, medals, items, cards. |
| `player.medals(): table` | consulta | tabla | Las medallas que publica el juego (no un número fijo): held y name_key. |
| `player.city(): table` | consulta | tabla | La ciudad: recruited, total y prosperity. |

### `options`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `options.list(): table` | consulta | tabla | Las opciones del juego en orden: name, kind (toggle, list, range), label_key, help_key, tab, default, original, values, texts, min, max. |
| `options.get(name: string): any` | consulta | número o texto | El valor de una opción ahora (o su valor por defecto); nil si no existe. |
| `options.set(name: string, value: any): boolean, string?` | acción | sí/no | Pide dar un valor a una opción; comprueba el tipo y los valores que admite. |
| `options.changed` | evento | tabla | Cambió una opción; name dice cuál y value su valor. Sale de `@options`. |

### `battle`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `battle.revision` | ajuste | número | Sube cada vez que cambian los luchadores, la tira de órdenes, el golpe final o la bolsa. |
| `battle.phase` | ajuste | texto | La fase: intro (la apertura), fight, victory (las poses), end (la caja del final) o closing (la tira sube). |
| `battle.frame_seconds` | ajuste | número | Lo que dura un cuadro del combate, para el ritmo de la caja del final. |
| `battle.fast` | ajuste | sí/no | Si el combate va al doble de velocidad. |
| `battle.menu.count` | ajuste | número | Cuántas órdenes tiene la tira. |
| `battle.menu.<n>.command` | ajuste | número | El número de la orden (1 huir... 8 a 10 las técnicas, 11 el golpe final). |
| `battle.menu.<n>.label` | ajuste | texto | La clave de texto del nombre de la orden o de la técnica. |
| `battle.menu.<n>.icon` | ajuste | texto | El icono de la orden en las hojas del aspecto; vacío para el golpe final. |
| `battle.menu.<n>.available` | ajuste | sí/no | Si la orden se puede dar ahora. |
| `battle.menu.<n>.current` | ajuste | sí/no | Si es la orden que sigue el compañero ahora. |
| `battle.menu.<n>.mp_cost` | ajuste | número | Los PM que le cuesta la técnica al compañero ahora; 0 en las demás órdenes. |
| `battle.menu.motion` | ajuste | número | Sube cada vez que la tira o las barras del compañero se mueven, o el icono bajo el cursor parpadea. |
| `battle.menu.top` | ajuste | número | El borde de arriba de la tira, en líneas desde el centro de la pantalla. |
| `battle.menu.bottom` | ajuste | número | El borde de abajo de la tira. |
| `battle.menu.blink` | ajuste | sí/no | Si el icono bajo el cursor enseña su segundo cuadro. |
| `battle.bars.hp_x` | ajuste | número | El borde izquierdo de la barra de PV del compañero mientras entra o sale. |
| `battle.bars.mp_x` | ajuste | número | El borde izquierdo de la barra de PM del compañero mientras entra o sale. |
| `battle.fighter.count` | ajuste | número | Cuántos luchadores hay: el compañero (0) y los enemigos. |
| `battle.fighter.<n>.name` | ajuste | texto | La clave de texto de su nombre. |
| `battle.fighter.<n>.status` | ajuste | texto | La clave de texto de su estado alterado, o vacío. |
| `battle.fighter.<n>.hp` | ajuste | número | Los PV de un luchador. |
| `battle.fighter.<n>.max_hp` | ajuste | número | Los PV máximos de un luchador. |
| `battle.fighter.<n>.mp` | ajuste | número | Los PM de un luchador. |
| `battle.fighter.<n>.max_mp` | ajuste | número | Los PM máximos de un luchador. |
| `battle.fighter.<n>.fainted` | ajuste | sí/no | Si ha caído. |
| `battle.fighter.<n>.hp_bar` | ajuste | sí/no | Si su barra de PV se ve ahora (la de un enemigo, un rato tras recibir daño). |
| `battle.finisher.progress` | ajuste | número | Lo que lleva cargado el indicador del golpe final. |
| `battle.finisher.goal` | ajuste | número | Lo que tiene que cargar para estar lleno. |
| `battle.finisher.ready` | ajuste | sí/no | Si el golpe final está listo. |
| `battle.finisher.charging` | ajuste | sí/no | Si dura el aura en la que cada pulsación carga el golpe final. |
| `battle.finisher.charge` | ajuste | número | La carga del aura, en porcentaje. |
| `battle.revive_seconds` | ajuste | número | La cuenta para revivir al compañero caído; -1 sin cuenta. |
| `battle.match_seconds` | ajuste | número | El reloj de un combate de copa; -1 en los demás combates. |
| `battle.bag.available` | ajuste | sí/no | Si la bolsa se puede abrir. |
| `battle.bag.open` | ajuste | sí/no | Si la bolsa está abierta sobre el combate. |
| `battle.item_in_flight` | ajuste | sí/no | Si un objeto lanzado va de camino al compañero. |
| `battle.end.outcome` | ajuste | texto | La clave de texto del resultado en la caja del final. |
| `battle.end.count` | ajuste | número | Cuántas líneas tiene la caja del final. |
| `battle.end.<n>.parts` | ajuste | texto | Las piezas de una línea, separadas por |: claves de texto o marcas entre llaves. |
| `battle.end.<n>.name` | ajuste | texto | La clave de texto del nombre de la línea ({name}). |
| `battle.end.<n>.subject` | ajuste | texto | La clave de texto de a quién se refiere ({subject}). |
| `battle.end.<n>.amount` | ajuste | número | El número de la línea ({amount}). |
| `battle.end.stat.<n>.value` | ajuste | número | Un atributo antes de la subida (PV, PM, ataque, defensa, velocidad, cerebro). |
| `battle.end.stat.<n>.max` | ajuste | número | El tope de ese atributo. |
| `battle.end.gain.<n>.value` | ajuste | número | Lo que sube ese atributo. |
| `battle.end.bits` | ajuste | número | Los bits ganados. |
| `battle.end.money` | ajuste | número | El dinero con los bits ya dentro. |
| `battle.end.count_delay_frames` | ajuste | número | Los cuadros antes de que los atributos empiecen a subir. |
| `battle.end.wait_frames` | ajuste | número | Los cuadros que espera cada línea de la caja del final. |
| `battle.intro.revision` | ajuste | número | Sube cada vez que cambia la apertura del combate. |
| `battle.intro.step` | ajuste | texto | El paso de la apertura: anger, banner, burst o done. |
| `battle.intro.anger.<n>.shown` | ajuste | sí/no | Si se ve la marca de enfado sobre un enemigo. |
| `battle.intro.anger.<n>.x` | ajuste | número | Dónde está esa marca, en porcentaje del ancho. |
| `battle.intro.anger.<n>.y` | ajuste | número | Dónde está esa marca, en porcentaje del alto. |
| `battle.intro.anger.scale` | ajuste | número | El tamaño de las marcas, en porcentaje. |
| `battle.intro.flee.<n>.shown` | ajuste | sí/no | Si se ve la marca sobre un personaje que huye. |
| `battle.intro.flee.<n>.x` | ajuste | número | Dónde está, en porcentaje del ancho. |
| `battle.intro.flee.<n>.y` | ajuste | número | Dónde está, en porcentaje del alto. |
| `battle.intro.flash` | ajuste | número | El destello del cartel, de 0 a 2. |
| `battle.intro.pieces` | ajuste | texto | Las piezas del cartel: siete números cada una (x, y, ancho, alto, textura, estela, izquierda). |
| `battle.damage.revision` | ajuste | número | Sube cada vez que cambian los números sobre los luchadores. |
| `battle.damage.numbers` | ajuste | texto | Los números sobre los luchadores: siete valores cada uno (x, y, dx, dy, valor, color, icono). |
| `battle.hp_bars.revision` | ajuste | número | Sube cada vez que cambian las barras de vida de los enemigos, las marcas o la cuenta del compañero caído. |
| `battle.hp_bars.always` | ajuste | sí/no | Si las barras de vida de los enemigos se ven todo el combate. |
| `battle.hp_bars` | ajuste | texto | Las barras de vida de los enemigos: nueve valores cada una. |
| `battle.markers` | ajuste | texto | Las marcas de 1P y COM de un combate de copa entre dos de la misma especie: tres valores cada una. |
| `battle.death_countdown` | ajuste | texto | La cuenta sobre el compañero caído: siete valores, o vacío. |
| `battle.shout.revision` | ajuste | número | Sube cada vez que cambia el grito del jugador. |
| `battle.shout.shown` | ajuste | sí/no | Si se ve el grito del jugador. |
| `battle.shout.text` | ajuste | texto | La clave de texto del grito. |
| `battle.shout.anchor_x` | ajuste | número | Dónde se ancla el grito, en porcentaje del ancho. |
| `battle.shout.anchor_y` | ajuste | número | Dónde se ancla el grito, en porcentaje del alto. |
| `battle.shout.node_x` | ajuste | número | El punto del compañero desde el centro de la pantalla. |
| `battle.shout.node_y` | ajuste | número | El punto del compañero desde el centro de la pantalla. |
| `battle.shout.y` | ajuste | número | La línea del grito. |
| `battle.shout.hop` | ajuste | número | El salto del grito. |
| `battle.shout.scale` | ajuste | número | El tamaño del grito. |
| `battle_command` | comando | número | Da una orden al compañero (su número, battle.menu.<n>.command). Un addon, solo en la fase fight. |
| `battle_bag_open` | comando | — | Abre la bolsa del jugador. Un addon, solo en la fase fight. |
| `battle_bag_close` | comando | — | Cierra la bolsa del jugador y vuelve al combate. Un addon, solo con la bolsa abierta. |
| `battle_charge` | comando | — | Una pulsación de la carga del golpe final. Un addon, solo en la fase fight. |
| `battle_close` | comando | — | Cierra la caja del final del combate. Un addon, solo en la fase end. |
| `battle_end_count` | comando | — | Un paso de la cuenta de los bits del final del combate. Un addon, solo en la fase end. |
| `battle_end_counted` | comando | — | La cuenta de los bits del final del combate ha terminado. Un addon, solo en la fase end. |
| `battle.state(): table` | consulta | tabla | El combate: phase, active, fast, frame_seconds, bag (available, open, item_in_flight), revive_seconds y match_seconds (o nil) y cup (un combate de copa). |
| `battle.fighters(): table` | consulta | tabla | Los luchadores, el compañero primero: name_key, status_key, hp, max_hp, mp, max_mp, fainted y hp_bar. |
| `battle.commands(): table` | consulta | tabla | La tira de órdenes, el golpe final primero: command, label_key, icon, available, current y mp_cost. |
| `battle.finisher(): table` | consulta | tabla | El golpe final: progress, goal, ready, charging (el aura) y charge (en porcentaje). |
| `battle.strip(): table` | consulta | tabla | Dónde están la tira y las barras del compañero al entrar y salir: top, bottom, blink, hp_x y mp_x (o nil). |
| `battle.intro(): table` | consulta | tabla | La apertura: step, anger (3 marcas), flee (8), anger_scale, flash y pieces (las piezas del cartel, con key). |
| `battle.numbers(): table` | consulta | tabla | Los números sobre los luchadores: x, y (porcentaje), dx, dy, value, kind (damage, gain, poison) e icon. |
| `battle.hp_bars(): table` | consulta | tabla | Las barras de vida de los enemigos: always y bars (enemy, x, y, left, top, fill, red, green, hp). |
| `battle.markers(): table` | consulta | tabla | Las marcas de 1P y COM: fighter (0 el compañero, 1 el rival), x e y (porcentaje). |
| `battle.death_countdown(): table?` | consulta | tabla | La cuenta sobre el compañero caído (x, y, digit, digit_scale, badge_scale, turn, palette), o nil. |
| `battle.shout(): table?` | consulta | tabla | El grito del jugador (text_key, x, y, node_x, node_y, line, hop, scale), o nil. |
| `battle.result(): table` | consulta | tabla | La caja del final: outcome_key, lines (parts, name_key, subject_key, amount), stats (value, max, gain), bits, money, count_delay_frames y wait_frames. |
| `battle.match(): table` | consulta | tabla | Lo que un combate de copa enseña encima: shown, name, name_y, stats, counted, stat, time_up, time_up_label, box, partner, rival, damage_label, dealt, taken y wipe. |
| `battle.command(command: integer): boolean, string?` | acción | sí/no | Pide que el compañero siga una orden de la tira (su command); solo en la fase fight. |
| `battle.charge(): boolean, string?` | acción | sí/no | Una pulsación de la carga del golpe final; solo en la fase fight. |
| `battle.open_bag(): boolean, string?` | acción | sí/no | Pide abrir la bolsa sobre el combate (la pantalla partner_menu); solo en la fase fight. |
| `battle.close_bag(): boolean, string?` | acción | sí/no | Pide cerrar la bolsa y volver al combate. |
| `battle.close(): boolean, string?` | acción | sí/no | La caja del final ha terminado: el combate se cierra; solo en la fase end. |
| `battle.end_count(): boolean, string?` | acción | sí/no | Un punto o un bit contado en la caja del final (su sonido); solo en la fase end. |
| `battle.end_counted(): boolean, string?` | acción | sí/no | Todos los bits dentro (el sonido que acaba la cuenta); solo en la fase end. |
| `battle.started` | evento | tabla | Empieza un combate (battle.active pasa a verdadero). Sale de `battle.active`. |
| `battle.ended` | evento | tabla | Se acaba un combate, tras cerrar la caja del final (battle.active pasa a falso). Sale de `battle.active`. |
| `battle.phase` | evento | tabla | Cambió la fase; value dice cuál. Sale de `battle.phase`. |
| `battle.changed` | evento | tabla | Cambiaron los luchadores, la tira, el golpe final, la bolsa o las cuentas. Sale de `battle.revision`. |
| `battle.speed` | evento | tabla | El combate pasó a ir rápido o a velocidad normal. Sale de `battle.fast`. |
| `battle.strip_moved` | evento | tabla | La tira o las barras del compañero se movieron, o el icono bajo el cursor parpadeó. Sale de `battle.menu.motion`. |
| `battle.intro` | evento | tabla | Cambió la apertura. Sale de `battle.intro.revision`. |
| `battle.numbers` | evento | tabla | Cambiaron los números sobre los luchadores. Sale de `battle.damage.revision`. |
| `battle.overlays` | evento | tabla | Cambiaron las barras de vida de los enemigos, las marcas de 1P y COM o la cuenta del compañero caído. Sale de `battle.hp_bars.revision battle.hp_bars.always`. |
| `battle.shout` | evento | tabla | Cambió el grito del jugador. Sale de `battle.shout.revision`. |
| `battle.match` | evento | tabla | Cambió lo que un combate de copa enseña encima. Sale de `tournament_match.revision`. |

### `tournament_match`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `tournament_match.revision` | ajuste | número | Sube cada vez que cambia lo de abajo. |
| `tournament_match.shown` | ajuste | sí/no | Si se ve. |
| `tournament_match.name` | ajuste | texto | El nombre del luchador de la presentación, lo que ha entrado de él (ya en el idioma). |
| `tournament_match.name_y` | ajuste | número | La fila del nombre. |
| `tournament_match.stats` | ajuste | sí/no | Si se ve la caja de atributos. |
| `tournament_match.counted` | ajuste | sí/no | Si se ven sus números. |
| `tournament_match.stat.<n>.label` | ajuste | texto | El nombre de un atributo. |
| `tournament_match.stat.<n>.value` | ajuste | número | El valor de un atributo. |
| `tournament_match.stat.<n>.width` | ajuste | número | El ancho de su barra. |
| `tournament_match.time_up` | ajuste | sí/no | Si se ve el aviso de fin de tiempo. |
| `tournament_match.time_up_label` | ajuste | texto | El texto de ese aviso. |
| `tournament_match.box` | ajuste | sí/no | Si se ve la caja del daño. |
| `tournament_match.partner` | ajuste | texto | El nombre del compañero. |
| `tournament_match.rival` | ajuste | texto | El nombre del rival. |
| `tournament_match.damage_label` | ajuste | texto | El título de la caja del daño. |
| `tournament_match.dealt` | ajuste | número | El daño hecho. |
| `tournament_match.taken` | ajuste | número | El daño recibido. |
| `tournament_match.wipe.<n>` | ajuste | número | La altura de una de las 32 columnas de la cortina, de 0 a 240. |

### `versus`

| Nombre | Tipo | Valor | Qué es |
|---|---|---|---|
| `versus.revision` | ajuste | número | Sube cada vez que cambia el versus. |
| `versus.view` | ajuste | texto | La vista: menu, host_setup, hosting, join_entry, joining, save_pick, lineup, waiting_lineup, match, intermission, result o error. |
| `versus.cursor` | ajuste | número | La fila con el foco, o en un combate la orden señalada. |
| `versus.battles` | ajuste | número | Cuántos combates tiene la serie. |
| `versus.choice` | ajuste | número | La fila de la duración de serie elegida. |
| `versus.choices` | ajuste | número | Cuántas duraciones de serie hay. |
| `versus.arena` | ajuste | número | El número del escenario elegido (su texto es versus.arena.<n>). |
| `versus.arena_choice` | ajuste | número | La fila del escenario elegido. |
| `versus.arenas` | ajuste | número | Cuántos escenarios hay. |
| `versus.code` | ajuste | texto | El código de la sala; vacío hasta que hay uno. |
| `versus.stage` | ajuste | texto | Cómo va la conexión: creating, waiting o connecting. |
| `versus.typed` | ajuste | texto | El código escrito hasta ahora. |
| `versus.bad_entry` | ajuste | sí/no | Si el último código no valía. |
| `versus.saves` | ajuste | número | Cuántas partidas hay para elegir. |
| `versus.save.<n>.player` | ajuste | texto | El nombre del jugador de la partida. |
| `versus.save.<n>.nickname` | ajuste | texto | El nombre del compañero. |
| `versus.save.<n>.partners` | ajuste | número | Cuántos compañeros tiene. |
| `versus.save.<n>.usable` | ajuste | sí/no | Si tiene compañeros para toda la serie. |
| `versus.partners` | ajuste | número | Cuántos compañeros hay para elegir. |
| `versus.partner.<n>.name` | ajuste | texto | El nombre del compañero. |
| `versus.partner.<n>.species` | ajuste | texto | La clave de texto de su especie. |
| `versus.partner.<n>.stats` | ajuste | texto | Sus atributos en un texto. |
| `versus.partner.<n>.picked` | ajuste | sí/no | Si ya está elegido. |
| `versus.picks` | ajuste | número | Cuántos compañeros van elegidos. |
| `versus.pick.<n>.name` | ajuste | texto | El nombre de un elegido, en orden. |
| `versus.local` | ajuste | número | El lado del jugador: 0 el del anfitrión, 1 el del invitado. |
| `versus.round` | ajuste | número | El combate que se ve, desde 1. |
| `versus.wins.<n>` | ajuste | número | Las victorias de cada lado (0 y 1). |
| `versus.clock` | ajuste | número | Los segundos del reloj del combate. |
| `versus.stalled` | ajuste | sí/no | Si el rival lleva un rato sin mandar nada. |
| `versus.leave_asked` | ajuste | sí/no | Si otra cancelación deja el versus. |
| `versus.blink` | ajuste | sí/no | Si el icono señalado enseña su segundo cuadro. |
| `versus.f.<n>.hp` | ajuste | número | Los PV de un lado (0 y 1). |
| `versus.f.<n>.max_hp` | ajuste | número | Sus PV máximos. |
| `versus.f.<n>.mp` | ajuste | número | Sus PM. |
| `versus.f.<n>.max_mp` | ajuste | número | Sus PM máximos. |
| `versus.f.<n>.segments` | ajuste | número | Las letras encendidas del golpe final, de 0 a 6. |
| `versus.f.<n>.finisher_ready` | ajuste | sí/no | Si el golpe final está listo. |
| `versus.f.<n>.menu` | ajuste | número | Cuántas órdenes tiene su tira. |
| `versus.f.<n>.hover` | ajuste | número | La orden señalada en su tira (la del rival, su orden de ahora). |
| `versus.f.<n>.menu.<n>.icon` | ajuste | texto | El icono de una orden de la tira; vacío para el golpe final. |
| `versus.f.<n>.command_name` | ajuste | texto | La clave de texto de la orden señalada, o vacío. |
| `versus.log` | ajuste | número | Cuántas líneas tiene lo que ha pasado. |
| `versus.log.<n>.key` | ajuste | texto | La clave de texto de la línea. |
| `versus.log.<n>.who` | ajuste | texto | Quién. |
| `versus.log.<n>.what` | ajuste | texto | La clave de texto del movimiento, o vacío. |
| `versus.log.<n>.amount` | ajuste | número | Cuánto. |
| `versus.results` | ajuste | número | Cuántos combates se han jugado. |
| `versus.result.<n>.outcome` | ajuste | texto | Cómo le fue al jugador: won, lost o draw. |
| `versus.winner` | ajuste | texto | Quién ganó la serie al acabar: you, rival o draw. |
| `versus.error_key` | ajuste | texto | La clave de texto de por qué paró. |
| `versus.error_detail` | ajuste | texto | El error en sus propias palabras. |
| `versus.show` | ajuste | texto | Lo que enseña el escenario: banner, fight, ending, score o nada; no está sin escenario. |
| `versus.score.round` | ajuste | número | La fila de la pantalla de marcador que se mueve. |
| `versus.score.offset` | ajuste | número | Cuánto se ha movido, en píxeles del original. |
| `versus.score.flash` | ajuste | sí/no | Si se ve el destello de la fila. |
| `versus.score.black` | ajuste | número | La opacidad del fundido a negro, de 0 a 1. |
| `versus.score.results` | ajuste | número | Cuántos combates tienen resultado. |
| `versus.score.result.<n>` | ajuste | texto | Quién ganó un combate: host, guest o draw. |
| `versus.score.wins.<n>` | ajuste | número | Las victorias de cada lado. |
| `versus.score.losses.<n>` | ajuste | número | Las derrotas de cada lado. |
| `versus.score.face.<n>.<n>` | ajuste | número | La cara del luchador de un lado en un combate (por el número de su especie). |
| `versus_action` | comando | texto | Un paso en la vista: up, down, left, right, confirm, cancel, menu, tab_previous, tab_next, finisher, o pick:<fila> para ir a una fila y confirmarla. |
| `versus.state(): table` | consulta | tabla | La vista, el cursor, los combates de la serie y, en un combate: round, clock, side (el lado del jugador), wins, stalled, leave_asked, blink y arena (lo que enseña el escenario, o nil sin él). |
| `versus.setup(): table` | consulta | tabla | La preparación del anfitrión: series y series_count, arena, arena_row y arenas. |
| `versus.connection(): table` | consulta | tabla | La conexión: stage (creating, waiting, connecting) y code (el código de la sala). |
| `versus.entry(): table` | consulta | tabla | El código escrito (typed) y si el último no valía (bad). |
| `versus.saves(): table` | consulta | tabla | Las partidas: player, nickname, partners y usable. |
| `versus.lineup(): table` | consulta | tabla | Los compañeros (name, species_key, stats, picked) y los elegidos (picks, sus nombres). |
| `versus.fighters(): table` | consulta | tabla | Los dos lados, el anfitrión primero: hp, max_hp, mp, max_mp, segments, finisher_ready, hover, icons (la tira) y command_key (la orden señalada). |
| `versus.log(): table` | consulta | tabla | Lo que ha pasado, lo último al final: key, who, what_key y amount. |
| `versus.results(): table` | consulta | tabla | Los combates jugados (rounds: won, lost, draw) y el ganador de la serie (winner). |
| `versus.score(): table` | consulta | tabla | La pantalla de marcador: round, offset, flash, black, results y sides (wins, losses, faces). |
| `versus.error(): table` | consulta | tabla | Por qué paró: key (clave de texto) y detail. |
| `versus.step(step: string): boolean, string?` | acción | sí/no | Un paso en la vista: up, down, left, right, confirm, cancel, menu, tab_previous, tab_next o finisher (el golpe final, con su medidor lleno). |
| `versus.pick(row: integer): boolean, string?` | acción | sí/no | Va a una fila de la vista (o a una orden de la tira del jugador) y la confirma. |
| `versus.changed` | evento | tabla | Cambió el versus. Sale de `versus.revision`. |

<!-- /ui-api:generated -->
