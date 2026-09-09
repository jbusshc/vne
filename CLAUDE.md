# vne — instrucciones del proyecto

Motor de novela visual en C++20. Lee `docs/SPEC.md` completo antes de escribir código.
Este archivo es el resumen operativo; la especificación manda sobre él en caso de conflicto.

## Estado actual

**Hito activo:** ninguno. El siguiente por defecto es M12 (presentación y jugabilidad
completas), que ya puede apoyarse en el sistema de assets de M11 para las máscaras de
transición.

**Último hito completado:** M11 — Sistema de assets y empaquetado.
`platform/files.{h,cpp}` sobre SDL3 unifica el filesystem y deja `audio.cpp` sin ningún
`#if` de plataforma (la deuda que SPEC.md §2 nombraba). `assets/pak.{h,cpp}` implementa el
`.pak` de §11 con dos backends tras la misma interfaz (directorio suelto en Debug/Dev,
`.pak` residente en Ship) y `vne_bake pack` lo construye; **ningún cargador del motor abre
ya un archivo por ruta literal** — texturas, fuentes, guiones, mapas, catálogos y audio
resuelven nombres lógicos. `assets/assets.{h,cpp}` añade el hilo de IO (SDL_Thread + cola
circular de tamaño fijo con mutex/condición, sin heap) y la API de §7.4: `assets_texture`
es asíncrona con placeholder y el mismo handle pasa a la textura real al integrarla;
`assets_font`/`assets_sound` son síncronas a propósito (ADR-0053, ninguna tiene placeholder
que enseñar). `assets/hot_reload.{h,cpp}` vigila mtimes cada 500 ms y recarga `.ttf` y
`.png` en caliente, con recarga *en el sitio* que conserva el handle (`text_reload_font`,
`assets_reload_texture`); los `.vns` siguen en el editor porque recargarlos toca la VM.

Criterios verificados con números, no por encima: handle válido en **9-12 µs** (criterio
<100 µs), integrar el atlas cuesta **~8 ms** de los 16.6 ms de un frame — por eso se
integra **una** carga por llamada y no todas —, y Ship arranca y se juega **solo desde
`game.pak`** con `assets_baked/` y `assets_src/` renombrados (los tres guiones de demo
completan con exit 0, incluido `demo_audio.vns`, que ejercita el decode desde memoria; la
ventana real corre sin crash con `heap_allocs_frame_max=0`). La recarga en caliente de
`.png`, `.ttf` y `.vns` se comprobó tocando los tres archivos con el juego corriendo.
126/126 tests en Dev, 125/125 en Ship, 125/126 en Debug+ASan (solo el de rendimiento de
M2, no representativo sin optimizar, ADR-0018).

Cinco decisiones nuevas: ADR-0051 (`heap_guard` pasa a `thread_local` — el segundo hilo
habría corrido datos con el contador del principal; la solución no fue una cuarta excepción
a la regla de cero heap sino contabilidad por hilo), ADR-0052 (el hilo de IO solo lee
bytes; decode y GPU en el principal), ADR-0053, ADR-0054 (`.pak` leído a memoria, no
`mmap`) y ADR-0055 (audio empaquetado y catálogo de música horneado). `atlas.bin` sigue
sin nombres lógicos **a propósito**, no por olvido: no hay ningún consumidor que pida un
sprite por nombre todavía, y añadir una API sin llamante es lo que SPEC.md §1 dice que no
se hace. Lo necesita M15. Windows sigue siendo la única plataforma verificada (ADR-0013).
Detalle completo en docs/DECISIONS.md.

M0–M10 están cerrados. La hoja de ruta se amplió con **M11–M15** (ADR-0050) tras comprobar
que cerrar en M10 dejaba fuera partes enteras de la especificación: M11 (cerrado), M12
presentación y jugabilidad completas (`Move`/`Transition`, `{w=}`/`{speed=}`/`{b}` con
efecto real, polifonía, AABB), M13 integridad de datos y herramientas offline (validar
actores, detectar colisiones de hash, `vne_bake font`, TMX robusto), M14 configuración y
localización completas (`config.ini`, backlog relocalizable, `.vnsave` v3) y M15
interacción y testabilidad de la UI (ratón, grabar/reproducir input, arte de UI real,
visor de atlas). M15 necesita M11, M12 se apoya en él y el 3D de §13.2 también lo da por
supuesto.

**Portabilidad: se programa siempre, se verifica cuando haya máquinas.** Son dos cosas
distintas y no hay que confundirlas. La portabilidad es la prioridad 2 de SPEC.md §1 y
condiciona cada línea que escribes hoy: lo específico del SO va tras `platform/` (SDL3
cubre también filesystem), lo específico de GPU tras `gfx.h`, y todo `#if` de plataforma
lleva su rama no-Windows escrita aunque nadie la compile. Las reglas concretas están en
SPEC.md §2, "Cómo se programa la portabilidad". Nunca escribas código solo-Windows con la
excusa de que las demás plataformas son trabajo futuro.

Lo único aplazado (SPEC.md §13.1, ADR-0050) es **compilar y verificar** en Linux y macOS,
más escribir el backend Metal: falta el hardware, no el trabajo, así que no es un hito ni lo
propongas como tal. Y sigue diciendo explícitamente en cada cierre que Windows es lo único
comprobado. §13.2 (3D) sigue fuera de alcance hasta que el usuario lo pida.

**Revisión posterior a M10** (a petición del usuario: completar documentación y arreglar
lo que quedó suelto, sin entrar en §13). Documentación: `README.md` reescrito (estaba
parado en "M0 no iniciado, `src/` está vacío") y `docs/SCRIPT_LANGUAGE.md` escrito de
verdad — era un stub que prometía "se completa en M3" y siguió así hasta M10; se verificó
comando a comando contra `parser.cpp`/`lexer.cpp`/`compiler.cpp` en vez de copiar el
ejemplo de la especificación, lo que destapó que `@move` y `@transition` aparecen en
SPEC.md §9.1 y en el skill pero no existen en el parser (dan "comando desconocido").
Seis bugs reales arreglados, todos con test donde era posible: `glyph_cache_init/shutdown`
no los llamaba nadie y `shutdown` además dejaba la tabla apuntando a páginas de atlas
destruidas (se cablearon desde `main.cpp`, no desde `gfx_init`, porque eso habría
invertido las capas: `text/` depende de `gfx/` y nunca al revés); `vn.play_sfx` seguía
siendo el no-op de M5 pese a que `Sfx` existe desde M6; el `voice_id` de audio era
`índice + 1` sin validar generación (ahora la empaqueta en los 16 bits altos, +2 tests);
y tres defectos del escáner de TMX (`npos + 1` desbordando a 0, una `<property>`
filtrándose al objeto anterior, y un `<object/>` autocerrado comiéndose el siguiente).
Los tres de TMX no se podían testear porque el código vivía en el `main()` de `vne_bake`:
se movió a `src/script/map_bake.{h,cpp}` dentro de `vne_script_tools` con seis tests de
regresión (ADR-0049, única decisión nueva de esta fase). 113/113 tests en Ship, 114/114 en
Dev, 113/114 en Debug+ASan (el de rendimiento de M2, no representativo sin optimizar,
ADR-0018), sin ningún reporte de memoria. Los tres guiones de demo siguen ejecutándose
completos vía `--autoplay-script` (185/20/11 comandos, exit 0) y el juego arranca con
`heap_allocs_frame_max=0` y `text_layout_calls=1`. Varias entradas de "Pendientes
observados" que ya no eran ciertas quedaron marcadas como resueltas.

**Último hito completado:** M10 — Localización. `Cmd::say`/`ChoiceOption` ganan
`key_hash` (`fnv1a_u32` del texto original en español, SPEC.md #9.2 "clave estable...
hash"; `sizeof(Cmd)` sigue en 16 bytes). `vne_bake catalog-extract` recorre los guiones
y escribe el catálogo base (`assets_src/locale/es.csv`); `vne_bake catalog-compile` lo
hornea a `.vnl` binario — decisión explícita del usuario (ADR-0046, SPEC.md §14 lo
marcaba como una decisión que el agente no debe tomar solo: horneado, no suelto en texto
plano, por consistencia con el resto del pipeline). `text/catalog.{h,cpp}` resuelve una
clave contra el catálogo activo con caída al texto base si falta la traducción (nunca
texto vacío); solo el hash importa en runtime, no archivo:línea (ADR-0047). `MenuMode`
gana una fila de idioma que alterna español/japonés en caliente: `catalog_generation()`
fuerza a `VnMode` a reconstruir su `TextLayout` (sin relayoutear en ningún otro frame,
regla intacta) y a cargar la fuente correcta — la fuente CJK ahora se carga aparte de la
latina (antes `main.cpp` cargaba solo `NotoSansJP.ttf` para todo; M10 separó
`NotoSans.ttf` para el idioma base). La traducción de prueba (`ja.csv`) es un placeholder
mecánico marcado "[JA-placeholder]", no japonés real (ADR-0048, regla de "no inventar
contenido" aplicada también a idiomas). 105/105 tests en Ship; en Debug+ASan 105/106 (el
de rendimiento de M2 no representativo sin optimizar, ADR-0018), sin ningún reporte de
memoria. El cambio de idioma se verificó con tests sobre `catalog.cpp`, no pulsando
flechas en la ventana interactiva en este entorno (misma limitación de siempre). El
backlog no se relocaliza (se queda en el idioma en que se dijo cada línea); el idioma
activo no persiste entre sesiones (no hay `config.ini` todavía en el proyecto). Windows
sigue siendo la única plataforma verificada (ADR-0013). Detalle completo en
docs/DECISIONS.md.

M9 — MapMode (hito anterior). `src/game/map_format.h` define un formato
`.vnm` propio (ADR-0043: SPEC.md #11 no da el layout, a diferencia de `.vnc`/`.vnsave`) —
rejilla de tiles, bits de colisión, triggers con ruta a un `.vnc`. `tools/bake/main.cpp`
(`vne_bake map`) escanea el subconjunto de TMX que este proyecto autora (una capa
"tiles", una "collision", ambas CSV sin comprimir, un `objectgroup` de rectángulos) con
un escáner de subcadenas, sin parser XML general ni dependencia nueva (ADR-0044) — un bug
real (`"<object"` encontraba `"<objectgroup"` por ser prefijo) se detectó con un test y
se corrigió antes de cerrar el hito. `GameState` sube de v1 a v2 (`map_id`,
`player_x/player_y`, añadidos al final; migración `migrate_v1_to_v2` escrita en el mismo
commit, ADR-0045) para que guardar/cargar dentro del mapa funcione. `src/game/map_mode.*`
implementa colisión por rejilla (sin motor de físicas), movimiento con WASD (las flechas
las usa el rollback en la base de la pila desde M4), y triggers que apilan una `VnMode`
nueva; volver de esa escena repone `MapMode` con la posición del jugador intacta (ya vive
en `GameState`). La pila de modos ahora empieza en `MapMode`, no en `VnMode` directamente
— "caminar por un mapa" es el punto de entrada que pide el criterio de M9. 98/99 tests en
Debug+ASan (el de rendimiento de M2 no representativo sin optimizar, ADR-0018) y 98/98 en
Ship, sin ningún reporte de memoria. El flujo completo (caminar, pisar el trigger, jugar
la escena, volver al mapa) se verificó con tests automatizados sobre la lógica de
`MapMode` y un arranque sin crashear (`heap_allocs_frame_max=0`), no con WASD real en la
ventana interactiva (misma limitación de siempre en este entorno). Solo hay un mapa
(`map_id` fijo a mano, sin catálogo por id como el de música de M6). Windows sigue siendo
la única plataforma verificada (ADR-0013). Detalle completo en docs/DECISIONS.md.

M8 — Editor (hito anterior). Dear ImGui (rama docking) integrado vía
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
docs/SPEC.md            especificación completa (fuente de verdad)
docs/DECISIONS.md       registro de decisiones, se actualiza en cada hito
docs/SCRIPT_LANGUAGE.md referencia del DSL, para quien escribe guiones
src/                    código del motor y del juego (los modos viven en src/game/)
tools/bake/             herramientas offline de horneado de assets
assets_src/             assets en formato de autoría
assets_baked/           generado, en .gitignore
tests/                  tests con doctest
.claude/skills/         guías por área, léelas antes de tocar la suya
```

Dos directorios existen pero están **vacíos**, y conviene saberlo antes de buscar algo
dentro: `shaders/` (SPEC.md §11 los quería en GLSL compilados por `sokol-shdc`, pero
ADR-0010 decidió escribirlos a mano por backend y viven en `src/gfx/shaders.h`) y
`src/assets/` (SPEC.md §7.4 nunca se implementó; lo construye M11). `src/modes/` tampoco
tiene nada: los modos acabaron en `src/game/`.

## Comunicación

Trabaja en español. Cuando termines un hito, entrega un resumen corto con: qué se
implementó, qué criterios de aceptación se verificaron y cómo, qué quedó pendiente, y qué
decisiones nuevas se registraron. No adornes.

Si algo no se pudo verificar (por ejemplo, no hay macOS disponible para compilar), dilo
explícitamente en vez de darlo por bueno.
