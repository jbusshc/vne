# Registro de decisiones

Append-only. Una entrada por decisión no trivial. No se editan entradas antiguas: se marcan
como sustituidas por una nueva.

Plantilla:

```markdown
## ADR-00NN — Título corto

**Fecha:** AAAA-MM-DD
**Hito:** MN
**Estado:** aceptada | sustituida por ADR-00XX

**Contexto.** Qué problema apareció.

**Decisión.** Qué se eligió.

**Alternativas descartadas.** Qué más se consideró y por qué no.

**Consecuencias.** Qué se vuelve más fácil y qué más difícil a partir de ahora.
```

---

## ADR-0001 — sokol_gfx como capa RHI en lugar de OpenGL directo o SDL_GPU

**Fecha:** 2026-09-06
**Hito:** pre-M0
**Estado:** aceptada

**Contexto.** Hacía falta una capa de abstracción gráfica portable que no limitara el
soporte futuro de 3D ni la publicación web.

**Decisión.** sokol_gfx, con backends GL 3.3, GLES3, D3D11, Metal y WebGL2. Shaders
compilados offline con sokol-shdc.

**Alternativas descartadas.** OpenGL directo: obsoleto en macOS, sin ruta a móvil ni a web
moderna, y no aporta control real frente a sokol. SDL_GPU: sería una dependencia menos, pero
su backend de WebGPU sigue siendo un PR experimental y sus requisitos (Vulkan, D3D12, Metal)
descartan hardware antiguo. bgfx: más pesado y opinionado de lo necesario para un VN 2D.

**Consecuencias.** Se puede exportar a WebGL2 para demos jugables en navegador, y el
requisito mínimo baja a GL 3.3. A cambio, no hay acceso a features de Vulkan o D3D12. Si
algún día se necesitan, el RHI está aislado tras `gfx.h`.

---

## ADR-0002 — Estado del juego en una única struct trivialmente copiable

**Fecha:** 2026-09-06
**Hito:** pre-M0
**Estado:** aceptada

**Contexto.** El guardado en cualquier punto y el rollback son requisitos del género y no se
pueden retrofitear si el estado vive repartido en objetos y en el stack de C++.

**Decisión.** Todo el estado mutable vive en `GameState`, con capacidades fijas y
`static_assert(std::is_trivially_copyable_v<GameState>)`. Guardar es un `memcpy`. El rollback
es un buffer circular de instantáneas.

**Alternativas descartadas.** Serialización por reflexión: requiere infraestructura que este
proyecto no quiere. Rollback por reejecución determinista: obligaría a que todo el motor sea
determinista, con un coste continuo muy superior al de 256 KB de instantáneas.

**Consecuencias.** El motor no necesita ser determinista. A cambio, ningún dato persistente
puede usar contenedores dinámicos, y ampliar una capacidad implica subir la versión del
formato de guardado.

---

## ADR-0003 — Comandos del guion como tagged union en lugar de jerarquía virtual

**Fecha:** 2026-09-06
**Hito:** pre-M0
**Estado:** aceptada

**Contexto.** El intérprete del guion necesita ser serializable, contiguo en memoria y fácil
de extender.

**Decisión.** `struct Cmd` con un `CmdKind` y una unión de structs POD. El intérprete es un
`switch` sin `default`, para que `-Wswitch` obligue a completar cada caso al añadir un
comando.

**Alternativas descartadas.** Jerarquía de clases con `virtual`: impide volcar el guion a
disco tal cual, fragmenta la memoria y requiere asignación por comando.

**Consecuencias.** Añadir un comando toca exactamente cuatro sitios, todos localizados. El
tamaño de `Cmd` está fijado por `static_assert` y ampliarlo es una decisión consciente.

---

## ADR-0004 — Dos lenguajes de scripting: DSL propio para diálogo, Lua para lógica

**Fecha:** 2026-09-06
**Hito:** pre-M0
**Estado:** aceptada

**Contexto.** Un único lenguaje siempre falla en uno de los dos frentes: Lua es incómodo para
escribir diálogo y un DSL de diálogo es incómodo para lógica.

**Decisión.** DSL propio compilado a bytecode `.vnc` para diálogo y flujo. Lua 5.4 con sol2
para lógica, confinado a una única unidad de traducción.

**Alternativas descartadas.** Solo Lua: hostil para el trabajo de escritura. ink con inkcpp:
buena opción, descartada por preferir control total sobre el formato compilado y su
integración con `GameState`.

**Consecuencias.** Hay que mantener un lexer, un parser y un compilador propios. A cambio, el
runtime no parsea texto y los errores de guion son de compilación.

---

## ADR-0005 — Arenas lineales y handles con generación en lugar de asignación dinámica

**Fecha:** 2026-09-06
**Hito:** pre-M0
**Estado:** aceptada

**Contexto.** El objetivo de frame times planos es incompatible con asignación dinámica
dentro del bucle de frame.

**Decisión.** Tres arenas (permanente, escena, frame) y handles de 8 bytes con contador de
generación para todos los recursos. Cero asignaciones de heap por frame, verificado con un
contador instrumentado.

**Alternativas descartadas.** Smart pointers: coste de contador atómico, fragmentación, y no
son serializables. Un allocator general personalizado: más complejo y sin la garantía de
tiempo constante de un bump allocator.

**Consecuencias.** Los punteros devueltos por la resolución de handles son válidos solo en el
ámbito actual, lo cual es una restricción real que hay que respetar en todo el código.

---

## ADR-0006 — SDL_Renderer como sustituto temporal de sokol_gfx solo para M0

**Fecha:** 2026-09-06
**Hito:** M0
**Estado:** aceptada

**Contexto.** El criterio de aceptación de M0 pide una ventana que "abre y limpia a un
color", pero sokol_gfx (la RHI definitiva, SPEC.md §3 y §7.1) no se integra hasta M1. Hacía
falta limpiar la pantalla sin adelantar trabajo de M1 ni introducir dependencias nuevas.

**Decisión.** M0 usa el `SDL_Renderer` que ya trae SDL3 (`platform/window.cpp`) únicamente
para `platform_window_clear`/`platform_window_present`. No se expone fuera de
`platform/window.*`: nada en `base/` ni en el futuro `gfx/` depende de el.

**Alternativas descartadas.** Adelantar la integracion de sokol_gfx a M0: viola la regla de
un hito a la vez. Limpiar la ventana a mano con `SDL_GetWindowSurface`: mas codigo para el
mismo resultado y sin vsync integrado.

**Consecuencias.** Todo el codigo de `platform/window.cpp` que toca `SDL_Renderer` se
descarta por completo en M1 cuando `gfx_init`/`gfx_present` tomen el control. El resto del
motor (arenas, handles, pools) no sabe que existe.

---

## ADR-0007 — Ausencia de UBSan y ASan condicionado en la build Debug de Windows

**Fecha:** 2026-09-06
**Hito:** M0
**Estado:** aceptada (ASan resuelto el mismo dia; UBSan sigue sin equivalente en MSVC)

**Contexto.** SPEC.md §15 exige que Debug pase bajo ASan y UBSan. MSVC no implementa UBSan
en absoluto (no existe equivalente). Ademas, la instalacion de Visual Studio disponible en
esta maquina de desarrollo no traia el runtime `clang_rt.asan` para x64 (faltaba el
componente individual "C++ AddressSanitizer" del VS Installer), y no se pudo instalar en un
primer intento por no haber permisos de administrador en la sesion.

**Decisión.** El `CMakeLists.txt` detecta en configuracion si el runtime de ASan esta
disponible (`find_file` sobre `clang_rt.asan_dynamic_runtime_thunk-x86_64.lib`) y solo anade
`/fsanitize=address` a Debug si lo encuentra; si no, emite un `message(WARNING ...)` explicito
y compila sin el, en vez de fallar todo el build por un componente opcional ausente.

**Alternativas descartadas.** Bloquear el build hasta instalar el componente: dejaria M0
completamente parado por una limitacion de la maquina, no del codigo. Forzar la flag sin
comprobar: rompe el link con un error dificil de diagnosticar ("cannot open file
clang_rt.asan_dynamic_runtime_thunk-x86_64.lib").

**Consecuencias.** El usuario reinicio la sesion con permisos de administrador e instalo el
componente "C++ AddressSanitizer" (`vs_installer.exe modify --add
Microsoft.VisualStudio.Component.VC.ASAN`). Con el runtime presente, `vne_tests.exe` (7/7
tests, 32/32 asserts) y `vne_game.exe` corrieron bajo `/fsanitize=address` real sin ningun
reporte; el contador de heap por frame se mantuvo en 0 tambien bajo instrumentacion ASan. La
deteccion en CMake se deja tal cual: sigue siendo util si el proyecto se clona en otra
maquina sin el componente instalado. Nota de entorno: el binario instrumentado necesita
`clang_rt.asan_dynamic-x86_64.dll` (en
`VC\Tools\MSVC\<version>\bin\Hostx64\x64\`) en el `PATH` en tiempo de ejecucion; sin el,
falla con "error while loading shared libraries" en vez de un error de ASan. UBSan sigue sin
verificarse: no existe en MSVC, asi que esta garantia solo se puede confirmar compilando en
Linux o macOS con GCC/Clang, que siguen sin estar disponibles en este entorno.

---

## ADR-0008 — Formato de textura horneada: QOI

**Fecha:** 2026-09-06
**Hito:** M1
**Estado:** aceptada

**Contexto.** SPEC.md §14 dejaba explícitamente sin decidir el formato final de textura
horneada (QOI vs BCn/ASTC) y prohibía al agente decidirlo solo. Se preguntó al usuario.

**Decisión.** QOI ("Quite OK Image"). Se usa el codec de referencia de un solo header
(`qoi.h`, dominio público) para codificar offline en `vne_bake` y decodificar en runtime en
`src/assets`/`src/gfx`. Coincide con lo que ya insinuaba la tabla de pipeline de SPEC.md §11
(`atlas_NN.qoi`).

**Alternativas descartadas.** BCn/ASTC: compresión real en GPU y menos VRAM, pero exige un
compresor offline más complejo y una ruta distinta por backend (BC en D3D/GL, ASTC en
móvil/Metal). Se descarta por ahora: el proyecto no maneja todavía volumen de texturas que lo
justifique, y puede añadirse después sin romper la interfaz de `texture_load`.

**Consecuencias.** Sin compresión real de GPU: más VRAM por textura que con BCn/ASTC. La
decodificación de `.qoi` en runtime es binaria y rápida (no es "parsear texto"), así que no
viola la regla de SPEC.md §4 de no parsear texto en release.

---

## ADR-0009 — Backend Metal (macOS) diferido, sin código sin probar

**Fecha:** 2026-09-06
**Hito:** M1
**Estado:** aceptada

**Contexto.** M1 requiere sokol_gfx funcionando en los tres backends de prioridad 1 y 2
(D3D11, GL 3.3, Metal). No hay una Mac disponible en este entorno para compilar ni probar el
backend Metal.

**Decisión.** M1 implementa y dejar verificados D3D11 (Windows) y GL 3.3 (Linux, compilado
condicionalmente pero sin poder ejecutarse en esta máquina). El backend Metal se deja sin
implementar, con un hueco explícito documentado aquí y en el cierre del hito, en vez de
escribir Objective-C++ sin poder compilarlo ni probarlo.

**Alternativas descartadas.** Escribir el `.mm` de todas formas siguiendo los ejemplos de
sokol: se descarta porque un backend gráfico con errores no detectados hasta la primera
compilación real en una Mac es peor que no tenerlo, y además impediría verificar M1 con
honestidad.

**Consecuencias.** M1 no está realmente cerrado hasta que alguien con acceso a macOS
implemente y verifique `gfx_backend_metal.mm`. Queda anotado en "Pendientes observados".

---

## ADR-0010 — Shaders escritos a mano en vez de invocar el binario sokol-shdc

**Fecha:** 2026-09-06
**Hito:** M1
**Estado:** aceptada

**Contexto.** SPEC.md §3 y §7.1 especifican que los shaders se compilan offline con
`sokol-shdc`. Ese programa es un binario prebuilt por plataforma (no una librería C++
descargable por CPM), y automatizar su descarga y ejecución como paso de build es trabajo
adicional no trivial para un hito que solo necesita dos shaders sencillos (sprite y blit de
letterbox) en dos backends verificables aquí.

**Decisión.** `src/gfx/shaders.h` contiene a mano un `sg_shader_desc` por shader, con el
código fuente HLSL5 (D3D11) y GLSL330 (GL) embebido como los generaría `sokol-shdc`, elegido
en runtime según `sg_query_backend()`. Sigue sin haber parseo de texto de guion ni de
assets del juego; es shader GPU, compilado una vez al crear el pipeline, igual que si
`sokol-shdc` hubiera generado bytecode.

**Alternativas descartadas.** Integrar `sokol-shdc` como paso de CMake que descarga el
binario y lo ejecuta sobre `shaders/*.glsl`: correcto a largo plazo, pero prematuro para dos
shaders fijos; se puede migrar sin romper la API de `gfx.h` cuando el número de shaders
crezca (M2 en adelante, con el shader de texto).

**Consecuencias.** Añadir un shader nuevo implica escribir su HLSL y GLSL a mano en vez de un
único `.glsl` anotado. Cuando el conteo de shaders crezca esto se volverá tedioso; anotado en
"Pendientes observados" para reconsiderar entonces.

---

## ADR-0011 — Atlas de M1 es una rejilla procedural, no un empaquetador real

**Fecha:** 2026-09-06
**Hito:** M1
**Estado:** aceptada

**Contexto.** El criterio de aceptación de M1 solo exige demostrar 5000 sprites de **un**
atlas en una draw call a más de 300 fps; no exige un empaquetador de atlas real (eso es
`vne_bake atlas` completo, con `assets_src/png/*.png` reales, que no existen todavía: SPEC.md
§6 prohíbe inventar contenido de juego).

**Decisión.** `tools/bake` genera proceduralmente una textura de rejilla de colores sólidos
(placeholder obvio) y la codifica a `.qoi`; un `.bin` trivial describe la rejilla de subrects
de forma fija (N×N celdas iguales). No hay empaquetador de rectángulos de tamaño variable.

**Alternativas descartadas.** Escribir un empaquetador de atlas real (shelf/skyline) ahora:
trabajo de un hito futuro sin assets reales que lo justifiquen todavía; se implementará
cuando `assets_src/png/` tenga sprites reales que empaquetar.

**Consecuencias.** El formato del `.bin` de M1 es deliberadamente el más simple posible y se
espera que cambie cuando llegue el empaquetador real; no es el formato final de
`atlas.bin` de SPEC.md §11.

---

## ADR-0012 — Swapchain D3D11 en modo flip y capa de depuracion con fallback

**Fecha:** 2026-09-06
**Hito:** M1
**Estado:** aceptada

**Contexto.** Dos problemas aparecieron al medir el criterio de M1 (5000 sprites, 1 draw
call, >300 fps) en esta maquina: (1) `D3D11_CREATE_DEVICE_DEBUG` fallaba con
`DXGI_ERROR_SDK_COMPONENT_MISSING` porque el componente opcional de Windows "Graphics
Tools" no esta instalado; (2) el modelo de swapchain "blit" clasico
(`DXGI_SWAP_EFFECT_DISCARD`) anade overhead de composicion de escritorio (DWM) en modo
ventana.

**Decisión.** `gfx_backend_d3d11.cpp` intenta crear el dispositivo con la capa de
depuracion solo en `VN_DEBUG`, y si falla, reintenta sin ella en vez de abortar. El
swapchain usa `DXGI_SWAP_EFFECT_FLIP_DISCARD` (modelo flip) en vez de `DISCARD`.

**Alternativas descartadas.** Exigir el componente de Graphics Tools como requisito de
build: bloquearia compilar en cualquier maquina sin ese componente opcional instalado, por
un beneficio (breakpoints de validacion D3D) que no es necesario para pasar los criterios
de aceptacion. Mantener el modelo "blit": mas simple pero con mayor latencia/overhead de
`Present()` en ventana, medible en el frame_p99 reportado por el HUD.

**Consecuencias.** La build Debug en una maquina sin "Graphics Tools" pierde la validacion
extra de D3D11 (queda un `log_warn`, no silencioso). Verificado en esta maquina: 5000
sprites en 2 draw calls (1 del atlas + 1 del blit de letterbox), ~450-500 fps con vsync
desactivado en build Dev optimizada (ver cierre de M1 en el resumen de la conversacion),
frame_p99 ~3.2-3.6 ms. La primera captura de pantalla automatizada salio en blanco (la
ventana no tenia foco todavia cuando se disparo el screenshot); con la ventana enfocada se
confirmo visualmente el atlas renderizado sin el placeholder magenta, y redimensionando la
ventana a un aspecto 4:3 se vio la barra negra de letterbox exactamente donde predice
`gfx_letterbox_rect`, sin distorsion del contenido.

---

## ADR-0013 — Windows como única plataforma verificada por ahora; Linux/macOS quedan abiertos, no bloqueantes

**Fecha:** 2026-09-06
**Hito:** M1 (cierre)
**Estado:** aceptada

**Contexto.** SPEC.md §15 exige compilar limpio en las tres plataformas de prioridad 1 y 2
antes de cerrar un hito. Este entorno de desarrollo solo tiene Windows disponible. El
usuario confirmó explícitamente que sacar el motor primero en Windows es la prioridad
real ahora mismo, y que Linux/macOS no son imperativos en este momento.

**Decisión.** M1 (y, mientras no cambie esta indicación, los hitos siguientes) se dan por
cerrados verificando solo Windows, siempre que la arquitectura no cierre la puerta a Linux
y macOS: toda dependencia de plataforma pasa por una costura aislada (`gfx_backend.h`,
`platform/window.h`) en vez de mezclarse con el resto del motor. El backend GL para Linux
se escribe junto con el de D3D11 aunque no se pueda compilar ni probar aquí; el backend
Metal se documenta como hueco explícito (ADR-0009) en vez de simularse.

**Alternativas descartadas.** Bloquear cada hito hasta tener acceso a Linux y macOS:
pararía todo el proyecto por una limitación del entorno de desarrollo, no del diseño.
Ignorar el multiplataforma por completo y acoplar el codigo a Windows: violaría SPEC.md
§1 (portabilidad es la prioridad #2 del proyecto) y obligaría a un rediseño caro más
adelante.

**Consecuencias.** Cada cierre de hito debe seguir diciendo explícitamente qué no se
verificó (Linux, macOS) en vez de darlo por bueno, tal como ya pedía CLAUDE.md. El primer
build real en Linux o macOS puede descubrir errores en código nunca compilado (el backend
GL, sobre todo) — no es una garantía, es una apuesta consciente a favor de avanzar.

---

## ADR-0014 — FreeType antes que HarfBuzz en CMake para activar HB_HAVE_FREETYPE

**Fecha:** 2026-09-06
**Hito:** M2
**Estado:** aceptada

**Contexto.** El CMakeLists.txt de HarfBuzz 9.0.0 detecta automaticamente si existe un
target `freetype` ya definido (`if (TARGET freetype)`) para activar `HB_HAVE_FREETYPE` y
enlazarlo, en vez de exponer una opcion explicita que se pueda forzar desde fuera.

**Decisión.** `CPMAddPackage(freetype)` se llama antes que `CPMAddPackage(harfbuzz)` en
CMakeLists.txt. FreeType se configura con `FT_DISABLE_HARFBUZZ=ON` (evita una dependencia
circular: FreeType puede opcionalmente usar HarfBuzz para su propio autohinting, pero
HarfBuzz todavia no existe como target en ese punto de la configuracion).

**Alternativas descartadas.** Ninguna: es la unica forma documentada de que el CMake de
HarfBuzz 9.0.0 use FreeType sin parchear su CMakeLists.txt.

**Consecuencias.** El orden de los dos `CPMAddPackage` en CMakeLists.txt es significativo
y no se puede reordenar sin perder la integracion FreeType-HarfBuzz.

---

## ADR-0015 — Atlas de glifos: R8 de cobertura replicado a RGBA8 en vez de un shader nuevo

**Fecha:** 2026-09-06
**Hito:** M2
**Estado:** aceptada

**Contexto.** SPEC.md §7.2 pide un atlas de glifos en formato R8. El pipeline de sprites
de M1 (ADR de M1, `shaders.h`) espera una textura RGBA8 y hace `color * tex.rgba`; anadir
un shader de texto aparte (que interprete R8 como mascara de alpha) duplicaria pipeline,
shader y logica de bindings solo para dibujar texto.

**Decisión.** El CPU-side glyph packer (`text/glyph_cache.cpp`) sigue tratando cada texel
como un byte de cobertura de FreeType, pero al subirlo a la GPU lo replica en los 4
canales (R=G=B=A=cobertura) de una textura RGBA8 (`texture_create_dynamic`/
`texture_update_dynamic` en `gfx/texture.h`). Con blend premultiplicado
(ONE, ONE_MINUS_SRC_ALPHA) esto da exactamente `color.rgb*cobertura, color.a*cobertura)`,
el resultado premultiplicado correcto, usando el pipeline de sprites de M1 sin cambios.

**Alternativas descartadas.** Shader de texto dedicado con textura R8 real: mas fiel a la
letra de SPEC.md §7.2, pero cuadruplica el trabajo de este hito para un ahorro de memoria
(4x menos bytes en el atlas) que no es un criterio de aceptacion de M2. Se puede migrar
mas adelante sin cambiar la API publica de `text/`.

**Consecuencias.** El atlas de glifos usa 4x mas memoria de la estrictamente necesaria
(1024x1024x4 = 4 MB por pagina en vez de 1 MB). Con 4 paginas maximo son 16 MB — aceptable
para el motor, revisar si algun dia importa el presupuesto de VRAM en plataformas moviles.

---

## ADR-0016 — glyph_cache_flush_dirty_pages() se llama una vez por frame, no dentro de text_layout()

**Fecha:** 2026-09-06
**Hito:** M2
**Estado:** aceptada

**Contexto.** Bug real encontrado en tests: `sg_update_image` de sokol_gfx solo admite
**una** subida por imagen y por frame (asercion interna `VALIDATE_UPDIMG_ONCE`). La
primera version de `text_layout()` llamaba a `glyph_cache_flush_dirty_pages()` al final de
cada layout; en la suite de tests, dos `text_layout()` sobre la misma pagina de atlas sin
un `sg_commit()` de por medio (los tests nunca dibujan un frame real) hacian abortar el
proceso. El mismo problema aparece en el juego real si dos cuadros de dialogo se inicializan
en el mismo frame (p. ej. al cargar una escena).

**Decisión.** `text_layout()` ya no sube nada a la GPU. `glyph_cache_flush_dirty_pages()`
se expone en `text/glyph_cache.h` y el bucle de frame (`main.cpp`) lo llama exactamente una
vez por frame, despues de todos los `text_draw()`/`text_layout()` de ese frame y antes de
`gfx_flush()`. Los tests de `text_layout()` no llaman a `glyph_cache_flush_dirty_pages()`
en absoluto porque no verifican pixeles, solo geometria.

**Alternativas descartadas.** Subir la textura dentro de `text_layout()` y aceptar la
limitacion de "una sola llamada a text_layout por frame por pagina compartida": demasiado
fragil e implicito, se rompe en cuanto el juego real muestre dos textos a la vez.

**Consecuencias.** Cualquier codigo que llame a `text_layout()` fuera del bucle de frame de
`main.cpp` (por ejemplo, en un test o una herramienta) tiene que acordarse de llamar a
`glyph_cache_flush_dirty_pages()` el si de verdad va a dibujar ese texto; si solo necesita
la geometria del layout, no hace falta.

---

## ADR-0017 — Fuentes de prueba: Noto Sans y Noto Sans JP (Google, licencia OFL)

**Fecha:** 2026-09-06
**Hito:** M2
**Estado:** aceptada

**Contexto.** `assets_src/ttf/` estaba vacio y SPEC.md §6 prohibe inventar contenido de
juego; M2 no puede probar FreeType/HarfBuzz, el word-wrap latino, el kinsoku CJK ni la
furigana sin fuentes .ttf reales. Se preguntó al usuario como conseguirlas.

**Decisión.** Se descargaron `NotoSans[wdth,wght].ttf` y `NotoSansJP[wght].ttf` (fuentes
variables) del repositorio `google/fonts` (licencia OFL, libre y redistribuible) a
`assets_src/ttf/NotoSans.ttf` y `NotoSansJP.ttf`, con sus textos de licencia
(`OFL-NotoSans.txt`, `OFL-NotoSansJP.txt`). Son fuentes de desarrollo/prueba del motor, no
assets finales del juego.

**Alternativas descartadas.** Usar fuentes ya instaladas en Windows (Segoe UI, Yu Gothic):
descartado por el usuario a favor de Noto, que es multiplataforma y no depende de lo que
haya instalado cada maquina de desarrollo.

**Consecuencias.** `NotoSansJP.ttf` pesa ~9.5 MB: infla el repositorio de git de forma
notable. Si esto molesta mas adelante, se puede mover a Git LFS o sustituir por un
subconjunto de glifos, pero no es necesario para M2. `vne_game` copia `NotoSansJP.ttf` al
directorio de build (CMakeLists.txt) para poder cargarla con una ruta relativa a su propio
directorio de trabajo.

---

## ADR-0018 — Rendimiento de layout: mediana de 1000 muestras, no el peor caso absoluto

**Fecha:** 2026-09-06
**Hito:** M2
**Estado:** aceptada

**Contexto.** SPEC.md §12 exige que un parrafo de 500 caracteres se relayoutee en menos de
1 ms. Midiendo el **peor caso absoluto** de 1000 relayouts se observaron valores muy
inestables (441 us, 1013 us, 1175 us, 565 us en corridas consecutivas sin cambiar una
linea de codigo), causados por interrupciones del planificador de Windows ajenas al
motor, no por el coste real de `text_layout()`.

**Decisión.** `tests/test_layout_perf.cpp` mide la **mediana** de 1000 muestras como
criterio de paso/fallo (establemente ~276-278 us en esta maquina, en una build optimizada
sin ASan), y registra el peor caso solo informativamente, sin usarlo para aprobar o
reprobar el test.

**Alternativas descartadas.** Relajar el umbral de 1 ms para que el peor caso siempre
pase: esconde el numero real en vez de medirlo bien. Repetir todo el experimento varias
veces y quedarse con el mejor: mas lento y no mas honesto que usar directamente la
mediana.

**Consecuencias.** El test ya no detecta un solo pico aislado de latencia como fallo; solo
detecta una regresion sostenida del coste tipico de `text_layout()`. Medido unicamente en
la build Dev (optimizada, sin ASan): en Debug+ASan sin optimizar el mismo layout tarda
~8 ms, muy por encima del criterio, porque ASan y `-Od` no representan el rendimiento
real (mismo patron que el criterio de fps de M1).

---

## ADR-0019 — glyph_cache_flush_dirty_pages() se autoprotege contra una segunda subida en el mismo frame

**Fecha:** 2026-09-06
**Hito:** M2
**Estado:** aceptada

**Contexto.** ADR-0016 movio la subida de texturas del atlas fuera de `text_layout()` para
que solo ocurra una vez por frame, pero la unica proteccion real era la disciplina de
tener un solo punto de llamada (`main.cpp`). En cuanto un hito futuro (M7, con varios
cuadros de dialogo o un modo skip que dispare varios `text_layout` seguidos) añada un
segundo punto de llamada sin conocer esa regla, vuelve el mismo crash: `sg_update_image`
solo admite una subida por imagen y por frame.

**Decisión.** `glyph_cache_begin_frame()` (nueva, se llama junto a `gfx_begin_frame()`)
resetea una bandera `g_flushed_this_frame`. `glyph_cache_flush_dirty_pages()` la consulta:
si ya subio algo este frame, la llamada de mas se ignora con un `log_warn`, en vez de
llamar a `sg_update_image` una segunda vez. Las paginas que quedaron sin subir siguen
`dirty` y se suben en el siguiente frame — un frame de retraso en esa actualizacion
puntual, no un crash.

**Alternativas descartadas.** Dejarlo solo documentado (la version anterior de esta ADR):
suficiente mientras solo exista un punto de llamada, pero es una mina enterrada para
cuando aparezca el segundo. Forzar `sg_commit()` extra para "cerrar" el frame antes de
cada flush: mas invasivo, y `sg_commit()` no es gratis ni es responsabilidad de `text/`
decidir cuando termina un frame.

**Consecuencias.** Cualquier codigo que llame a `text_layout()`/`glyph_cache_flush_dirty_pages()`
fuera del bucle de frame de `main.cpp` (tests, herramientas) debe llamar tambien a
`glyph_cache_begin_frame()` si le importa que la subida a GPU ocurra de verdad; si no lo
hace, sigue sin crashear, simplemente pospone la subida. Test de regresion en
`tests/test_glyph_cache.cpp`.

---

## ADR-0020 — text_layout() comprueba null tras cada arena_alloc, no asume que siempre hay espacio

**Fecha:** 2026-09-06
**Hito:** M2
**Estado:** aceptada

**Contexto.** Al escribir el test de regresion de ADR-0019 con una arena de prueba
demasiado chica (64 KB), `arena_alloc_n<Chunk>` devolvio `nullptr` (comportamiento
correcto y documentado: la arena esta llena) pero `chunk_segment()` escribio en ese
puntero nulo igualmente, provocando un access-violation real detectado por ASan. `text_layout()`
nunca comprobaba el resultado de sus propias llamadas a `arena_alloc_n` para `segments`,
`chunks`, `line_starts` ni `quads`.

**Decisión.** Las cuatro asignaciones criticas de `text_layout()` comprueban `nullptr` y,
si falta espacio, `log_error` y devuelven un `TextLayout` vacio (`count == 0`) en vez de
escribir en memoria invalida. Coherente con la regla general de arenas (SPEC.md §6.1: "el
llamante debe comprobarlo").

**Alternativas descartadas.** Ninguna: es la regla que ya existia para el resto del motor
(ver `arena_alloc` en `base/arena.cpp`), simplemente no se habia aplicado aqui todavia.

**Consecuencias.** Un texto que no cabe en la arena que se le paso ya no crashea el juego:
se ve como si no hubiera texto (`TextLayout::count == 0`), con un error en el log que dice
por que. El llamante sigue siendo responsable de pasar una arena razonable (en el juego
real, `g_arena_scene`, con margen de sobra).

---

## ADR-0021 — CmdKind/Cmd de M3 solo con el subconjunto de este hito; sizeof(Cmd) crecera

**Fecha:** 2026-09-06
**Hito:** M3
**Estado:** aceptada

**Contexto.** SPEC.md §8.1 da `CmdKind`/`Cmd` con los 22 comandos finales del proyecto y un
`static_assert(sizeof(Cmd) == 20)`. M3 solo pide `Say, Show, Hide, Bg, Wait, Jump, Label,
End`. El skill `vne-script-dsl` dice explicitamente que los tres `switch` del interprete
van **sin** `default` para que `-Wswitch` obligue a cubrir cada `CmdKind` que exista.

**Decisión.** `CmdKind` declara solo los 9 valores de M3 (`Nop` incluido como comodin). El
`static_assert(sizeof(Cmd) == 16)` refleja el tamano real de este subconjunto, no el 20
final. Cada hito que anada comandos (M5: `Choice, SetVar, AddVar, JumpIf, Call, Return,
LuaCall`; M6: `Sfx, Bgm, StopBgm`) suma sus valores y struct de union, y ajusta el
`static_assert` al nuevo tamano real.

**Alternativas descartadas.** Declarar los 22 `CmdKind` y los 14 structs de union desde
ya, dejando sin implementar los `switch` de los que no tocan a M3: rompe la regla de "un
hito a la vez" (obligaria a decidir semantica de `Choice`/`SetVar`/`LuaCall` ahora) y el
`-Wswitch` sin `default` dejaria de servir para nada (todo estaria "cubierto" con casos
vacios puestos por adelantado).

**Consecuencias.** Cada hito que anada comandos debe recordar tocar el `static_assert` de
tamano (fallara la compilacion si no coincide, lo cual es la señal correcta). El tamano de
`Cmd` en disco (`.vnc`) cambia entre hitos: no hay compatibilidad de formato entre
versiones de este ADR, aceptable porque los `.vnc` son artefactos de build, no assets
versionados.

---

## ADR-0022 — Identificador desconocido: solo etiquetas se validan de verdad en M3

**Fecha:** 2026-09-06
**Hito:** M3
**Estado:** aceptada

**Contexto.** SPEC.md §9.2 exige que "un identificador desconocido" sea error de
compilacion con archivo y linea. El guion referencia tres tipos de identificador: etiquetas
(`@jump destino`), nombres de actor/pose (`@show marta neutral`) y nombres de fondo (`@bg
mansion`). No existe todavia ningun registro de assets reales (no hay `assets_src/png/`
con sprites, SPEC.md §6 prohibe inventar contenido) contra el que validar actor/pose/fondo.

**Decisión.** El parser valida etiquetas estrictamente: recolecta todas las `::etiqueta`
declaradas en el propio guion y falla si un `@jump` referencia una que no existe ahi — es
autocontenido, no depende de ningun asset externo. Los nombres de actor, pose y fondo se
internan automaticamente la primera vez que aparecen (compilador, `NameInterner`), sin
validarlos contra nada externo.

**Alternativas descartadas.** Inventar una lista de actores/fondos "validos" solo para
poder rechazar los demas: violaria SPEC.md §6 (no inventar contenido de juego) con
contenido inventado peor todavia, solo para simular una validacion que no significa nada
sin assets reales.

**Consecuencias.** Un typo en un nombre de actor (`@show mrata neutral`) compila sin error
y crea un actor nuevo con ese nombre por accidente — no se detecta hasta que el juego se
ve mal. Cuando exista un registro real de actores/fondos (probablemente atado al pipeline
de assets de M9/M11), esta validacion debe extenderse a esos identificadores tambien.

---

## ADR-0023 — Say no bloquea esperando input en M3

**Fecha:** 2026-09-06
**Hito:** M3
**Estado:** aceptada

**Contexto.** `VmState.waiting_for_input` (SPEC.md §8.2) sugiere que `Say` deberia esperar
a que el jugador avance. M3 no tiene todavia una UI real (`VnMode` es M7) que lea input y
decida cuando avanzar un dialogo.

**Decisión.** En M3, `Say.update()` completa siempre al instante (pone y quita
`waiting_for_input` en el mismo paso). El campo sigue existiendo y se sigue tocando, para
que `VnMode` (M7) solo tenga que dejar de limpiarlo automaticamente y esperar una accion
real del jugador, sin cambiar la forma del campo.

**Alternativas descartadas.** Dejar `Say` bloqueado para siempre esperando input real:
imposible de probar en M3 (no hay VnMode todavia) y rompe el criterio de "un guion de 200
lineas se ejecuta completo" con `--autoplay-script`.

**Consecuencias.** El guion de demo se "juega" en el modo interactivo de `vne_game` sin
que el jugador pueda leer el dialogo (avanza solo): esperado y aceptable, es una
demostracion de la VM, no del juego terminado.

---

## ADR-0024 — DSL parseado linea a linea en vez de un grammar completo, hasta que existan bloques

**Fecha:** 2026-09-06
**Hito:** M3
**Estado:** aceptada

**Contexto.** SPEC.md §9.2 dice que la indentacion de 4 espacios "solo es significativa
dentro de `@if` y `@choice`". Ninguno de los dos existe hasta M5. Escribir ya un parser con
manejo de bloques indentados para comandos que no existen todavia adelanta trabajo de M5.

**Decisión.** `src/script/parser.cpp` procesa el guion linea a linea, sin ningun concepto
de bloque ni indentacion: cada linea logica (tras quitar comentarios) es una etiqueta, un
comando o una linea de dialogo independiente.

**Alternativas descartadas.** Escribir ya un parser recursivo-descendente con soporte de
bloques: la complejidad extra no tiene ningun caso de uso real hasta que `@if`/`@choice`
existan, y el diseño de bloques no esta decidido todavia (SPEC.md no especifica su AST).

**Consecuencias.** Cuando llegue M5, `parse_script` necesita reescritura real (no una
extension incremental) para soportar bloques indentados con `@if`/`@else`/`@end` y
`@choice`/`@end` anidables. Anotado aqui para que no sorprenda entonces.

---

## ADR-0025 — Empaquetador de atlas real con sprites CC0 de Kenney, sobre el placeholder de ADR-0011

**Fecha:** 2026-09-06
**Hito:** post-M3 (verificacion adicional pedida por el usuario)
**Estado:** aceptada

**Contexto.** ADR-0011 (M1) dejo el atlas de prueba como una rejilla procedural porque
`assets_src/png/` no tenia sprites reales, y advirtio explicitamente que habria que
sustituirlo por un empaquetador real "cuando existan sprites reales que empaquetar". El
usuario pidio buscar assets libres y probarlo de verdad en vez de dejarlo pendiente
indefinidamente.

**Decisión.** Se descargo el "UI Pack" de Kenney (kenney.nl/assets/ui-pack, licencia CC0,
`assets_src/png/LICENSE-kenney-ui-pack.txt`): 82 PNG sueltos de tamaños muy variados (16x16
hasta 192x64), justo lo que hace falta para probar un empaquetador de verdad en vez de una
rejilla uniforme. `tools/bake/main.cpp` ahora decodifica cada PNG con `stb_image` (SPEC.md
§3, "solo en herramientas offline"), los empaqueta con un shelf packer (mismo principio que
el de `glyph_cache.cpp` en M2: ordenar por alto descendente, colocar de izquierda a
derecha, saltar de estante cuando no cabe) en un atlas de 1024x1024, y escribe
`assets_baked/atlas_00.qoi` + un `atlas_00.bin` version 2 (manifiesto generico de
rectangulos `{x,y,w,h}`, sin nombres). Si `assets_src/png/` estuviera vacio, cae de vuelta
al placeholder procedural de ADR-0011, ahora escrito con el mismo formato version 2 para
que `main.cpp` tenga una unica ruta de lectura.

**Alternativas descartadas.** Mantener dos formatos de `atlas_00.bin` (rejilla v1 y
sprites v2) y dos rutas de lectura en `main.cpp`: mas codigo por mantener sin ningun
beneficio real, cuando el placeholder puede emitir el mismo formato que el empaquetador
real. Empaquetar los 870 archivos del pack completo: innecesario para probar la logica del
packer: 82 sprites de la variante "Blue/Default" ya cubren un rango de tamaños amplio.

**Consecuencias.** El criterio de M1 (5000 sprites de un atlas en 1 draw call, >300 fps) se
volvio a verificar con sprites reales en vez de la rejilla: sigue cumpliendose (ver cierre
en el resumen de la conversacion). El formato `atlas_00.bin` cambio de version (1 -> 2);
como es un artefacto de build, no un asset versionado, no hace falta migracion. Los
nombres de archivo originales de los sprites se pierden (el manifiesto solo guarda
rectangulos): si algun dia se necesita referenciar un sprite por nombre desde el DSL
(conectaria con ADR-0022), hay que anadir un pool de nombres al formato, como ya se hace en
`.vnc`.

---

## ADR-0026 — Miniatura de `.vnsave` diferida a M7

**Fecha:** 2026-09-06
**Hito:** M4
**Estado:** aceptada

**Contexto.** SPEC.md #8.3 define el formato `.vnsave` con un bloque de miniatura PNG
384x216 tras el `GameState`. La pila cerrada de dependencias (SPEC.md #3) no incluye
ningun codificador PNG (solo decodificacion via `stb_image`, y QOI para las texturas
horneadas); anadir uno nuevo solo para esto requeriria pararse a preguntar por regla
general del proyecto. Ademas, M4 no tiene todavia ninguna pantalla de guardado que
muestre esa miniatura: el unico consumidor real llega en M7.

**Decision.** Se pregunto al usuario explicitamente (parar-y-preguntar, formato en disco).
Eligio diferir la miniatura a M7. El formato `.vnsave` ya incluye el campo
`thumbnail_size` (u32) tal como lo define SPEC.md #8.3, compatible hacia adelante: M4
siempre escribe 0 y `load_game` salta ese bloque con `fseek` si algun archivo futuro trae
un tamano mayor que 0.

**Alternativas descartadas.** Codificar la miniatura en QOI (ya esta en la pila) en vez de
PNG: cambiaria el formato de disco frente a lo que dice SPEC.md #8.3 sin necesidad, y
seguiria sin haber nada que capture el framebuffer todavia. Anadir un encoder PNG nuevo a
la pila: es la dependencia nueva que la regla del proyecto prohibe anadir sin preguntar.

**Consecuencias.** El formato de archivo no cambiara cuando llegue la miniatura real en M7
(el campo ya existe); solo hay que rellenar `thumbnail_size` con el tamano real y escribir
los bytes despues. Ningun criterio de aceptacion de M4 depende de la miniatura.

---

## ADR-0027 — El rollback solo captura en `Say`; `Choice` se anadira en M5

**Fecha:** 2026-09-06
**Hito:** M4
**Estado:** aceptada

**Contexto.** El skill `vne-serializable-state` especifica que la instantanea de rollback
se captura "justo antes de ejecutar un comando `Say` y un comando `Choice`". El subconjunto
de `CmdKind` de M3 (ADR-0021) no incluye `Choice` todavia: ese comando llega con la
ramificacion en M5.

**Decision.** `cmd_start` solo captura en `CmdKind::Say` por ahora. Cuando M5 añada
`Choice` al enum, se añadira la misma llamada a `rollback_capture` en su caso del switch;
como los switches del interprete no llevan `default` (ADR de M3), el compilador ya fuerza a
tocar ese caso en cuanto exista.

**Alternativas descartadas.** Ninguna: no hay otra opcion razonable mientras `Choice` no
exista como comando.

**Consecuencias.** El buffer de 64 instantaneas de rollback de M4 es funcionalmente
completo para el subconjunto de comandos actual. Queda anotado en "Pendientes observados"
para no olvidar el caso `Choice` al llegar a M5.

---

## ADR-0028 — Relleno de struct explicito en `VmState`, `GameState` y `BacklogEntry`

**Fecha:** 2026-09-06
**Hito:** M4
**Estado:** aceptada

**Contexto.** El test obligatorio de M4 (skill `vne-serializable-state`: ejecutar
`demo.vns` guardando y recargando en cada comando, comparar el estado final byte a byte
con una ejecucion sin interrupciones) fallaba de forma intermitente con `memcmp` en
`GameState` y en `Backlog`, incluso cuando el contenido logico de ambos era identico. El
diagnostico (comparando byte a byte donde diferian) mostro que las diferencias caian
siempre exactamente en el relleno de alineacion implicito que el compilador inserta entre
miembros de tamaño distinto (por ejemplo entre `cmd_phase` (u8) y `cmd_timer` (f32) en
`VmState`, o entre `speaker_id` (u16) y `text_id` (u32) en `BacklogEntry`). La
especificacion del proyecto ya avisaba de esto: el skill `vne-serializable-state` prohibe
"padding sin inicializar" y pide relleno explicito con `memset` a cero. La causa raiz es
que la inicializacion por valor (`GameState{}`) esta obligada por el estandar a poner a
cero la representacion de objeto completa la primera vez, pero MSVC no garantiza que ese
cero sobreviva a escrituras parciales posteriores sobre miembros vecinos: el relleno
*implicito* no es un sub-objeto real, asi que ninguna copia ni ninguna asignacion
posterior esta obligada a preservarlo.

**Decision.** Se convirtio cada hueco de alineacion implicito en un campo real y con
nombre: `u8 _pad0[2]`, `u8 _pad1[3]`, etc., en `VmState`, `GameState` y `BacklogEntry`,
cada uno con su propio inicializador `= {}`. Al ser un miembro real del struct (no un
hueco invisible para el lenguaje), su valor por defecto se preserva de la misma forma
fiable que cualquier otro campo a traves de copias, asignaciones y el volcado crudo a
disco de `save_game`. Tambien se corrigio `backlog_push`, que construia un `BacklogEntry`
temporal por lista de agregados (`BacklogEntry{a, b, c}`) y lo asignaba: la temporal en si
llevaba relleno indeterminado de la pila del llamador. Ahora se declara `BacklogEntry
entry{};` (inicializacion por valor) y se rellenan los campos uno a uno antes de asignarla.

**Alternativas descartadas.** Comparar `GameState`/`Backlog` campo a campo en vez de con
`memcmp`: evita el sintoma en el test, pero no arregla el problema de fondo, que es que el
propio `save_game` vuelca esos mismos bytes de relleno indeterminado a disco — dos
partidas guardadas con el mismo contenido logico podrian producir archivos `.vnsave`
distintos, lo cual es peor que un test fragil. Usar `#pragma pack(1)`: elimina el relleno
pero fuerza acceso desalineado a los `u32`/`f32` del struct, mas lento y no portable a
todas las plataformas de la lista de prioridad del proyecto.

**Consecuencias.** El test obligatorio de M4 (`tests/test_save_replay.cpp`) pasa de forma
estable en ejecuciones repetidas (verificado 4 veces seguidas). Cualquier campo nuevo que
se añada a estos tres structs en hitos futuros debe revisarse por huecos de alineacion
implicitos antes de darlo por terminado; no hay una comprobacion automatica de esto todavia
(podria añadirse un `static_assert` de tamaño total documentado como ya existe para
`Cmd`).

---

## Pendientes observados

Anota aquí cosas detectadas fuera del alcance del hito actual, para no perderlas ni
desviarte.

- ~~Instalar el componente "C++ AddressSanitizer" del VS Installer~~ — resuelto: instalado
  el 2026-09-06 con permisos de administrador. Ver ADR-0007.
- UBSan no tiene equivalente en MSVC/Windows. Solo se puede verificar compilando en Linux o
  macOS con GCC/Clang.
- No se compiló ni verificó en Linux ni en macOS por no haber esas plataformas disponibles en
  este entorno. Sigue pendiente para todos los hitos hasta ahora (M0, M1, M2) — decisión
  consciente del usuario (ADR-0013), no un olvido.
- Backend Metal de sokol_gfx sin implementar (ADR-0009): hace falta una Mac para escribirlo y
  probarlo. Bloquea el cierre real de M1/M2 en macOS.
- Backend GL 3.3 de M1 escrito pero no compilado ni probado: no hay Linux disponible aquí.
- Shaders escritos a mano en vez de vía `sokol-shdc` (ADR-0010): reconsiderar automatizar el
  binario cuando el número de shaders crezca.
- ~~El atlas de M1 es una rejilla procedural fija, no el empaquetador real~~ — resuelto:
  ADR-0025 implementa un shelf packer real sobre 82 sprites CC0 reales (Kenney UI Pack) en
  `assets_src/png/`, verificado visualmente. Sigue sin ser el `atlas.bin` final de
  SPEC.md §11 (sin nombres, sin sub-paginas, sin rotacion): eso queda para cuando el
  pipeline de assets se formalice (probablemente M9/M11).
- El componente "C++ AddressSanitizer" no cubre el componente separado "Graphics Tools" de
  Windows: la capa de depuracion D3D11 sigue sin poder probarse aqui (ADR-0012). No bloquea
  ningun criterio de aceptacion, solo reduce la validacion extra disponible en Debug.
- `glyph_cache_init()`/`glyph_cache_shutdown()` existen pero no los llama nadie (ni
  `gfx_init`/`gfx_shutdown` ni `main.cpp`): funciona por casualidad porque los arrays
  globales de `glyph_cache.cpp` se ponen a cero solos al arrancar el proceso. Cablearlos
  correctamente al ciclo de vida de `gfx_init`/`gfx_shutdown` antes de que un hito futuro
  necesite reiniciar el atlas de glifos (p. ej. al cambiar de idioma en M10).
- `{b}` se parsea correctamente (marca `bold` en el `Segment`) pero no tiene ningun efecto
  visual: no hay una variante bold cargada ni negrita sintetica. Falta decidir si M7 (UI de
  VN) carga una segunda `FontHandle` para negrita o si se sintetiza.
- `{w=n}` y `{speed=n}` se reconocen y se descartan sin efecto: la temporizacion real del
  efecto de maquina de escribir la conduce la VM (M7), que todavia no existe.
- `GlyphQuad` en `text/layout.h` extiende el struct ilustrativo de SPEC.md §7.2 con
  `atlas_page`, `color` e `is_ruby` — necesarios para que el atlas multi-pagina y el
  marcado `{color=}`/furigana funcionen de verdad (ver comentario en `layout.h`). Si esto
  choca con algo mas adelante, es la primera pista a revisar.
- La fuente de prueba `NotoSansJP.ttf` (~9.5 MB, ADR-0017) infla el repositorio; considerar
  Git LFS o un subconjunto de glifos si llega a molestar.
- `parse_script` es linea a linea (ADR-0024): necesita reescritura real cuando lleguen
  `@if`/`@choice` en M5 (bloques indentados).
- Validacion de identificadores desconocidos limitada a etiquetas (ADR-0022): actores,
  poses y fondos se internan sin validar. Ampliar cuando exista un registro real de
  assets.
- `Say` no bloquea esperando input (ADR-0023): revisar en cuanto exista `VnMode` (M7).
- Al usar `CHECK()`/`REQUIRE()` de doctest sobre un `std::string`/`std::string_view`, el
  STL de MSVC dispara C4530 (excepcion usada sin `/EHsc`) dentro de su propio
  `basic_ostream::operator<<`; se silencio con `/wd4530` solo en `vne_tests` (mismo patron
  que C5285 de doctest+`std::tuple`, ver tests/CMakeLists.txt). Si aparece en un contexto
  nuevo, es el mismo problema, no uno distinto.
- Rollback: falta capturar tambien en `CmdKind::Choice` cuando M5 lo añada (ADR-0027). El
  switch sin `default` de `cmd_start` obligara a tocar ese caso, pero queda anotado aqui
  para no olvidar añadir la llamada a `rollback_capture` justo ahi.
- Las teclas F5 (guardar)/F9 (cargar)/flechas (rollback) cableadas en `main.cpp` para M4 se
  verificaron por tests automatizados (round-trip byte a byte, deshacer/rehacer, limite de
  64 pasos) pero no se probaron pulsando las teclas de verdad en la ventana interactiva en
  este entorno: no hay forma de inyectar pulsaciones de teclado real contra una ventana
  SDL desde aqui. Si algo en el cableado de `input.key_pressed[...]` especifico de M4
  estuviera mal (a diferencia de la logica que envuelve, que si esta probada), no se
  detectaria hasta una prueba manual real.
