# shop_job

El trabajo en la tienda del cañón, mejorado (`dwe::game::shop_job`, capa `game`): la fila de los 20 clientes, sus
presupuestos ocultos, lo que el jugador sabe de cada uno, el regateo en el mostrador y las pagas del final. Es una
mejora del motor con opción (`shop_job_mode`: `improved` por defecto, `original` juega el guion del disco tal cual).
No dibuja nada: depende de `core`, `assets` y `ruleset`. Los números salen de la sección `shop_job` del ruleset
(`rules/<id>/shop_job.yaml`, la de `original` la heredan los demás). Las reglas del juego, en la
[referencia del trabajo](../../reference/shop-job.md).

## Uso

```cpp
auto rules = dwe::game::shop_job::parseShopJobRules(*ruleset.section("shop_job"));
dwe::ruleset::Random random{dwe::game::shop_job::lineupSeed(session.seed(), job)};
auto line = dwe::game::shop_job::makeLineup(*rules, random);   // 20 clientes, perfecta en [4000, 4300]
dwe::game::shop_job::Haggle haggle{*rules, line.customers[0]};
haggle.chance(5);                    // % de compra a precio normal, con lo que el jugador sabe
auto result = haggle.offer(3);       // reacción, venta, lo que suma, si se va
```

- **Fila** (`makeLineup`): las copias de cada objeto repartidas entre las especies, humor al llegar por peso y
  presupuesto sorteado con la tabla de aceptación del original; luego los presupuestos pasan al nivel más cercano que
  el cliente podía tener (`shiftWeights`) hasta que la puntuación perfecta (`perfectProfit`: una charla y la venta al
  presupuesto) cae en la ventana, y una fila que sigue fuera se sortea de nuevo. La semilla (`lineupSeed`) mezcla la de la partida con el número
  del trabajo: no gasta números aleatorios del juego y una carga da la misma fila.
- **Regateo** (`Haggle`): la probabilidad de cada precio sale de los pesos del presupuesto que el jugador conoce
  (especie, humor y rechazos); cada oferta da una reacción por la distancia al presupuesto y cuesta paciencia si no
  compra; charlar sube el presupuesto un nivel una vez; despedir acaba sin enfado.
- **Pagas** (`payTierOf`): las tres del original por la cuenta y el premio encima.

## Paridad con el original

| Qué | Original | Motor | Tests | Estado |
|---|---|---|---|---|
| Fila de clientes | Cada cliente es una tirada (`store_random(110, 8)` en el guion 49): especie y objeto al azar, el techo de la cuenta va de 800 a 16000 | `makeLineup`: 7/6/4/3 especies y 4/10/6 objetos, humor y presupuesto por cliente, perfecta en [4000, 4300] | `shop_job_tests.cpp` («every job's line lets a perfect player make 4000 to 4300», «a job's line is the same for the same playthrough and job…», «every customer's budget is one its species and mood can give») | Mejora (opción) |
| Presupuesto y reacción | Subir precio tira el recargo al azar y la cara solo depende del nivel | Presupuesto oculto con la tabla de aceptación de cada especie y el humor aplicado; reacción por la distancia (encantado, contento, dudoso, molesto, furioso) | «the reaction says how far the offer was from the budget», «a refusal rules out the budgets…», «talking raises the budget once…», «a customer out of patience leaves…» | Mejora (opción) |
| Pagas | Rutina 56 y guion 49 (0x52): 500, 1500 o 2000 bits y el disparador 247 | `payTierOf` con el premio (`bonus.from`, 3584) | «the job pays by the tally's tier» | Mejora (opción) |
