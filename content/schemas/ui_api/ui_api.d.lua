---@meta
-- The UI API 0.12.0 (docs/modding/ui-api.md) for the Lua language server. The game
-- tests write it with DWE_WRITE_API_DOC=1: do not edit it by hand.

ui = {}

--- El valor de un ajuste que el juego publica; nil si no lo publica.
---@param name string
---@return any
function ui.setting(name) end

--- Manda un comando al juego, que lo atiende una vez.
---@param name string
---@param value any?
function ui.send(name, value) end

--- Cuenta un estado al juego, que lee el último valor cuando lo necesita.
---@param name string
---@param value any?
function ui.report(name, value) end

--- Enseña una pantalla; modal toma la navegación. args llega a su on_show.
---@param screen string
---@param modal boolean?
---@param args table?
function ui.show(screen, modal, args) end

--- Esconde una pantalla.
---@param screen string
function ui.hide(screen) end

--- Si una pantalla se ve.
---@param screen string
---@return boolean
function ui.is_visible(screen) end

--- El valor que el aspecto da a una opción de disposición en esta pantalla; nil sin él.
---@param option string
---@return string?
function ui.layout(option) end

--- El nombre del control que hace una acción en el dispositivo en uso.
---@param action string
---@return string
function ui.prompt(action) end

--- El dispositivo en uso: keyboard_mouse o gamepad.
---@return string
function ui.device() end

--- La familia del mando en uso.
---@return string
function ui.gamepad_family() end

--- Los idiomas que el jugador puede elegir.
---@return string[]
function ui.languages() end

--- Los aspectos que el jugador puede elegir.
---@return string[]
function ui.skins() end

--- La clave de texto del nombre de un aspecto; vacía si no existe.
---@param id string
---@return string
function ui.skin_name_key(id) end

--- Suena box_open, box_close o confirm; false con otro nombre.
---@param name string
---@return boolean
function ui.cue(name) end

--- El texto de una clave en el idioma del jugador, con sus huecos rellenos.
---@param key string
---@param args table?
---@return string
function t(key, args) end

--- El nombre con el que la interfaz cargó el documento.
---@type string
document_name = nil

hooks = {}

--- Llama a handler cada vez que llega el evento; hasta 64 por documento.
---@param event string
---@param handler fun(event: table, document: Element)
function hooks.on(event, handler) end

--- Deja de llamar a handler con ese evento.
---@param event string
---@param handler function
function hooks.off(event, handler) end

--- Lo llama la interfaz al enseñar el documento.
---@param document Element
---@param args table
function on_show(document, args) end

--- Lo llama la interfaz al esconder el documento.
---@param document Element
function on_hide(document) end

--- Lo llama la interfaz cuando cambia un ajuste, con el documento a la vista.
---@param document Element
---@param name string
function on_setting(document, name) end

--- Una acción de navegación (confirm, cancel, menu...); true si el documento la atiende.
---@param document Element
---@param action string
---@return boolean?
function on_action(document, action) end

--- Cada frame con el documento a la vista.
---@param document Element
---@param seconds number
function on_tick(document, seconds) end

--- Cambió el dispositivo en uso.
---@param document Element
function on_device(document) end

--- Cambió el idioma.
---@param document Element
function on_language(document) end

---@class Element
---@field id string Su id (campo de solo lectura).
local Element = {}

--- Si el elemento existe.
---@return boolean
function Element:valid() end

--- El elemento con ese id dentro de este.
---@param id string
---@return Element
function Element:get(id) end

--- Pone su texto.
---@param text string
function Element:set_text(text) end

--- Lo convierte en el dibujo del control que hace la acción.
---@param action string
---@param icon boolean?
function Element:set_prompt(action, icon) end

--- Pone un texto con sus marcas {prompt:acción} dibujadas como controles.
---@param text string
---@param original boolean?
function Element:set_prompt_text(text, original) end

--- Su contenido.
---@return string
function Element:text() end

--- Pone o quita una clase.
---@param name string
---@param on boolean
function Element:set_class(name, on) end

--- Si tiene una clase.
---@param name string
---@return boolean
function Element:has_class(name) end

--- El valor de un atributo; nil sin él.
---@param name string
---@return string?
function Element:attribute(name) end

--- Pone un atributo; no uno de evento (on...).
---@param name string
---@param value string
function Element:set_attribute(name, value) end

--- Pone una propiedad de estilo.
---@param name string
---@param value string
function Element:set_property(name, value) end

--- Quita una propiedad que puso set_property.
---@param name string
function Element:remove_property(name) end

--- Su caja en dp desde la esquina de la ventana: izquierda, arriba, ancho y alto.
---@return number
---@return number
---@return number
---@return number
function Element:rect() end

--- Le da el foco.
function Element:focus() end

--- Si tiene el foco.
---@return boolean
function Element:has_focus() end

--- Lo pulsa.
function Element:click() end

--- Vuelve una lista arriba.
function Element:scroll_to_top() end

---@class Event
---@field type string El tipo del evento (campo de solo lectura).
---@field wheel number Cuánto giró la rueda, positivo hacia abajo (campo de solo lectura).
local Event = {}

--- No deja que el evento siga subiendo.
function Event:stop() end

clock = {}

--- La hora: year, day, hour, minute, night, phase (morning, day, late, night) y running (si el tiempo del juego corre).
---@return table
function clock.now() end

field = {}

--- Cuánto oscurece el fundido del cambio de mapa, de 0 a 1.
---@return number
function field.fade() end

--- La clave de texto del nombre del mapa que se enseña al llegar, o nil.
---@return string?
function field.map_title() end

--- El minimapa: shown (la opción y el campo lo dejan), covered (una ventana lo tapa) y size.
---@return table
function field.minimap() end

--- Dónde está el jugador en la ventana: x e y en porcentaje.
---@return table
function field.player() end

--- La cuenta del trabajo en la tienda mientras dura (tally, goal, better_pay, customer, customers), o nil.
---@return table?
function field.shop_job() end

partner = {}

--- Las barras (meters: id, label_key, value, min, max, two_tone, mood), los PV y PM (pools) y lo que le parece el mapa (area: liked, disliked o nil).
---@return table
function partner.status() end

--- Las necesidades en orden: id, label_key, active y urgency (1 la más urgente; 0 si no la tiene).
---@return table
function partner.needs() end

--- Dónde está su cabeza en la ventana: visible, x, y (porcentaje) y evolving (el cuadro de la burbuja de evolución, o nil).
---@return table
function partner.bubble() end

--- Errores de cuidado, combates y horas vividas; nil si el jugador tiene apagados los extras del estado.
---@return table?
function partner.extras() end

--- Pide felicitarlo.
---@return boolean
function partner.praise() end

--- Pide regañarlo.
---@return boolean
function partner.scold() end

--- Pide equipar o quitar la técnica de ese hueco de la especie.
---@param slot integer
---@return boolean
---@return string?
function partner.toggle_technique(slot) end

bag = {}

--- Los huecos de la bolsa.
---@return integer
function bag.count() end

--- El dinero del jugador.
---@return integer
function bag.money() end

--- Un dato de un hueco, o su valor por defecto.
---@param index integer
---@param name string
---@return any
function bag.field(index, name) end

--- Un hueco: empty, name, description, description_species, count, dimmed, usable, discardable, item_color, icon, icon_rect, favorite.
---@param index integer
---@return table
function bag.slot(index) end

--- La respuesta a la última acción: key (clave del mensaje) e item; nil antes de ninguna.
---@return table?
function bag.result() end

--- Pide usar el objeto de un hueco.
---@param index integer
---@return boolean
---@return string?
function bag.use(index) end

--- Pide tirar el objeto de un hueco.
---@param index integer
---@return boolean
---@return string?
function bag.drop(index) end

--- Pide intercambiar dos huecos.
---@param from integer
---@param to integer
---@return boolean
---@return string?
function bag.move(from, to) end

--- Pide ordenar la bolsa en un orden.
---@param order integer
---@return boolean
---@return string?
function bag.sort(order) end

player = {}

--- El jugador: name, level, bits, seconds, raised, medals, items, cards.
---@return table
function player.profile() end

--- Las medallas que publica el juego (no un número fijo): held y name_key.
---@return table
function player.medals() end

--- La ciudad: recruited, total y prosperity.
---@return table
function player.city() end

options = {}

--- Las opciones del juego en orden: name, kind (toggle, list, range), label_key, help_key, tab, default, original, values, texts, min, max.
---@return table
function options.list() end

--- El valor de una opción ahora (o su valor por defecto); nil si no existe.
---@param name string
---@return any
function options.get(name) end

--- Pide dar un valor a una opción; comprueba el tipo y los valores que admite.
---@param name string
---@param value any
---@return boolean
---@return string?
function options.set(name, value) end

---@alias UiEventName string
---| "clock.changed" Cambió el estado del campo, la hora incluida.
---| "field.fade" Cambió el fundido.
---| "field.map_title" Cambió el nombre del mapa que se enseña.
---| "field.minimap" Cambió si el minimapa puede verse o si algo lo tapa.
---| "field.shop_job" Cambió el trabajo en la tienda.
---| "partner.changed" Cambió algo del compañero; name dice qué ajuste.
---| "partner.bubble_moved" Su cabeza se movió en la ventana.
---| "bag.changed" Cambió la bolsa.
---| "bag.result" El juego respondió a una acción de la bolsa.
---| "options.changed" Cambió una opción; name dice cuál y value su valor.
