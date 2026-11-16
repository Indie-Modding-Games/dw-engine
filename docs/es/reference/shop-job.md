# Trabajo en la tienda del cañón

El trabajo de Monochromon en la tienda del cañón (mapa gcan11): el jugador atiende a 20 clientes y, si la cuenta de
lo ganado llega a la meta, Monochromon se une a la ciudad. El motor trae una versión mejorada, documentada y con
opción: `shop_job_mode` en la pestaña Juego (`improved` por defecto; `original` juega el guion del disco tal cual;
en la consola y las rutas, `shop_job_mode("original")`).
Los números de la versión mejorada están en `rules/original/shop_job.yaml` del paquete del juego; el código, en
[`game/shop_job`](../modules/game/shop_job.md).

## El original

- **Comprobado** (guiones 49 y 199–202): el guion 49 (sección 0x51) empieza el trabajo (la rutina 30 pone la cuenta a
  0, la variable 123 cuenta los clientes) y su sección 0x52 manda cada cliente con una tirada a uno de los cuatro
  guiones de cliente (199–202, sección 0x33). A los 10 clientes el tendero dice cómo va; a los 20, paga.
- **Comprobado**: cada cliente pide un objeto al azar (carne, orinal portátil o medicina) y el jugador elige en
  menús subir, mantener o bajar el precio. «Subir» tira el recargo al azar y «Lo pensaré mejor» vuelve a tirar sin
  coste. La cara del globo de humor (rutina 28) solo depende del nivel del precio.
- **Comprobado**: un cliente que se va enfadado suma dos a la variable 123 (gasta dos de los 20 huecos).
- **Comprobado**: la paga sale del byte alto de la cuenta (rutina 56): menos de 1792, 500 bits; desde 1792, 1500;
  desde 3072, 2000 bits, la prosperidad sube 2 y Monochromon se une (disparador 247).
- Con la suerte del objeto, el techo de la cuenta va de 800 a 16000: jugando perfecto se une en unos 4 de cada 10
  trabajos (simulación).

## La versión mejorada

### Clientes

- Al empezar el trabajo (primer cliente) se hace la fila de los 20 con la semilla de la partida y el número del
  trabajo: guardar y cargar no la cambia y no gasta números aleatorios del juego.
- Especies y objetos salen de bolsas barajadas (las cuentas, en el ruleset). Cada especie tiene paciencia y carácter
  (derrochador, regateador, tacaño, caprichoso), que se ven en el mostrador.
- Cada cliente llega con un humor que se ve (alegre, normal, serio) y mueve su presupuesto un nivel arriba o abajo.
- El presupuesto (el precio más caro que acepta) es oculto y sale de la tabla de aceptación del original de su
  especie; algunos no compran nada.
- La fila se sortea de nuevo hasta que la puntuación perfecta (vender a cada uno a su presupuesto después de una
  charla) cae entre 4000 y 4300: la meta siempre se puede alcanzar.
- Un cliente que se va gasta un solo hueco.

### En el mostrador

La pantalla del mostrador (`shop_haggle`) sustituye a los menús de precios; el paseo, las frases y el globo de humor
siguen siendo los del original.

- **Precio**: uno de los 9 niveles del original (de +50 % a −30 %). Cada fila dice el precio, lo que suma a la cuenta
  (el precio menos el más barato, como el original), la probabilidad de compra con lo que el jugador sabe (especie,
  humor, charla y rechazos) y la cara más probable. El nivel elegido es exacto: no hay tirada.
- **Cara**: encantado (2 niveles o más por debajo del presupuesto) o contento compran; dudoso (1 por encima) y molesto
  (2) cuestan 1 de paciencia; furioso (3 o más), 2. Cada rechazo descarta presupuestos y la lista se recalcula. La
  cara sale en el globo de humor (caras 0 a 4) y en la pantalla.
- **Charlar**: una vez por cliente, con paciencia 2 o más; sube el presupuesto un nivel por 1 de paciencia.
- **Despedir**: se va sin comprar ni enfadarse.
- Sin paciencia, se va sin comprar.

### Pagas

| Cuenta | Paga |
|---|---|
| menos de 1792 | 500 bits |
| desde 1792 | 1500 bits |
| desde 3072 | 2000 bits y Monochromon se une (disparador 247), como el original |
| desde 3584 | lo anterior y el premio, un Corazón de acero (`game:steel_heart`); con la bolsa llena va al guardián de objetos (el trabajo no se repite, así que no se pierde) |

## Interfaz

- **Barra del HUD** (opción `shop_job_bar`): de 0 a la puntuación perfecta de la fila (en el modo original, de 0 a
  la meta) con tres marcas y su etiqueta: mejor paga, se une (la principal) y premio, con el icono del objeto. El
  relleno lleva una clase por paga alcanzada. El botón **?** abre el panel de reglas; con teclado o mando, la acción
  `help` (H o el gatillo izquierdo) mientras se ve la barra, con su tecla dibujada junto al **?**.
- **Panel de reglas** (`shop_job_rules`): se abre solo en el primer cliente de cada trabajo, con el **?** de la barra
  con `help` (H o el gatillo izquierdo) y con el botón de menú (Tab o el de menú del mando) en el mostrador.
  Confirmar, cancelar, `help`, Esc o el Back del mando lo cierran.
- **Mostrador** (`shop_haggle`): especie (con su sprite del tablero de torneos, `shop_haggle.species_icon`), carácter, humor, cara, paciencia, el objeto con su precio normal, los 9
  precios, Charlar, Despedir, las ofertas a este cliente y la cuenta. Ratón o teclado/mando (flechas, confirmar).

## Cómo se engancha a la historia

El parche `story/patches/shop_job_049.lua` cambia solo la sección 0x51 (el mostrador): pregunta al motor por el modo
(rutina 256) y en el original deja correr la sección del disco, sin tocar nada más del trabajo. En el mejorado juega
la charla del original y sigue en una sección propia, la 0x300 del guion 49 (el bucle: siguiente cliente, a mitad, la
paga) y de los guiones 199–202 (`shop_job_199.lua`…`shop_job_202.lua`: el cliente). Así los dos modos no se mezclan
y `dwx script-warps` sigue cada uno por su lado. Las rutinas propias del motor (números desde 256,
`session/src/story_routines/routine_shop_job.cpp`) contestan en la variable 110: 256 el modo (1 mejorado, 0 original),
257 el guion del siguiente cliente de la fila, 258 el objeto del cliente que llega, 259 un turno en el mostrador (abre
la pantalla y espera: 0 vendido, 1 rechazo, 2 charla, 3 se va, 4 despedido; el nivel en la 117 y la cara en la 248)
y 260 el premio al final (0 nada, 1 en la bolsa, 2 con el guardián de objetos, 3 sin sitio en ninguno). La venta suma a la cuenta desde el motor; el parche enseña
la frase de beneficio del original para ese nivel. Cambiar la opción a mitad de un trabajo vale para el siguiente.
