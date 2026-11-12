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
`options`) que cualquier documento usa igual, sea de la interfaz de serie o de otra:

- Una **consulta** lee el juego y devuelve una tabla nueva: `partner.status()`, `bag.slot(3)`, `clock.now()`.
  Cambiar esa tabla no cambia nada del juego.
- Una **acción** pide algo al juego: `bag.use(3)`, `options.set("digital_time", false)`. Comprueba sus argumentos y
  devuelve `true`, o `false` y por qué; el juego vuelve a comprobarlo y responde con un evento.
- Un **evento** llega a las funciones que el documento registra con `hooks.on("bag.changed", función)`. La función
  recibe una tabla con `version` (la de la tabla del evento), `name` (el ajuste que cambió) y `value`, y el documento.
  `hooks.off` la quita; un documento registra como mucho 64.

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
HUD, el menú de pausa, el menú del compañero y la caja del aspecto del primer arranque; el resto de pantallas llega
después.

## Referencia

<!-- ui-api:generated -->
Versión de la API de la interfaz: **0.11.0**.

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
| `skin_picker_done` | comando | — | El aspecto está elegido: la portada sigue con la caja de idiomas. |

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

<!-- /ui-api:generated -->
