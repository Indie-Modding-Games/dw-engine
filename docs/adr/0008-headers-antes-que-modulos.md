# 0008. Headers ahora, módulos de C++ más adelante

- **Estado:** Aceptada
- **Fecha:** 2026-10-18
- **Nota (2026-11-06):** aceptada porque el código ya la sigue: el código usa headers; no hay módulos de C++.

## Contexto

Los módulos de C++ (`import`) ya los soporta CMake, pero clangd, clang-tidy y algunos compiladores todavía tienen
fallos con ellos.

## Decisión

Se usan **headers** (`#pragma once`) con la separación entre API pública (`include/`) y privada (`src/`). La
decisión se revisa en 2027.

## Consecuencias

- Las herramientas funcionan sin sorpresas.
- La estructura `include/dwe/<módulo>/` permite pasar a módulos más adelante sin reorganizar el repositorio.

## Alternativas descartadas

- **Módulos desde el principio:** el riesgo con las herramientas es demasiado alto para la fase 0.
