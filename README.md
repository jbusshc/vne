# vne

Motor de novela visual en C++20 con secciones de exploración 2D, diseñado para eficiencia,
portabilidad y una ruta abierta a 3D simple en el futuro.

## Documentación

| Archivo | Contenido |
|---|---|
| `docs/SPEC.md` | Especificación completa. Fuente de verdad del proyecto. |
| `docs/DECISIONS.md` | Registro de decisiones arquitectónicas (ADR) y pendientes observados. |
| `docs/SCRIPT_LANGUAGE.md` | Referencia del lenguaje de guion `.vns`, para quien escribe guiones. |
| `CLAUDE.md` | Instrucciones operativas para el agente de IA. |
| `.claude/skills/` | Skills por área: estilo, memoria, estado, guion, render, hitos, build. |

## Estado

M0–M10 están implementados y cerrados: esqueleto, renderizado 2D, texto, VM y DSL,
guardado/rollback, ramificación, audio, UI de novela visual, editor, MapMode y
localización. El motor se juega de principio a fin.

Quedan cinco hitos por delante (M11–M15, ver `docs/SPEC.md` §12 y ADR-0050), que cierran lo
que M0–M10 dejó fuera: el sistema de assets de §7.4 y el `game.pak` de §11 —
`src/assets/` está vacío y hoy toda carga es síncrona—, los comandos `Move` y `Transition`,
la validación de identificadores al compilar, `config.ini` y el soporte de ratón.

Solo Windows está **verificado**. El código se escribe portable desde el principio —es la
prioridad 2 de `docs/SPEC.md` §1, con reglas concretas en §2— pero Linux y macOS no se han
compilado nunca porque no hay esas máquinas en el entorno de desarrollo: el backend GL está
escrito y sin compilar, y el de Metal no existe. Lo aplazado es comprobarlo, no programarlo
(`docs/SPEC.md` §13.1). Ver "Pendientes observados" al final de `docs/DECISIONS.md` para la
lista completa de limitaciones conocidas.

## Compilar y ejecutar

Requiere CMake 3.25+, un compilador con C++20 y Ninja. Las dependencias las descarga CPM
automáticamente en el primer configure.

```
cmake --preset debug     # o dev / ship
cmake --build build/debug
```

| Preset | Para qué |
|---|---|
| `debug` | Desarrollo con AddressSanitizer. Los números de rendimiento **no** son representativos aquí. |
| `dev` | Optimizado y con el editor (F1) compilado. |
| `ship` | Optimizado, sin editor ni símbolos de ImGui. Es donde se miden los criterios de rendimiento. |

El build hornea los assets (atlas, guiones, mapa y catálogos) al directorio de build, así
que un checkout limpio compila y arranca sin pasos manuales.

```
build/debug/vne_game                              # juego
build/debug/vne_game --autoplay-script <a.vnc>    # corre un guion sin ventana, sale 0/1
build/debug/tests/vne_tests                       # tests (doctest)
```

## Controles

| Tecla | Acción |
|---|---|
| WASD | Mover al jugador en el mapa |
| Espacio / Enter | Avanzar diálogo (y completar el efecto de máquina de escribir) |
| S / A | Alternar modo skip / modo auto |
| B | Backlog |
| M | Menú (volúmenes de bus e idioma) |
| F5 / F9 | Pantalla de guardado / de carga |
| ← → | Rollback atrás / adelante, **solo sin overlay abierto**; dentro de un menú cambian el valor de la fila |
| ↑ ↓ | Navegar filas del menú y de la pantalla de guardado; desplazar el backlog |
| F1 | Editor (solo en build `dev`) |
| ESC | Cerrar el overlay abierto, o salir del juego |

Toda la interacción es por teclado: no hay soporte de ratón todavía (ADR-0040).

## Herramientas offline

`vne_bake` hornea todo lo que el juego consume en binario ("cero parsing en release"):

```
vne_bake                                          # atlas de sprites
vne_bake script <in.vns> <out.vnc>                # guion
vne_bake map <in.tmx> <out.vnm>                   # mapa de Tiled
vne_bake catalog-extract <out.csv> <guion.vns...> # catálogo base de localización
vne_bake catalog-compile <in.csv> <out.vnl>       # catálogo de un idioma
```

## Stack

SDL3 · sokol_gfx · Dear ImGui · FreeType · HarfBuzz · miniaudio · Lua 5.4 + sol2 ·
HandmadeMath · stb_image · doctest

La lista es cerrada: ver `docs/SPEC.md` §3. Dos matices registrados como decisiones:
QOI se añadió como formato de textura horneada y de miniatura de guardado (ADR-0008,
ADR-0037), y los shaders se escriben a mano en vez de compilarse con `sokol-shdc`
(ADR-0010).
