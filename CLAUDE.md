# vne — instrucciones del proyecto

Motor de novela visual en C++20. Lee `docs/SPEC.md` completo antes de escribir código.
Este archivo es el resumen operativo; la especificación manda sobre él en caso de conflicto.

## Estado actual

**Hito activo:** ninguno (M8 implementado, pendiente de confirmación para cerrar)
**Último hito completado:** M8 — Editor. Dear ImGui (rama docking) integrado vía
`util/sokol_imgui.h` de sokol (ya en el pin de M1, sin dependencia nueva). `src/editor/`
y la propia librería de ImGui se excluyen del build por completo en Ship a nivel de
CMake (`CMAKE_BUILD_TYPE STREQUAL "Ship"`), no solo con un `#ifdef` vacío (ADR-0041):
verificado con `strings vne_game.exe | grep -i imgui` sobre el binario Ship real, 0
coincidencias. F1 activa/desactiva el editor; paneles: inspector de `GameState` (pc,
actores, 16 variables editables), salto a comando arbitrario, contador de
allocs/gráfico de frame time, y recarga de scripts. La recarga invoca `vne_bake` como
subproceso en vez de enlazar el compilador del DSL en el juego (ADR-0042): el
compilador del DSL sigue siendo exclusivo de herramientas offline incluso en Dev.
Criterio medible de M8 ("editar un `.vns` y ver el cambio sin reiniciar") verificado
end-to-end de verdad: modificar `demo.vns` mientras `vne_game.exe` (Dev) corría disparó
la recompilación y recarga sin reiniciar el proceso, con `heap_allocs_frame_max` en 0
durante todo el proceso (confirma que la excepción de `heap_guard` alrededor del
subproceso, mismo patrón que Lua/audio, funciona de verdad, no solo por simetría de
código). `Say` bloqueando de verdad (M7) hizo evidente que `platform/input.h` necesitaba
ratón real para que un editor con paneles fuera usable: `InputState` gana posición,
botones y rueda de ratón (píxeles de ventana reales, la UI de VN sigue siendo solo
teclado, ADR-0040 no cambia). 94/94 tests en Dev; 93/94 en Debug+ASan (el de rendimiento
de M2 no representativo sin optimizar, ADR-0018) y 93/93 en Ship, sin ningún reporte de
memoria en ninguna configuración. No hay tests automatizados para el watcher de recarga
ni para los paneles del editor (ImGui no se presta a tests unitarios sin un backend de
captura de pantalla); se verificó a mano una vez. Windows sigue siendo la única
plataforma verificada (ADR-0013). Detalle completo en docs/DECISIONS.md.

M7 — UI de novela visual (hito anterior). Pila de modos `Mode`/`ModeStack`
(SPEC.md #10, `virtual` explícito de la especificación, no viola la regla general porque
la pila tiene 2-4 elementos). `VnMode` dirige la VM y dibuja el cuadro de diálogo real:
`Say` por fin bloquea de verdad esperando input (cierra ADR-0023 desde M3, ver ADR-0039),
con modo skip (`vm_skip_current` en bucle) y modo auto (temporizador tras el efecto
máquina de escribir). `BacklogMode`, `MenuMode` (volúmenes de bus) y `SaveLoadMode` (4
slots, miniaturas QOI reales — ADR-0037, decisión del usuario ante la falta de un
codificador PNG en runtime) se apilan encima. Captura del framebuffer
(`gfx_backend_capture_thumbnail`) implementada de verdad solo en D3D11 (staging texture +
`CopyResource` + `Map`, con downsampling durante la lectura; GL sigue sin implementar,
ADR-0038/ADR-0009). Toda la interacción es solo teclado (sin ratón todavía, ADR-0040).
Criterio medible de M7 (SPEC.md #12: "modo skip recorre 1000 comandos en menos de 1
segundo") verificado con holgura: mediana de 444us en Debug+ASan, 65us en Ship (mediana
de 50 muestras, mismo método que ADR-0018). 93/93 tests en Ship; en Debug+ASan 93/94 (el
de rendimiento de M2 no representativo sin optimizar, ADR-0018), sin ningún reporte de
memoria. `SaveLoadMode`/`BacklogMode`/`MenuMode` no se probaron con pulsaciones de teclado
reales en la ventana interactiva en este entorno (misma limitación que F5/F9 desde M4):
solo se verificó que compilan, que el juego arranca sin crashear con ellos cableados
(`heap_allocs_frame_max=0` se mantiene, `vm_pc` se queda correctamente parado en el
primer `Say` esperando confirmación en vez de avanzar solo), y su lógica interna vía
tests (`mode_stack`, `save.h`). Windows sigue siendo la única plataforma verificada
(ADR-0013). Detalle completo en docs/DECISIONS.md.

M6 — Audio (hito anterior). `audio/audio.{h,cpp}` envuelve miniaudio con la
API exacta de SPEC.md #7.3 (`Bus`, `audio_init/load/play/stop/set_bus_volume/
crossfade_music`). `CmdKind` gana `Sfx`, `Bgm`, `StopBgm` (`sizeof(Cmd)` sigue en 16
bytes). `Sfx.sound_id` reutiliza el `string_pool` del guion (misma ruta que `Say`);
`Bgm.track_id` en cambio es `fnv1a % 65536` contra un catalogo escaneado de
`assets_src/ogg/` en `audio_init()`, porque `GameState.bgm_track_id` (u16, fijo por
SPEC.md #8.2) tiene que sobrevivir a un guardado sin depender del `string_pool` de un
guion concreto (ADR-0034). `vm_resync_after_state_change` (M4, vacía hasta ahora) cobra
su primer uso real: restaura la pista de música, su posición aproximada y los volúmenes
de bus tras cargar o hacer rollback (criterio de M6). Mismo patrón de excepción a
heap_guard que Lua (ADR-0032) extendido a la primera carga de un sonido nuevo
(ADR-0035, sin volver a preguntar: es el mismo problema ya resuelto, no uno nuevo). Un
test bajo ASan encontró un bug real de miniaudio 0.11.21 (use-after-free al cargar un
archivo inexistente); se evita comprobando con `fopen` antes de llamar a la librería
(ADR-0036). Guion de prueba nuevo `demo_audio.vns` (`@bgm`/`@sfx`/`@stopbgm`). 85/85
tests en Ship; en Debug+ASan 85/86 (el de rendimiento de M2 no representativo sin
optimizar, ADR-0018), sin ningún reporte de memoria tras el fix del bug de miniaudio.
Sonidos de prueba: tonos sintéticos generados (no assets de terceros). No se verificó con
un contador de heap real que la excepción de heap_guard cubra el caso de un `@bgm`/`@sfx`
disparado dentro de una partida interactiva real (solo se probó vía `--autoplay-script`,
sin bucle de frame) — se apoya en la simetría de código con el caso de Lua, ya probado.
Windows sigue siendo la única plataforma verificada (ADR-0013). Detalle completo en
docs/DECISIONS.md.

M5 — Ramificación (hito anterior). `CmdKind` amplia con `SetVar`, `AddVar`,
`JumpIf`, `Choice`, `ChoiceEnd`, `Call`, `Return`, `LuaCall` (`sizeof(Cmd)` sigue en 16
bytes, verificado compilando). Parser reescrito con pila de bloques e indentacion
significativa (`script/lexer.h` gana un campo `indent`): `@if/@else/@end` se traduce a
`JumpIf`+`Jump`+etiquetas sinteticas (no hay `CmdKind::If`, SPEC.md #8.1 no lo tiene);
`@choice/@end` con opciones `"texto" [if var OP valor] -> etiqueta`; `@set`/`@add`/
`@call`/`@return`/`@lua`. Nombres de variable/flag se resuelven por `fnv1a % capacidad`
sin tabla de interning (ADR-0029). Tabla `ChoiceOption[]` anadida al `.vnc` (version 2,
ADR-0030); `Label[]` (ya en el formato desde M3 pero ignorado) ahora se usa de verdad
para `vn.jump()` (ADR-0031). Lua 5.4 + sol2 integrados (`script/lua_bindings.cpp`, unica
unidad de traduccion con sol2): `vn.get_var/set_var/get_flag/set_flag/jump/random`
funcionando, `vn.play_sfx` como no-op documentado hasta M6. Conflicto real detectado
entre ejecutar Lua y la regla de cero heap por frame: se paro y pregunto al usuario, que
eligio una excepcion documentada y acotada a `LuaCall` en `heap_guard_suspend/resume`
(ADR-0032); intérprete Lua persistente creado una vez en `lua_init()` (ADR-0033).
Rollback ahora tambien captura en `Choice` (cierra ADR-0027). Guion de prueba nuevo
`assets_src/scripts/demo_branching.vns` (3 ramas, 2 finales, SPEC.md #12), recorrido
completo verificado en tests (las 3 ramas por separado via `vm_select_choice`, incluida
la rama con opcion condicionada que se rechaza correctamente) y en
`--autoplay-script` (20 comandos, exit 0). 74/74 tests pasan en Ship; en Debug+ASan pasan
74/75 (el de rendimiento de M2 no es representativo sin optimizar, ADR-0018; el test
adicional que falta en Ship es uno de `arena_reset` que solo existe bajo `VN_DEBUG`), sin
ningun reporte de memoria. F5/F9/flechas de M4 y `@lua`/`@choice` de M5 no se probaron con
pulsaciones/decisiones reales en la ventana interactiva en este entorno (sin forma de
inyectar input real aqui); la logica esta probada exhaustivamente por tests
automatizados. Windows sigue siendo la unica plataforma verificada (ADR-0013). Detalle
completo en docs/DECISIONS.md.

M4 — Guardado, carga y rollback (hito anterior). `base/crc32` (IEEE 802.3,
vector de prueba `0xCBF43926` verificado), `vm/backlog` (200 entradas circulares),
`vm/rollback` (64 instantaneas, deshacer/rehacer con truncado de "futuro" al capturar tras
un retroceso), `vm/save` (formato `.vnsave` exacto de SPEC.md #8.3, magic+version+size+
CRC32+GameState+miniatura(0, diferida a M7, ADR-0026)+backlog). F5/F9/flechas cableadas en
`main.cpp` para probar a mano. Test obligatorio del skill `vne-serializable-state`
(`tests/test_save_replay.cpp`): guardar y recargar `demo.vns` en cada uno de sus 185
comandos da un `GameState`/`Backlog` byte a byte identico a una ejecucion sin
interrupciones, verificado 4 ejecuciones seguidas sin fallos intermitentes. Bug real
encontrado y arreglado en el proceso (ADR-0028): relleno de alineacion implicito en
`VmState`/`GameState`/`BacklogEntry` no sobrevivia de forma fiable a copias/escrituras
parciales bajo MSVC, convertido en campos `_pad` explicitos. 61/61 tests pasan en Ship; en
Debug+ASan pasan 60/61 (el de rendimiento de M2 no es representativo sin optimizar,
ADR-0018), sin ningun reporte de memoria. `--autoplay-script` verificado sin cambios
(185 comandos). Las teclas F5/F9/flechas no se probaron con pulsaciones reales en la
ventana interactiva en este entorno (sin forma de inyectar input real aqui); si estuviera
mal cableado el input especifico de M4, una prueba manual del usuario lo detectaria.
Rollback solo captura en `Say` por ahora; `Choice` se anadira en M5 (ADR-0027). Windows
sigue siendo la unica plataforma verificada (ADR-0013). Detalle completo en
docs/DECISIONS.md.

M3 — VM y DSL (hito anterior). `Cmd`/`CmdKind` (subconjunto de M3, ADR-0021),
interprete con `start`/`update`/`skip_to_end`, lexer/parser/compilador del DSL
(`vne_script_tools`, solo herramientas offline), formato `.vnc`, `--autoplay-script`.
Verificado en Windows (SPEC.md §12): guion de prueba de 204 lineas / 185 comandos se
ejecuta completo (`vne_game --autoplay-script`, exit 0, y en tiempo real dentro del juego,
`vm_pc` avanzando visible en el log); `@jump` a una etiqueta desconocida falla la
compilacion con archivo y linea exactos (verificado a mano y en test); `skip_to_end`
completa cualquier comando al instante (test + uso real en autoplay). 45/45 tests pasan en
Ship; en Debug+ASan pasan 45/46 (el de rendimiento de M2 no es representativo sin
optimizar, ADR-0018), sin ningun reporte de memoria. Identificador desconocido solo se
valida de verdad para etiquetas (ADR-0022: actor/pose/fondo se internan sin registro de
assets real, que todavia no existe). Windows sigue siendo la unica plataforma verificada
(ADR-0013). Detalle completo en docs/DECISIONS.md.

Verificacion adicional post-M3 (pedida por el usuario): el atlas procedural de M1
(ADR-0011) se sustituyo por un empaquetador shelf real, probado con 82 sprites CC0 reales
(Kenney UI Pack, `assets_src/png/`) — botones, flechas, estrellas — de tamaños variados
(16x16 a 192x64). Confirmado visualmente en el juego real (ADR-0025). El criterio de M1
(5000 sprites de un atlas en 1 draw call) se volvio a verificar con assets reales.

M2 — Texto (hito anterior): FreeType+HarfBuzz, atlas de glifos, word-wrap+kinsoku,
marcado inline, furigana, maquina de escribir. Verificado y confirmado visualmente.

M1 — Renderizado 2D (hito anterior): verificado en Windows, 5000 sprites de un atlas en 1
draw call + 1 de letterbox, >300 fps en build optimizada, letterbox correcto, cero allocs
de heap por frame, tests bajo ASan real. Backend GL escrito sin compilar (sin Linux
disponible), Metal/macOS sin implementar (ADR-0009).

Actualiza estas dos líneas al empezar y al terminar cada hito.

## Reglas que no se negocian

1. **Un hito a la vez.** No empieces M(n+1) hasta que M(n) cumpla todos sus criterios de
   aceptación en `docs/SPEC.md` §12. No esbozes trabajo de hitos futuros "para adelantar".
2. **Cero dependencias nuevas.** La lista cerrada está en `docs/SPEC.md` §3. Si crees que
   falta una, **párate y pregunta**. No la añadas.
3. **Cero rediseños por iniciativa propia.** Las secciones 4–8 de la especificación son
   decisiones tomadas. Si encuentras un problema real que las invalida, párate, explica el
   problema, propón la alternativa y espera respuesta.
4. **Cero asignaciones de heap en el bucle de frame.** Es un criterio verificable, no un
   ideal. Ver skill `vne-memory-model`.
5. **Ante ambigüedad, elige lo más simple** y regístralo en `docs/DECISIONS.md`.
6. **No inventes contenido de juego.** Si necesitas assets o guiones para probar, crea
   placeholders obvios (rectángulos de colores, texto lorem) y dilo.

## Prohibiciones de código

Sin excepciones. Sin RTTI. Sin `shared_ptr`. Sin `std::function` en rutas por frame.
Sin herencia de más de un nivel. Sin `virtual` en nada que se itere por elemento.
Sin `new`/`malloc` dentro del frame. Sin parsear texto en builds de release.

Detalle completo en el skill `vne-cpp-style`.

## Skills disponibles

Consúltalos antes de tocar el área correspondiente. Están en `.claude/skills/`.

| Skill | Cuándo |
|---|---|
| `vne-cpp-style` | Antes de escribir cualquier `.h` o `.cpp` |
| `vne-memory-model` | Al asignar memoria, crear pools o manejar recursos |
| `vne-serializable-state` | Al tocar `GameState`, guardado o rollback |
| `vne-script-dsl` | Al trabajar en el lenguaje de guion o el bytecode |
| `vne-rendering` | Al tocar `gfx/` o `text/` |
| `vne-milestone-workflow` | Al empezar y al cerrar cualquier hito |
| `vne-build-verify` | Al configurar el build o verificar criterios |

## Estructura

```
docs/SPEC.md         especificación completa (fuente de verdad)
docs/DECISIONS.md    registro de decisiones, se actualiza en cada hito
src/                 código del motor y del juego
tools/bake/          herramientas offline de horneado de assets
shaders/             GLSL fuente, compilado por sokol-shdc
assets_src/          assets en formato de autoría
assets_baked/        generado, en .gitignore
tests/               tests con doctest
```

## Comunicación

Trabaja en español. Cuando termines un hito, entrega un resumen corto con: qué se
implementó, qué criterios de aceptación se verificaron y cómo, qué quedó pendiente, y qué
decisiones nuevas se registraron. No adornes.

Si algo no se pudo verificar (por ejemplo, no hay macOS disponible para compilar), dilo
explícitamente en vez de darlo por bueno.
