# JSON Schema: the subset the engine checks

The schemas in `content/schemas/` describe each type of YAML document (tables, maps, metadata…). They are checked by
`core::JsonSchema` (`packages/engine/core`), with no new dependencies: the schemas are JSON and JSON is YAML, so they
are read with `core::parseYaml`. It is not a full implementation of JSON Schema 2020-12, but **the subset our schemas
use**. A keyword that is not in the list is an error when compiling the schema, it is never ignored: this way a schema
never checks less than it says.

## Keywords

| Keyword | What it checks |
|---|---|
| `type` | A type or a list of types: `object`, `array`, `string`, `integer`, `number`, `boolean`, `null` |
| `properties` | The schema of each key of a mapping |
| `patternProperties` | The schema of the keys that match each regular expression (see below) |
| `required` | The keys that must be present |
| `additionalProperties` | `false` (no other keys are allowed), `true` or a schema for the keys that neither `properties` nor `patternProperties` describe |
| `minProperties`, `maxProperties` | How many keys a mapping has, at least and at most |
| `propertyNames` | The schema that each key of a mapping meets, as text |
| `items` | The schema of each element of a list (the tuple form, a list of schemas, is not supported) |
| `minItems`, `maxItems` | How many elements a list has |
| `uniqueItems` | With `true`, no element of the list equals another (compared as JSON, the same as `enum`) |
| `minLength`, `maxLength` | How many characters a text has (UTF-8 code points, not bytes) |
| `minimum`, `maximum` | Limits of a number, inclusive |
| `exclusiveMinimum`, `exclusiveMaximum` | Limits of a number, exclusive (the 2020-12 form: a number) |
| `pattern` | A regular expression that the text has to contain (see below) |
| `enum`, `const` | The allowed values, compared as JSON: numbers by value, `"1"` is not `1`, mappings without looking at the order of their keys |
| `anyOf`, `oneOf` | A list of schemas applied to the same value: it has to meet at least one (`anyOf`) or exactly one (`oneOf`) |
| `not` | A schema that the value cannot meet |
| `$ref` | `#` (the root) or `#/$defs/<name>`; it is applied together with the other keywords of the same schema |
| `$defs` | Reusable definitions, only at the root |
| `title`, `description`, `default`, `deprecated`, `$schema`, `$id` | Annotations for people and editors; they check nothing |

As in 2020-12, a key meets its `properties` schema and, in addition, that of each `patternProperties` entry whose
expression matches it (it can be more than one). `additionalProperties` only applies to the keys that neither of the
two describes. So `{"patternProperties": {"^[0-9]+$": {"type": "integer"}}, "additionalProperties": false}` allows
`12: 3` and rejects `x: 3`.

`anyOf`, `oneOf` and `not` check the value separately with each of their schemas and only look at whether it meets
them: the problems inside do not appear in the report, a single one appears for the value
(`matches none of the 2 schemas of anyOf`, `matches 2 of the 9 schemas of oneOf instead of exactly one`,
`matches the schema of not`). They are useful for keys that exclude each other, like `value` or `percent` in
`ruleset_difficulty`:

```json
"oneOf": [{ "required": ["value"] }, { "required": ["percent"] }]
```

A `$ref` cycle that does not descend through the document (`a` → `b` → `a`, also through `anyOf`, `oneOf` or `not`) is
an error when compiling.

Whoever compiles the schema can add their own **annotation keywords**. Their value has to be a text, and the check
returns, along with the problems, each text of the document to which they apply. Inside `anyOf` and `oneOf` only the
annotations of the schemas that the value meets count; those of `not`, never.

## Asset references

`dwx validate` registers an annotation keyword, `assetReference`, that turns a text in the document into a reference
to an asset in the stack. Its value is a template where `{}` is the text:

| Template | Text | Leads to |
|---|---|---|
| `"./{}"` | `"bank.sf2"` in `sound/ss/bank_meta` | the file `sound/ss/bank.sf2`: id `sound/ss/bank`, and it has to be `.sf2` |
| `"{}"` | `"map/yaka02/data.yaml"` | the file `map/yaka02/data.yaml`, from the package root |
| `"digimon/{}/model"` | `"species_a"` | the asset `digimon/species_a/model`, whatever its extension |

If the last segment of the result has an extension it is a file, relative to the document's folder if the template
starts with `./` and to the package root if not; if it has none, it is an id. The reference has to resolve in the
mounted stack, so a mod can point to assets in the base. Only texts are looked at: a `null` is not a reference.

```json
"target_map_id": { "type": ["string", "null"], "assetReference": "map/{}/data" }
```

## Types of a YAML scalar

YAML does not distinguish a number from a string in the text the way JSON does, so each scalar is resolved with the
YAML 1.2 *core* schema:

| Scalar | Type |
|---|---|
| Quoted (`"12"`, `'species_a'`) | `string`, always |
| Empty, `~`, `null` | `null` |
| `true`, `false` | `boolean` |
| `12`, `-3`, `0x00fe`, `0o17` | `integer` (and also `number`) |
| `1.5`, `-2e3`, `.inf`, `.nan` | `number` |
| Any other (`species_a`, `1.0.0`) | `string` |

That is why an id that looks like a number (`"001"`) has to be quoted when the schema asks for `string`. The
`minimum` and `maximum` limits are compared as 64-bit floating-point numbers, which are exact up to 2^53.

## Regular expressions

`pattern` and the keys of `patternProperties` use the same subset of ECMA-262 expressions. They search for the
expression anywhere in the text (or the key), as JSON Schema requires; to match the whole text it has to be anchored
with `^` and `$`. Supported:

- printable ASCII characters, and punctuation escaped with `\` (`\.`, `\-`, `\\`);
- `.`: any character except a line terminator (`\n`, `\r`, U+2028 and U+2029, as in ECMA-262 without the `s` flag);
  a non-ASCII character counts as one, however many UTF-8 bytes it takes;
- classes with ranges: `[a-z0-9_]` (not negated: `[^…]` is not supported);
- groups `( )` with alternatives `|`;
- quantifiers `?`, `*`, `+`, `{n}`, `{n,}` and `{n,m}` (n and m up to 1000), greedy only;
- anchors `^` and `$`.

Anything else is an error when compiling: `\d` and the other classes with a letter, `(?…)`, lazy quantifiers,
backreferences and non-ASCII characters. A non-ASCII character in the text only matches `.`: never an ASCII class or a
literal.

The expression is compiled to a Pike virtual machine: all the possible matches advance at once, so the time grows
linearly with the text, with no exponential backtracking or recursion, even if the pattern is hostile (those in
`patternProperties` too: they can come from a third-party pack). The pattern has a limit of 1024 characters and its
program of 20,000 instructions.

## Messages

Each problem says where it is, with the path inside the document (`$` is the root, then `.key` and `[index]`) and the
line and column of the text:

```text
$.digimon[3].level: expected integer, found string     (line 12, column 9)
$.digimon[0]: the key 'moves' is missing               (line 3, column 1)
```
