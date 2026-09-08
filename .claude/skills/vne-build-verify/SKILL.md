---
name: vne-build-verify
description: Sistema de build de vne (CMake con CPM), configuraciones Debug/Dev/Ship, flags de compilación, sanitizers, y cómo verificar de forma objetiva los criterios de aceptación de cada hito. Consulta este skill SIEMPRE que vayas a tocar CMakeLists.txt, añadir un objetivo de compilación, integrar una dependencia, configurar sanitizers o tests, o cuando necesites demostrar que un criterio de aceptación se cumple (fps, draw calls, asignaciones por frame, tiempos de layout).
---

# Build y verificación

## Objetivos de CMake

| Objetivo | Tipo | Contenido |
|---|---|---|
| `vne_base` | biblioteca estática | `base/ platform/ gfx/ text/ audio/ vm/ game/` más `script/lua_bindings.cpp` |
| `vne_script_tools` | biblioteca estática | código **offline**: lexer, parser, compilador del DSL y horneado de mapas |
| `vne_game` | ejecutable | `vne_base` + `main.cpp` + `editor/` (solo si no es Ship) |
| `vne_bake` | ejecutable | `tools/bake/` + `vne_script_tools` |
| `vne_tests` | ejecutable | tests con doctest, enlaza `vne_base` y `vne_script_tools` |

`vne_script_tools` existe para que el compilador del DSL **nunca** entre en el juego (SPEC.md
§9.3: cero parsing en release) y, a la vez, sea alcanzable por los tests. Si escribes lógica
offline no trivial, va ahí y no dentro del `main()` de `vne_bake`, precisamente para que se
pueda testear: tres bugs del escáner de TMX vivieron sin cobertura por ese motivo (ADR-0049).

Los modos de juego viven en `src/game/`, no en `src/modes/` — ese directorio está vacío.

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

En MSVC, los equivalentes: `/W4 /WX /GR- /EHs-c-` y `/fsanitize=address` en Debug. **UBSan no
tiene equivalente en MSVC**, así que en Windows solo se verifica ASan; comprobar UBSan exige
Linux o macOS y por eso está en SPEC.md §13.1.

Windows es la única plataforma en la que este proyecto se ha compilado nunca (ADR-0013). El
comando de verificación de símbolos de ImGui de arriba usa `nm`, que aquí no existe: el
equivalente que se usó para cerrar M8 fue `strings vne_game.exe | grep -i imgui`.

## Cómo verificar cada tipo de criterio

Los criterios de aceptación son números. Mídelos, no los estimes.

**Asignaciones de heap por frame (cero).** En `Debug` y `Dev` se sobrecargan `operator new`,
`operator delete` y se instrumenta `malloc` para incrementar `g_frame_alloc_count`. Hay tres
excepciones acotadas y documentadas (Lua, primera carga de audio, subproceso del editor) que se
marcan con `heap_guard_suspend/resume`: ver el skill `vne-memory-model` antes de dar por buena
una asignación nueva. Al final del frame: 

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
./build/debug/tests/vne_tests
./build/debug/vne_game --autoplay-script assets_baked/demo.vnc
```

El autoplay toma un `.vnc` ya horneado (el build los deja en `build/<preset>/assets_baked/`),
no un `.vns` fuente: el juego no parsea texto. Un test de rendimiento falla siempre en `debug`
porque ASan y `-O0` lo hacen no representativo (ADR-0018); es esperado y no cuenta como
regresión, pero **dilo** en el cierre en vez de omitirlo.

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
