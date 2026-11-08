# net

La red entre dos máquinas (`dwe::net`, capa `base`): datagramas, un canal fiable y ordenado encima, lockstep para
simulaciones deterministas y direcciones con código para unirse. Solo depende de `core`; no sabe nada del juego. Lo usa
el versus en línea ([versus](../game/versus.md)).

## Piezas

| Pieza | Qué hace |
|---|---|
| `DatagramLink` | Un extremo de un enlace de datagramas sin garantías (como UDP): `send` y `receive`, nada bloquea. Ningún datagrama pasa de `MaxDatagram` (1200 bytes), así que no se fragmenta |
| `UdpLink` | El enlace de verdad sobre sockets UDP del sistema (IPv4). `host(port)` escucha y se queda con el primer par que le habla; `join(endpoint)` habla con el anfitrión desde un puerto libre. `localAddresses()` da las direcciones IPv4 de la máquina para enseñarlas |
| `LoopbackNetwork` | Dos pares simulados en un proceso para los tests, con su reloj (`advance`) y `LinkConditions`: latencia, variación (que desordena), pérdida y duplicados con semilla, y `cut()` para un par que se va sin avisar |
| `ReliableChannel` | Mensajes que llegan una vez y en orden sobre un `DatagramLink`: número de secuencia, acuse del último en orden más una máscara de los 32 siguientes, reenvío a los 120 ms sin acuse y un acuse vacío cada 250 ms en silencio (así el otro sabe que sigue ahí). Cuenta el tiempo desde lo último que oyó |
| `Lockstep` | Una simulación determinista en dos máquinas: cada lado manda solo sus entradas, programadas `inputDelay` ticks por delante; un tick se juega cuando se conocen las dos. Tras cada tick los dos mandan una suma de su estado y el primer tick en que no coinciden es una desincronización que para la partida. Los ticks antes del retraso van sin entrada (0) |
| `WireWriter` / `WireReader` | Enteros little-endian, textos con longitud y lectura que falla limpio si el mensaje es corto |
| `Endpoint`, `parseEndpoint`, `joinCodeOf`, `parseJoinTarget` | `a.b.c.d:puerto`, y un código corto que es la misma dirección en diez letras y cifras (base 32 de Crockford, `ABCDE-12345`; acepta minúsculas, espacios, guiones, I/L por 1 y O por 0). No oculta nada: cualquiera lee la dirección en él. `parseJoinTarget` acepta las dos formas y pone el puerto por defecto a una dirección sin él |

Los mensajes del lockstep empiezan con `0x40`/`0x41`; los demás protocolos del mismo canal usan otros primeros bytes
(el versus, `0x10`-`0x14`).

## Por qué UDP propio y no una librería

Se miró ENet (MIT, en vcpkg): hace lo mismo que `ReliableChannel` y más. Se prefirió el canal propio porque los tests
del lockstep y del versus pasan por el mismo código de fiabilidad que se usa en la partida (con pérdida, duplicados y
desorden simulados), porque no añade dependencia ni licencia y porque la compilación cruzada a Windows no cambia
(solo `ws2_32`). Si un día hace falta NAT traversal o cifrado, se puede cambiar el `DatagramLink` sin tocar lo de
encima.

## Límites

- Solo IPv4 y conexión directa: el anfitrión tiene que estar en la misma red o tener abierto (o redirigido en su
  router) el puerto UDP del versus (47120). No hay NAT traversal ni servidor de señalización o de relevo.
- Sin cifrado ni autenticación: el código de unión es la dirección y nada más.

## Tests

`dwe_net_tests` (etiqueta `net`): el canal sobre un enlace limpio y sobre uno malo (latencia, pérdida, duplicados y
desorden), reenvío, silencio y corte, mensajes ajenos y demasiado grandes; el lockstep de dos pares sobre un enlace
malo, los ticks sin entrada, la desincronización en su tick y los mensajes dañados; direcciones y códigos; y dos
`UdpLink` de verdad en `127.0.0.1` hablando por el canal.
