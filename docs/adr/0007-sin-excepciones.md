# 0007. Sin excepciones, con `std::expected`

- **Estado:** Propuesta
- **Fecha:** 2026-10-18

## Contexto

Los motores de juego suelen desactivar las excepciones: el flujo de errores queda explícito, el rendimiento es
predecible y es más fácil cruzar la frontera con Lua y con librerías en C.

## Decisión

- Se compila con `-fno-exceptions`.
- Los errores recuperables se devuelven con `std::expected<T, Error>`.
- Lo que no debe pasar nunca se comprueba con asserts.

## Consecuencias

- Cada error se ve en la firma de la función.
- Las dependencias que lanzan excepciones se envuelven en el borde del módulo que las usa.

## Alternativas descartadas

- **Excepciones:** el flujo de errores queda oculto y el coste es menos predecible.
