# JSON Schema: el subconjunto que comprueba el motor

Los esquemas de `content/schemas/` describen cada tipo de documento YAML (tablas, mapas, metadatos…). Los comprueba
`core::JsonSchema` (`packages/engine/core`), sin dependencias nuevas: los esquemas son JSON y el JSON es YAML, así que se leen
con `core::parseYaml`. No es una implementación completa de JSON Schema 2020-12, sino **el subconjunto que usan
nuestros esquemas**. Una palabra clave que no está en la lista es un error al compilar el esquema, nunca se ignora:
así un esquema no comprueba menos de lo que dice.

## Palabras clave

| Palabra clave | Qué comprueba |
|---|---|
| `type` | Un tipo o una lista de tipos: `object`, `array`, `string`, `integer`, `number`, `boolean`, `null` |
| `properties` | El esquema de cada clave de un mapping |
| `patternProperties` | El esquema de las claves que encajan en cada expresión regular (ver abajo) |
| `required` | Las claves que tienen que estar |
| `additionalProperties` | `false` (no se admiten otras claves), `true` o un esquema para las claves que ni `properties` ni `patternProperties` describen |
| `minProperties`, `maxProperties` | Cuántas claves tiene un mapping, como mínimo y como máximo |
| `propertyNames` | El esquema que cumple cada clave de un mapping, como texto |
| `items` | El esquema de cada elemento de una lista (la forma de tupla, una lista de esquemas, no está) |
| `minItems`, `maxItems` | Cuántos elementos tiene una lista |
| `uniqueItems` | Con `true`, ningún elemento de la lista es igual a otro (comparados como JSON, igual que `enum`) |
| `minLength`, `maxLength` | Cuántos caracteres tiene un texto (puntos de código UTF-8, no bytes) |
| `minimum`, `maximum` | Límites de un número, incluidos |
| `exclusiveMinimum`, `exclusiveMaximum` | Límites de un número, excluidos (la forma de 2020-12: un número) |
| `pattern` | Una expresión regular que el texto tiene que contener (ver abajo) |
| `enum`, `const` | Los valores permitidos, comparados como JSON: los números por valor, `"1"` no es `1`, los mappings sin mirar el orden de sus claves |
| `anyOf`, `oneOf` | Una lista de esquemas que se aplican al mismo valor: tiene que cumplir al menos uno (`anyOf`) o exactamente uno (`oneOf`) |
| `not` | Un esquema que el valor no puede cumplir |
| `$ref` | `#` (la raíz) o `#/$defs/<nombre>`; se aplica junto a las demás palabras clave del mismo esquema |
| `$defs` | Definiciones reutilizables, solo en la raíz |
| `title`, `description`, `default`, `deprecated`, `$schema`, `$id` | Anotaciones para las personas y los editores; no comprueban nada |

Como en 2020-12, una clave cumple su esquema de `properties` y, además, el de cada entrada de `patternProperties` cuya
expresión encaje en ella (puede ser más de una). `additionalProperties` solo se aplica a las claves que ninguna de las
dos describe. Así, `{"patternProperties": {"^[0-9]+$": {"type": "integer"}}, "additionalProperties": false}` admite
`12: 3` y rechaza `x: 3`.

`anyOf`, `oneOf` y `not` comprueban el valor aparte con cada uno de sus esquemas y solo miran si los cumple: los
problemas de dentro no salen en el informe, sale uno solo para el valor (`matches none of the 2 schemas of anyOf`,
`matches 2 of the 9 schemas of oneOf instead of exactly one`, `matches the schema of not`). Sirven para claves que se
excluyen entre sí, como `value` o `percent` en `ruleset_difficulty`:

```json
"oneOf": [{ "required": ["value"] }, { "required": ["percent"] }]
```

Un ciclo de `$ref` que no baja por el documento (`a` → `b` → `a`, también a través de `anyOf`, `oneOf` o `not`) es un
error al compilar.

Quien compila el esquema puede añadir **palabras clave de anotación** propias. Su valor tiene que ser un texto, y la
comprobación devuelve, junto a los problemas, cada texto del documento al que se aplican. Dentro de `anyOf` y `oneOf`
solo cuentan las anotaciones de los esquemas que el valor cumple; las de `not`, nunca.

## Referencias a assets

`dwx validate` registra una palabra clave de anotación, `assetReference`, que convierte un texto del documento en una
referencia a un asset de la pila. Su valor es una plantilla donde `{}` es el texto:

| Plantilla | Texto | Lleva a |
|---|---|---|
| `"./{}"` | `"bank.sf2"` en `sound/ss/bank_meta` | el fichero `sound/ss/bank.sf2`: id `sound/ss/bank`, y tiene que ser `.sf2` |
| `"{}"` | `"map/yaka02/data.yaml"` | el fichero `map/yaka02/data.yaml`, desde la raíz del paquete |
| `"digimon/{}/model"` | `"species_a"` | el asset `digimon/species_a/model`, sea cual sea su extensión |

Si el último segmento del resultado tiene extensión es un fichero, relativo a la carpeta del documento si la plantilla
empieza por `./` y a la raíz del paquete si no; si no la tiene, es un id. La referencia tiene que resolverse en la pila
montada, así que un mod puede apuntar a assets de la base. Solo se miran los textos: un `null` no es una referencia.

```json
"target_map_id": { "type": ["string", "null"], "assetReference": "map/{}/data" }
```

## Tipos de un escalar YAML

YAML no distingue en el texto un número de una cadena como JSON, así que cada escalar se resuelve con el esquema
*core* de YAML 1.2:

| Escalar | Tipo |
|---|---|
| Entre comillas (`"12"`, `'species_a'`) | `string`, siempre |
| Vacío, `~`, `null` | `null` |
| `true`, `false` | `boolean` |
| `12`, `-3`, `0x00fe`, `0o17` | `integer` (y también `number`) |
| `1.5`, `-2e3`, `.inf`, `.nan` | `number` |
| Cualquier otro (`species_a`, `1.0.0`) | `string` |

Por eso un id que parece un número (`"001"`) tiene que ir entre comillas cuando el esquema pide `string`. Los límites
`minimum` y `maximum` se comparan como números de coma flotante de 64 bits, que son exactos hasta 2^53.

## Expresiones regulares

`pattern` y las claves de `patternProperties` usan el mismo subconjunto de las expresiones de ECMA-262. Buscan la
expresión en cualquier parte del texto (o de la clave), como manda JSON Schema; para el texto entero hay que anclarla
con `^` y `$`. Se admite:

- caracteres ASCII imprimibles, y la puntuación escapada con `\` (`\.`, `\-`, `\\`);
- `.`: un carácter cualquiera salvo un fin de línea (`\n`, `\r`, U+2028 y U+2029, como en ECMA-262 sin la bandera `s`);
  un carácter no ASCII cuenta como uno, ocupe los bytes UTF-8 que ocupe;
- clases con rangos: `[a-z0-9_]` (no negadas: `[^…]` no está);
- grupos `( )` con alternativas `|`;
- cuantificadores `?`, `*`, `+`, `{n}`, `{n,}` y `{n,m}` (n y m hasta 1000), solo voraces;
- anclas `^` y `$`.

Lo demás es un error al compilar: `\d` y las demás clases con letra, `(?…)`, cuantificadores perezosos,
referencias hacia atrás y caracteres que no sean ASCII. Un carácter no ASCII del texto solo encaja en `.`: nunca en una
clase ASCII ni en un literal.

La expresión se compila a una máquina virtual de Pike: todas las coincidencias posibles avanzan a la vez, así que el
tiempo crece en línea con el texto, sin retroceso exponencial ni recursión, aunque el patrón sea hostil (los de
`patternProperties` también: pueden venir de un pack de terceros). El patrón
tiene un límite de 1024 caracteres y su programa, de 20 000 instrucciones.

## Mensajes

Cada problema dice dónde está, con la ruta dentro del documento (`$` es la raíz, luego `.clave` e `[índice]`) y la
línea y columna del texto:

```text
$.digimon[3].level: expected integer, found string     (línea 12, columna 9)
$.digimon[0]: the key 'moves' is missing               (línea 3, columna 1)
```
