# vne

Motor de novela visual en C++20 con secciones de exploración 2D, diseñado para eficiencia,
portabilidad y una ruta abierta a 3D simple en el futuro.

## Documentación

| Archivo | Contenido |
|---|---|
| `docs/SPEC.md` | Especificación completa. Fuente de verdad del proyecto. |
| `docs/DECISIONS.md` | Registro de decisiones arquitectónicas (ADR). |
| `CLAUDE.md` | Instrucciones operativas para el agente de IA. |
| `.claude/skills/` | Skills por área: estilo, memoria, estado, guion, render, hitos, build. |

## Estado

Hito activo: **M0 — Esqueleto** (no iniciado).

El proyecto todavía no compila: `src/` está vacío a la espera de M0. Ver `docs/SPEC.md` §12
para la lista de hitos y sus criterios de aceptación.

## Stack

SDL3 · sokol_gfx · Dear ImGui · FreeType · HarfBuzz · miniaudio · Lua 5.4 + sol2 ·
HandmadeMath · stb_image · doctest

La lista es cerrada. Ver `docs/SPEC.md` §3.
