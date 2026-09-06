---
name: vne-build-verify
description: Sistema de build de vne (CMake con CPM), configuraciones Debug/Dev/Ship, flags de compilación, sanitizers, y cómo verificar de forma objetiva los criterios de aceptación de cada hito. Consulta este skill SIEMPRE que vayas a tocar CMakeLists.txt, añadir un objetivo de compilación, integrar una dependencia, configurar sanitizers o tests, o cuando necesites demostrar que un criterio de aceptación se cumple (fps, draw calls, asignaciones por frame, tiempos de layout).
---

# Build y verificación

## Objetivos de CMake

| Objetivo | Tipo | Contenido |
|---|---|---|
| `vne_base` | biblioteca estática | `base/ platform/ gfx/ text/ audio/ assets/ vm/ script/` |
| `vne_game` | ejecutable | `vne_base` + `modes/` + `editor/` (condicional) |
| `vne_bake` | ejecutable | herramientas offline de `tools/bake/` |
| `vne_tests` | ejecutable | tests con doctest |

CMake 3.25 mínimo. Dependencias con **CPM.cmake** (`cmake/CPM.cmake`, versión fijada por
commit hash). Sin vcpkg, sin Conan, sin submódulos de git.

Toda dependencia se fija a una versión o commit concreto. Nunca `main` ni `master`.

## Configuraciones

```
Debug   -O0 -g -fsanitize=address,undefined -DVN_DEBUG=1
Dev     -O2 -g                              -DVN_DEBUG=1 -DVN_EDITOR=1
Ship    -O3                                 -DNDEBUG -DVN_SHIPPING=1
Todas   -Wall -Wextra -Werror -fno-exceptions -fno-rtti
```

En `Ship`, `src/editor/` queda excluido del build por completo. Verifica que el binario final
no contiene símbolos de ImGui:

```bash
nm -C build/ship/vne_game | grep -i imgui   # debe salir vacio
```

En MSVC, los equivalentes: `/W4 /WX /EHsc-` y `/fsanitize=address` en Debug.

## Cómo verificar cada tipo de criterio

Los criterios de aceptación son números. Mídelos, no los estimes.

**Asignaciones de heap por frame (cero).** En `Debug` y `Dev` se sobrecargan `operator new`,
`operator delete` y se instrumenta `malloc` para incrementar `g_frame_alloc_count`. Al final
del frame:

```cpp
VN_ASSERT(g_frame_alloc_count == 0, "asignacion de heap dentro del frame");
```

El contador aparece en el HUD de debug. Para la verificación de cierre de hito, ejecuta 60
segundos de gameplay y reporta el valor máximo observado.

**Draw calls.** Contador incrementado en `gfx_flush`, mostrado en el HUD. Para el criterio de
M1: 5000 sprites del mismo atlas deben producir exactamente 1 draw call.

**Frame time y fps.** Histograma de los últimos 240 frames en el HUD. Reporta el percentil 99,
no la media. Una media de 300 fps con picos de 40 ms es un fallo.

**Tiempos de operación puntual (layout de texto, carga).** Instrumenta con
`clock_now_microseconds()` alrededor de la operación y reporta el peor caso de 1000
ejecuciones, no la primera.

**Sanitizers.** Un hito no está cerrado si ASan o UBSan reportan algo:

```bash
cmake --preset debug && cmake --build build/debug
ASAN_OPTIONS=detect_leaks=1 ./build/debug/vne_tests
ASAN_OPTIONS=detect_leaks=1 ./build/debug/vne_game --autoplay-script tests/scripts/smoke.vns
```

**Compilación multiplataforma.** Si no tienes acceso a las tres plataformas, compila las que
puedas y **di explícitamente cuáles no verificaste**. No lo des por bueno.

## Tests

doctest, solo en `vne_tests`. No enlaza contra el ejecutable del juego.

Qué merece un test en este proyecto:

- Arena: alineación, agotamiento, reset, patrón `0xCD` en debug.
- Pool y handles: detección de handle caducado tras liberar y reutilizar un slot.
- Serialización: el test de guardar y recargar en cada comando (ver skill
  `vne-serializable-state`).
- Parser del DSL: cada comando, y los errores esperados con archivo y línea correctos.
- Layout de texto: word-wrap latino y kinsoku CJK con casos concretos.
- Formatos binarios: escribir y releer `.vnc`, `.pak`, `.vnsave`.

Qué **no** merece un test: envolturas de una línea sobre SDL o sokol, getters, código de
render que solo se puede validar mirando la pantalla.

## Modo autoplay

Desde M3, el juego acepta `--autoplay-script <ruta>` para ejecutar un guion completo sin
input humano, a máxima velocidad, y salir con código 0 o distinto de 0. Es la base de toda
verificación automatizada posterior. Impleméntalo en cuanto exista la VM.

## Assets horneados

`assets_baked/` está en `.gitignore`. El build debe regenerarlo desde `assets_src/` si falta.
Un checkout limpio más un build debe producir un juego ejecutable sin pasos manuales.
