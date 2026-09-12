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

## ADR-0029 — Nombres de variable/flag se resuelven por hash modulo capacidad, sin tabla de interning

**Fecha:** 2026-09-06
**Hito:** M5
**Estado:** aceptada

**Contexto.** El DSL referencia variables por nombre (`@set confianza = 0`, `@if
confianza >= 3`) y Lua tambien (`vn.get_var("confianza")`, SPEC.md #9.4). El compilador
del DSL interna nombres de actor/pose/fondo con una tabla secuencial (`NameInterner`),
pero esa tabla vive solo en tiempo de compilacion y no se serializa al `.vnc`. Lua, en
cambio, recibe el nombre como texto en tiempo de ejecucion (dentro de un fragmento
`@lua` sin parsear, ver ADR-0024): no hay forma de que consulte una tabla de interning
que el compilador ya descarto, salvo que esa tabla tambien se serialice — una seccion
mas en el `.vnc` solo para esto.

**Decision.** El `var_id`/`flag_id` de un nombre es `fnv1a_u32(nombre) % capacidad`
(`k_max_vars` = 512, `k_max_flags` = 2048), calculado igual en `script/compiler.cpp`
(para `@set`/`@add`/`@if`/opciones de `@choice`) y en `script/lua_bindings.cpp` (para
`vn.get_var`/`vn.set_var`/`vn.get_flag`/`vn.set_flag`). El mismo nombre cae siempre en el
mismo indice sin necesitar ninguna tabla adicional ni en el `.vnc` ni en tiempo de
ejecucion.

**Alternativas descartadas.** Una tabla `{name_hash, var_id}` serializada al `.vnc` (como
la de `ChoiceOption`, ver ADR-0030): resuelve el problema sin riesgo de colision, pero
Lua seguiria sin poder registrar nombres nuevos que el DSL nunca uso (un `@lua` que
solo usa `vn.set_var` con un nombre que ningun `@set` toco en el guion no tendria
entrada en esa tabla). Pedir que todo nombre de variable se declare primero en el DSL:
anadiria una sintaxis nueva sin necesidad real todavia.

**Consecuencias.** Riesgo real pero pequeño de colision de hash entre dos nombres
distintos (dos variables distintas cayendo en el mismo indice y pisandose). Para el
tamaño de guion de este proyecto (cientos de nombres, no millones) la probabilidad es
baja, y el sintoma seria detectable (una variable con un valor inesperado). Si esto
molesta en un hito futuro con guiones grandes, la solucion es la tabla serializada
descartada arriba, no cambiar el esquema de hash.

---

## ADR-0030 — Tabla de `ChoiceOption` como extension del formato `.vnc` (version 2)

**Fecha:** 2026-09-06
**Hito:** M5
**Estado:** aceptada

**Contexto.** `Cmd::choice` (SPEC.md #8.1) solo reserva `first_option`/`option_count`: la
cardinalidad de las opciones de un `@choice` es variable, así que no caben en el `Cmd` de
tamaño fijo. El formato `.vnc` documentado en SPEC.md #9.3 solo lista
`Cmd[]`/`string_pool`/`Label[]`, sin ninguna tabla de opciones. Sin datos en algún sitio
del archivo, el runtime no puede saber a qué `pc` salta cada opción ni si tiene una
condición.

**Decision.** Se añadió una sección `ChoiceOption[]` al final del `.vnc`, con su propio
contador `choice_option_count` en la cabecera (que pasó de 5 a 6 campos `u32`). Como es
un cambio de formato binario, se subió `k_vnc_version` de 1 a 2 — sin migración, porque
`.vnc` es un artefacto de build (`assets_baked/`, en `.gitignore`), no un asset
versionado que alguien pueda tener guardado con el formato viejo (mismo precedente que
`atlas_00.bin` en ADR-0025). `Cmd::choice.first_option`/`option_count` indexan un tramo
contiguo de esta tabla, igual que `Label[]` ya indexaba por nombre.

**Alternativas descartadas.** Codificar las opciones como `Cmd` normales intercalados en
el array principal (p. ej. un `CmdKind::ChoiceOption` por opción, entre `Choice` y
`ChoiceEnd`): evitaría la sección nueva, pero el intérprete tendría que reconocerlos y
saltárselos en vez de ejecutarlos como comandos normales — más complejidad en el `switch`
sin `default` por una distinción que no es realmente "un comando más".

**Consecuencias.** `vm/vm.h` amplía `CompiledScript` con `choice_options`/
`choice_option_count`, y de paso también expone `labels`/`label_count` (que SPEC.md #9.3
ya documentaba pero el runtime ignoraba desde M3 — ver ADR-0031 para por qué M5 sí los
necesita). `script/script_load.cpp` valida el tamaño total esperado del archivo
incluyendo la sección nueva antes de aceptarlo.

---

## ADR-0031 — `vn.jump()` reactiva la tabla de etiquetas del `.vnc` en tiempo de ejecucion

**Fecha:** 2026-09-06
**Hito:** M5
**Estado:** aceptada

**Contexto.** Desde M3, `script_load.cpp` lee `Label[label_count]` del `.vnc` pero lo
descarta (`(void)label_count`) porque el runtime nunca necesitaba resolver un nombre a
`pc`: todos los saltos del DSL (`@jump`, `@call`) ya se resuelven a `pc` directo en
tiempo de compilación. SPEC.md #9.4 expone `vn.jump(label)` a Lua, y Lua sólo tiene el
nombre como texto en tiempo de ejecución — no hay forma de evitar una búsqueda en
runtime para este caso concreto.

**Decision.** `CompiledScript` (vm/vm.h) ahora expone `labels`/`label_count` apuntando
directo al bloque `Label[]` ya presente en el `.vnc` (SPEC.md #9.3 ya lo documentaba, no
es un campo nuevo del formato). `vm_find_label()` hace una búsqueda lineal por
`name_hash`: el número de etiquetas de un guion es pequeño (decenas, no miles), así que
no hace falta una tabla hash real.

**Alternativas descartadas.** Ninguna seria: los datos ya estaban en el archivo desde
M3, sólo hacía falta dejar de ignorarlos.

**Consecuencias.** Ninguna negativa: es estrictamente wiring de algo que SPEC ya
preveía. `vn.jump()` a una etiqueta desconocida se registra con `log_error` y no mueve el
`pc` (no hay excepciones que lanzar, SPEC.md #4).

---

## ADR-0032 — `heap_guard` se suspende durante la ejecución de un `LuaCall`

**Fecha:** 2026-09-06
**Hito:** M5
**Estado:** aceptada — decisión del usuario, no tomada unilateralmente

**Contexto.** `CmdKind::LuaCall` ejecuta un fragmento de código Lua (vía sol2) dentro de
`cmd_start`, que corre desde `vm_update`/`vm_skip_current` — el bucle de frame normal.
Compilar y correr un fragmento de Lua asigna heap por cómo funciona cualquier intérprete
de Lua (parsing a bytecode, tablas, closures): no hay forma de evitarlo sin renunciar a
Lua como lenguaje de lógica, que es una decisión ya cerrada en SPEC.md #3/#9.4. Esto
entra en conflicto directo con la regla no negociable #4 de `CLAUDE.md` ("cero
asignaciones de heap en el bucle de frame"), que no preveía ninguna excepción. Se
paró y se preguntó al usuario en vez de decidir por iniciativa propia (regla #3 de
`CLAUDE.md`).

**Decision.** El usuario eligió una excepción documentada y acotada: `heap_guard_suspend()`/
`heap_guard_resume()` (base/heap_guard.h) rodean exactamente la llamada a sol2 dentro de
`script/lua_bindings.cpp` (`lua_init()` y `lua_run()`), y en ningún otro sitio. Mientras
está suspendido, `operator new` sigue funcionando pero no incrementa
`g_frame_alloc_count`, así que `heap_guard_check_frame()` no dispara al final del frame
por asignaciones que ocurrieron sólo ahí dentro.

**Alternativas descartadas.** Precompilar todo el Lua del guion a bytecode al cargar el
`.vnc` (fuera del bucle de frame): reduce pero no elimina la asignación (sol2/Lua pueden
seguir asignando tablas y closures al *ejecutar*, no sólo al parsear), y no es una
garantía completa — se descartó por dar una falsa sensación de estar resuelto. Restringir
`@lua` a que sólo corra fuera de frames con render activo (p. ej. sólo en
`--autoplay-script` o transiciones): cambiaría el alcance funcional de `@lua` respecto a
como SPEC.md #9.1 lo muestra (intercalable con diálogo normal), sin necesidad.

**Consecuencias.** `heap_guard` deja de ser una garantía absoluta de cero heap en el
frame: es cero heap salvo la única excepción documentada y acotada aquí. Cualquier
auditoría futura de asignaciones de heap por frame debe saber que un `@lua` en el guion
es la única fuente legítima. Las llamadas a suspend/resume deben ir siempre en pareja y
nunca envolver más que la llamada a sol2 en sí — si algún día se filtran a un ámbito más
amplio, dejan de detectar bugs reales de asignación en el resto del motor.

---

## ADR-0033 — Un único `sol::state` persistente, creado en `lua_init()` fuera del bucle de frame

**Fecha:** 2026-09-06
**Hito:** M5
**Estado:** aceptada

**Contexto.** Con la excepción de heap_guard ya aceptada (ADR-0032), quedaba decidir si
crear un `sol::state` nuevo en cada `LuaCall` o mantener uno persistente. SPEC.md #9.4
exige que el estado de Lua no se serialice: cualquier dato que deba sobrevivir a un
guardado tiene que pasar por `vn.set_var`/`vn.set_flag` hacia `GameState`.

**Decision.** Un único `sol::state` global (`g_lua`), creado una vez en `lua_init()`
(llamado junto al resto de la inicialización en `main()`/`test_main.cpp`, fuera del
bucle de frame) y reutilizado en cada `lua_run()`. La tabla `vn` se liga una sola vez.
Reutilizar el intérprete no compromete la restricción de SPEC.md #9.4: la restricción es
sobre qué sobrevive a un *guardado* (sólo `GameState`), no sobre si el intérprete en sí
persiste entre llamadas dentro de la misma sesión de juego.

**Alternativas descartadas.** Crear y destruir un `sol::state` en cada `lua_run()`: más
simple de razonar (garantiza cero estado colgante entre llamadas por construcción), pero
mucho más lento (reabrir librerías y volver a ligar `vn.*` en cada `@lua`), y ninguna
regla de SPEC.md lo exige.

**Consecuencias.** Si un script Lua crea una variable global de Lua (no vía `vn.set_var`)
esa variable sobrevive entre llamadas a `@lua` dentro de la misma sesión pero se pierde
al reiniciar el proceso — y nunca se guarda. Es responsabilidad de quien escriba guiones
Lua usar `vn.set_var`/`vn.set_flag` para cualquier dato que deba persistir; no hay
enforcement automático de esto todavía.

---

## ADR-0034 — `Bgm.track_id` se resuelve contra un catalogo escaneado, no via el string_pool del guion

**Fecha:** 2026-09-07
**Hito:** M6
**Estado:** aceptada

**Contexto.** `Sfx.sound_id` se resolvió por el mismo camino que `Say.text_id`: un
`text_id` (u32) apuntando al `string_pool` del guion compilado, con la ruta completa del
archivo (`assets_src/ogg/nombre.wav`). Eso funciona porque el estado de un efecto de
sonido no sobrevive a un guardado. `Bgm.track_id`, en cambio, **sí** tiene que
sobrevivir: `GameState.bgm_track_id` (SPEC.md #8.2) es un `u16` fijo por la especificación
del estado serializable, y el criterio de M6 exige que cargar una partida restaure la
pista de música — pero un `text_id` solo es válido dentro del `string_pool` del guion
concreto que lo compiló, y una partida guardada se puede cargar sin que ese guion (o
siquiera ese `.vnc`) esté presente todavía.

**Decision.** `Cmd::bgm.track_id` y `GameState.bgm_track_id` son
`fnv1a_u32(nombre_logico) % 65536`, el mismo esquema de hash-sin-tabla de ADR-0029 pero
aplicado a un catálogo real: `audio_init()` escanea `assets_src/ogg/` (copiado al
directorio de build igual que las fuentes y los PNG) y arma `{hash del nombre de archivo
sin extensión -> ruta}`. `audio_load_track()` resuelve el id contra ese catálogo tanto
al ejecutar un `Bgm` normal como al restaurar tras cargar (`vm_resync_after_state_change`).

**Alternativas descartadas.** Guardar el `text_id` del guion original en `GameState` de
todas formas: rompe en cuanto la partida se carga con la aplicación reiniciada (el
`string_pool` del guion vive en una arena que se resetea, ADR-0004) o con un guion
distinto activo. Ampliar `GameState.bgm_track_id` a un `text_id`-like más grande: viola
SPEC.md #8.2, que fija el campo en `u16`.

**Consecuencias.** Mismo riesgo de colisión de hash que ADR-0029 (dos nombres de pista
distintos cayendo en el mismo id), mitigado por ser un catálogo pequeño (assets de
música, no cientos de variables). El catálogo se reconstruye escaneando el disco en cada
`audio_init()`: si `assets_src/ogg/` cambia entre partidas guardadas (se borra o renombra
un archivo), una partida vieja con ese `track_id` simplemente no encuentra música al
cargar (se degrada a silencio, `audio_load_track` devuelve `false` y se registra con
`log_error`, nunca crashea).

---

## ADR-0035 — `heap_guard_suspend`/`resume` tambien exceptua la primera carga de un sonido nuevo

**Fecha:** 2026-09-07
**Hito:** M6
**Estado:** aceptada — extiende el precedente de ADR-0032, no una decision nueva desde cero

**Contexto.** Igual que ejecutar Lua (ADR-0032), decodificar un archivo de audio nuevo
(`ma_sound_init_from_file`) asigna heap por como funciona miniaudio, y `CmdKind::Sfx`/
`Bgm` pueden ejecutarse dentro del bucle de frame la primera vez que el guion los alcanza.

**Decision.** Se aplicó el mismo patrón ya aceptado por el usuario en ADR-0032:
`heap_guard_suspend()`/`heap_guard_resume()` rodean exactamente la llamada a
`ma_sound_init_from_file` dentro de `audio_load()`, y en ningún otro punto de
`audio/audio.cpp`. Cargas repetidas del mismo `path` usan una cache interna
(`{hash de la ruta -> SoundHandle}`) y no vuelven a pasar por ahí, así que el coste real
solo ocurre una vez por sonido distinto, no en cada `@sfx`/`@bgm`.

**Alternativas descartadas.** Las mismas que en ADR-0032 y por las mismas razones:
precargar todo el audio del guion al cargar el `.vnc` reduciría pero no eliminaría el
problema (miniaudio puede seguir asignando durante la reproducción, no solo al decodificar),
y restringir `@sfx`/`@bgm` a fuera del bucle de frame cambiaría su alcance funcional sin
necesidad.

**Consecuencias.** No se pidió confirmación explícita al usuario esta vez porque ya es
literalmente el mismo mecanismo aprobado en ADR-0032, aplicado al mismo tipo de problema
(un tercero que no puede evitar asignar heap la primera vez que se usa un recurso nuevo);
extenderlo aquí es la aplicación directa de esa decisión, no una nueva. Si aparece un
tercer caso que no encaje en este patrón (p. ej. algo que asigne heap en *cada* llamada,
no solo la primera vez que se ve un recurso), eso sí necesitaría pararse a preguntar de
nuevo.

---

## ADR-0036 — Bug real en miniaudio 0.11.21: cargar un archivo inexistente crashea, se evita comprobando antes

**Fecha:** 2026-09-07
**Hito:** M6
**Estado:** aceptada

**Contexto.** `tests/test_audio.cpp` (bajo ASan) detectó un `heap-use-after-free` real
dentro de `ma_resource_manager_data_buffer_node_acquire` (miniaudio.h:68596) al llamar a
`ma_sound_init_from_file` sobre una ruta que no existe: el gestor de recursos de
miniaudio libera un nodo y lo vuelve a leer en su propia ruta de manejo de error. No es
un bug de este proyecto (confirmado con el stack trace completo de ASan, enteramente
dentro de `miniaudio.h`), pero sí hay que evitarlo.

**Decision.** `audio_load()` comprueba con `fopen`/`fclose` que el archivo existe *antes*
de llamar a `ma_sound_init_from_file`, y devuelve `AudioLoadResult::NotFound`
inmediatamente si no. Esto evita por completo la ruta de fallo de miniaudio donde vive el
bug, en vez de intentar recuperarse después de que ya ocurrió.

**Alternativas descartadas.** Envolver la llamada en algún mecanismo de recuperación
después del hecho: no existe tal mecanismo fiable para un use-after-free ya disparado
(el daño de memoria ya ocurrió antes de que `ma_sound_init_from_file` devuelva el código
de error). Fijar una versión distinta de miniaudio: no investigado a fondo (podría no
tener el bug), pero la comprobación previa es más simple, más barata, y de todas formas
es una buena práctica independiente del bug (evita gastar un slot del pool en una carga
que se sabe de antemano que va a fallar).

**Consecuencias.** Si en un hito futuro se actualiza la versión de miniaudio, vale la
pena probar si el bug sigue presente (podría eliminarse la comprobación extra si ya no
hace falta) — pero quitar la comprobación no aporta nada mientras tanto, así que no hay
prisa. Anotado también en "Pendientes observados".

---

## ADR-0037 — Miniatura del `.vnsave` en QOI, no PNG

**Fecha:** 2026-09-07
**Hito:** M7
**Estado:** aceptada — decisión del usuario, no tomada unilateralmente

**Contexto.** SPEC.md #8.3 dice literalmente "miniatura PNG 384x216", pero la lista
cerrada de dependencias (SPEC.md #3) solo tiene `stb_image` para decodificar imágenes, y
explícitamente "solo en herramientas offline, nunca en runtime" — no hay ningún
codificador PNG disponible para volcar el framebuffer a disco cuando el jugador guarda
la partida en tiempo real. Esto ya se había diferido una vez (ADR-0026, M4); M7 lo
necesita de verdad porque el criterio de aceptación es "pantalla de guardado con
miniaturas". Se paró y se preguntó al usuario en vez de decidir por iniciativa propia
(regla #3 de `CLAUDE.md`, cambio de formato de disco).

**Decision.** La miniatura se codifica en QOI en vez de PNG. QOI ya está en la pila
cerrada desde ADR-0008 (usado para las texturas horneadas) y es trivial de codificar en
tiempo de ejecución (formato mucho más simple que PNG, sin necesitar zlib ni ninguna
dependencia nueva). El campo `thumbnail_size`/bloque de miniatura de `.vnsave` (ya
presente desde M4) ahora contiene bytes QOI reales en vez de estar siempre a 0.

**Alternativas descartadas.** El usuario también consideró "downsample crudo sin
comprimir" (guardar los 384x216 píxeles RGB8 tal cual): descartada por inflar cada
`.vnsave` a ~250 KB por miniatura sin necesidad, cuando QOI ya resuelve eso con una
dependencia que el proyecto ya tiene. "Miniatura sin implementar todavía, otra vez":
descartada porque dejaría el criterio de M7 sin cumplir una tercera vez.

**Consecuencias.** `vm/save.h` gana `k_thumbnail_width/height` (384x216) y
`load_save_thumbnail()` para leer solo la miniatura sin decodificar el `GameState`
completo (para listar slots en `SaveLoadMode` sin pagar el coste de cada carga
completa). La captura del framebuffer en sí (`gfx_capture_thumbnail`) solo está
implementada en el backend D3D11 por ahora (ver ADR-0038): en GL, `save_game` sigue
funcionando pero sin miniatura.

---

## ADR-0038 — Lectura de vuelta del render target (`gfx_backend_capture_thumbnail`) solo en D3D11

**Fecha:** 2026-09-07
**Hito:** M7
**Estado:** aceptada — mismo hueco que el resto del backend GL (ADR-0009)

**Contexto.** La versión de sokol_gfx pineada por el proyecto (previa al refactor de
"sg_view", ADR de M1) no tiene una API de lectura de textura portable entre backends.
Capturar el framebuffer para la miniatura de guardado (M7) necesita, por tanto, un
mecanismo especifico de cada backend gráfico.

**Decision.** `gfx_backend_capture_thumbnail()` (declarada en `gfx/gfx_backend.h`) se
implementa de verdad solo en D3D11: usa `sg_d3d11_query_image_info()` (función de
interop que sokol_gfx sí expone) para obtener el `ID3D11Texture2D*` del render target de
escena, lo copia a una textura de staging (`D3D11_USAGE_STAGING` +
`D3D11_CPU_ACCESS_READ`) con `CopyResource`, y lee los píxeles con `Map` — con
downsampling por vecino más cercano directo durante la lectura, sin materializar nunca
un buffer intermedio a resolución completa (1920x1080 RGBA8 serían ~8.3 MB, mayor que
`g_arena_frame`). El backend GL devuelve `false` sin implementar nada (mismo patrón que
el resto de ese backend desde M1, ADR-0009: sin Linux disponible aquí para escribirlo y
probarlo con `glReadPixels`).

**Alternativas descartadas.** Materializar un buffer RGBA8 a resolución completa en la
arena de escena antes de reducirlo: más simple de escribir pero desperdicia ~8 MB de una
arena que además se resetea al cambiar de capítulo/mapa, por una operación que en
realidad no necesita nunca los píxeles completos en memoria a la vez.

**Consecuencias.** Guardar una partida en el backend GL (cuando exista, ADR-0009) no
tendrá miniatura real hasta que alguien escriba y pruebe la contraparte GL de esta
función en una máquina Linux de verdad. `SaveLoadMode` ya maneja ese caso con
normalidad (guarda igual, solo que sin imagen).

---

## ADR-0039 — `Say` bloquea de verdad esperando input (cierra ADR-0023)

**Fecha:** 2026-09-07
**Hito:** M7
**Estado:** aceptada

**Contexto.** ADR-0023 (M3) simplificó `Say` para que se completara al instante,
anotando explícitamente "revisar en cuanto exista VnMode (M7)". Ese momento llegó.

**Decision.** `cmd_update` para `CmdKind::Say` ahora es
`return state->vm.waiting_for_input == 0;` en vez de limpiar la bandera él mismo: el
comando se queda parado hasta que algo externo llama a la nueva `vm_confirm_say()`
(`vm/vm.h`), que `VnMode::update()` invoca cuando el jugador confirma (tecla/clic) o
cuando el modo automático completa su temporizador. `vm_skip_current` no cambia: su
`cmd_skip_to_end` para `Say` ya limpiaba la bandera incondicionalmente desde M3, así que
el modo skip y `--autoplay-script` siguen funcionando igual que antes sin ningún cambio.

**Alternativas descartadas.** Ninguna: es exactamente el mecanismo que ADR-0023 ya
había anticipado, mismo patrón que `Choice`/`vm_select_choice` (M5).

**Consecuencias.** Cualquier código que llame a `vm_update` directamente sobre un guion
con líneas de diálogo (no vía `vm_skip_current`) ahora se queda parado en el primer
`Say` hasta que alguien llame a `vm_confirm_say` — confirmado en el smoke test
interactivo de M7 (`vm_pc` se queda fijo en el primer `Say` en vez de avanzar solo).
Ningún test existente se vio afectado porque M3-M6 solo ejercitaban `Say` vía
`vm_skip_current`, nunca vía `vm_update` directo en un test.

---

## ADR-0040 — Sin soporte de ratón todavía: toda la UI de M7 es solo teclado

**Fecha:** 2026-09-07
**Hito:** M7
**Estado:** aceptada

**Contexto.** `platform/input.h` (desde M0) solo rastrea teclado (`key_down`/
`key_pressed` por `SDL_Scancode`); no hay posición ni botones de ratón. SPEC.md #10 no
especifica el mecanismo de interacción de cada modo, así que la elección de cómo
interactuar con el cuadro de diálogo/backlog/menú/pantalla de guardado quedaba abierta.

**Decision.** Toda la UI de M7 (confirmar diálogo, navegar backlog, ajustar volúmenes
del menú, elegir slot de guardado) se maneja solo con teclado: SPACE/ENTER confirma,
flechas navegan, ESC cierra un overlay. Ninguna de las nuevas pantallas necesita
hit-testing de rectángulos contra una posición de ratón.

**Alternativas descartadas.** Añadir rastreo de posición/clic de ratón a
`platform/input.cpp` (eventos `SDL_EVENT_MOUSE_*` de SDL3) para permitir clic-para-
avanzar y clic en botones: se descartó por alcance — ampliaría el módulo de input de
plataforma (fuera del núcleo de "UI de novela visual" que pide M7) y el teclado ya cubre
cada interacción sin ambigüedad. SPEC.md no exige ratón explícitamente en ningún
criterio de M7.

**Consecuencias.** Un jugador esperaría poder hacer clic para avanzar diálogo (convención
estándar del género); no puede todavía. Añadir soporte de ratón real (posición +
botones en `InputState`, más hit-testing de rectángulos de UI) queda como trabajo futuro
explícito — anotado en "Pendientes observados". El proyecto no necesita rediseñar nada
para añadirlo despues: `InputState` es un struct plano, ampliarlo es aditivo.

---

## ADR-0041 — `src/editor/` se excluye de Ship a nivel de CMake, no con `#ifdef` vacío

**Fecha:** 2026-09-07
**Hito:** M8
**Estado:** aceptada

**Contexto.** SPEC.md #6 dice literalmente "en Ship, todo el código de `src/editor/`
queda excluido del build", y el criterio de aceptación de M8 exige que la build Ship "no
contenga símbolos de ImGui". El patrón ya usado para el backend GL (`gfx_backend_gl.cpp`,
ADR-0009) es envolver el cuerpo entero de un archivo en `#if defined(...)` y dejar que
compile "vacío" cuando la macro no está definida — eso basta para "cero uso", pero no
garantiza "cero símbolos": el `.cpp` se compilaría igual (a un objeto casi vacío) y en
teoría un símbolo residual podría colarse.

**Decision.** Ni Dear ImGui ni `src/editor/editor.cpp` se añaden al build en absoluto
cuando `CMAKE_BUILD_TYPE STREQUAL "Ship"` (comprobación en tiempo de configuración,
fiable aquí porque cada preset tiene su propio directorio de build fijo, ver
CMakePresets.json). `editor.h` sigue declarando sus funciones incondicionalmente (para
que `main.cpp` compile en cualquier configuración), pero cada llamada real en `main.cpp`
está envuelta en `#if defined(VN_EDITOR)`: en Ship ese código ni siquiera se parsea, y el
enlazador nunca ve una referencia a un símbolo que no existe. Verificado con
`strings vne_game.exe | grep -i imgui` sobre el binario Ship real: 0 coincidencias.

**Alternativas descartadas.** `#if defined(VN_EDITOR)` envolviendo todo `editor.cpp`
(mismo patrón que `gfx_backend_gl.cpp`) sin tocar CMake: más simple de escribir, pero no
garantiza "cero símbolos" de la misma forma — dependería de que el enlazador elimine
agresivamente un objeto casi vacío, en vez de que ni siquiera exista.

**Consecuencias.** `editor.cpp` solo se compila (y por tanto solo se type-checkea) en
Debug y Dev, nunca en Ship — igual que ya pasaba con `gfx_backend_gl.cpp` en Windows. Un
error de tipos en el editor no se detectaría corriendo solo builds Ship; hay que
recordar probar Dev cuando se toque `src/editor/`.

---

## ADR-0042 — Recarga de scripts invoca `vne_bake` como subproceso, no enlaza el compilador del DSL en el juego

**Fecha:** 2026-09-07
**Hito:** M8
**Estado:** aceptada

**Contexto.** El criterio de M8 "editar un `.vns` y ver el cambio sin reiniciar" exige
que el editor pueda recompilar un guion en caliente. El compilador del DSL
(`script/lexer.cpp`, `parser.cpp`, `compiler.cpp`) vive en la librería `vne_script_tools`,
que las reglas del proyecto (skill vne-script-dsl) confinan explícitamente a
herramientas offline: "nunca en `vne_game`/`vne_base`". Enlazar `vne_script_tools`
directo en `vne_game` para poder recompilar en caliente rompería esa regla, aunque fuera
solo bajo `VN_EDITOR`.

**Decision.** El watcher del editor (mtime de `assets_src/scripts/demo.vns` comprobado
cada 0.5s, SPEC.md #7.4) invoca `vne_bake.exe` como un subproceso (`std::system`) cuando
detecta un cambio, y luego llama a `script_load()` (que sí es parte normal de `vne_base`,
lee el `.vnc` ya compilado) sobre el resultado. El compilador del DSL en sí nunca se
enlaza en `vne_game`, ni siquiera en Dev: sigue siendo exclusivo de `vne_bake`. Verificado
end-to-end: modificar `demo.vns` mientras `vne_game.exe` (Dev) corre dispara la
recompilación y el guion se recarga sin reiniciar el proceso (log:
"editor: '...' recargado sin reiniciar").

**Alternativas descartadas.** Enlazar `vne_script_tools` en `vne_base`/`vne_game` solo
bajo `VN_EDITOR` (nunca en Ship): técnicamente respetaría "cero parsing en release" (la
letra de la regla), pero no su espíritu — el objetivo de mantener el compilador fuera del
juego es que el juego nunca necesite saber parsear texto, ni siquiera opcionalmente.
Invocar `vne_bake` como proceso aparte mantiene esa separación limpia.

**Consecuencias.** La recarga en caliente depende de que `vne_bake.exe` exista al lado de
`vne_game.exe` en el directorio de build (siempre cierto tras un build normal) y de que
`std::system()` pueda lanzarlo — en Windows se invoca con el prefijo `.\` explícito
porque `cmd.exe` (lo que `system()` usa por debajo) no siempre resuelve el ejecutable del
propio directorio de trabajo sin él. `heap_guard_suspend/resume` rodea la llamada (mismo
motivo que ADR-0032/ADR-0035: lanzar un proceso puede asignar heap por debajo): `Dev` es
la única configuración con `VN_EDITOR` activo, y también tiene `VN_DEBUG` activo (SPEC.md
#4: sus flags son acumulativos, no exclusivos), así que `heap_guard` sí está vigilando de
verdad ahí — la excepción no es teórica en este caso.

---

## ADR-0043 — Formato `.vnm` propio: SPEC.md no especifica el layout binario del mapa

**Fecha:** 2026-09-07
**Hito:** M9
**Estado:** aceptada

**Contexto.** SPEC.md #11 lista el pipeline `maps/*.tmx -> vne_bake map -> *.vnm` pero, a
diferencia de `.vnc` (SPEC.md #9.3) y `.vnsave` (SPEC.md #8.3), no da el layout binario
exacto. Hacía falta diseñar uno.

**Decision.** `src/game/map_format.h` (compartido entre `tools/bake/main.cpp`, que
escribe, y `game/map_mode.cpp`, que lee — mismo patrón que `vm/cmd.h` comparte `Cmd`
entre `compiler.cpp` y `vm.cpp`): cabecera con magic/version/dimensiones/tamaño de tile/
número de triggers/tamaño del pool de strings, seguida de un array `u16` de gids de tile
(para render), un array de bits de colisión (1 bit por tile, 1 = bloqueado), un array de
`MapTrigger` (rectángulo en coordenadas de tile + offset al string_pool), y el
string_pool con las rutas `.vnc` de cada trigger. Es la versión más simple que cubre el
criterio de M9 (rejilla + colisión + triggers), sin nada que M9 no necesite (sin
capas múltiples, sin tilesets con más de una imagen, sin objetos que no sean
rectángulos).

**Alternativas descartadas.** Reutilizar JSON (Tiled también exporta a JSON, SPEC.md #11
lo menciona junto a TMX): añadiría una dependencia de parseo JSON solo para esto, cuando
XML ya se lee con un escáner mínimo sin dependencia nueva (ver ADR-0044). Guardar el TMX
tal cual y parsearlo en runtime: rompería "cero parsing en release" (SPEC.md #9.3, mismo
principio que ya aplica a los guiones).

**Consecuencias.** El formato no es forward-compatible con nada todavía (`k_vnm_version`
existe pero no hay migración escrita, no hace falta hasta que se rompa compatibilidad,
mismo principio que `.vnsave` en M4). Si M9 necesitara más de una capa de tiles (fondo +
decoración) en un hito futuro, el formato tendría que crecer — anotado en "Pendientes
observados".

---

## ADR-0044 — Parser de TMX propio: un escáner de subconjunto, no un parser XML general

**Fecha:** 2026-09-07
**Hito:** M9
**Estado:** aceptada

**Contexto.** TMX es XML. La lista cerrada de dependencias (SPEC.md #3) no incluye
ninguna librería XML, y añadir una solo para leer mapas de Tiled en una herramienta
offline sería una dependencia nueva sin preguntar primero (regla no negociable de
`CLAUDE.md`).

**Decision.** `tools/bake/main.cpp` (`bake_map`) escanea texto plano en vez de parsear
XML de verdad: busca subcadenas literales (`<map`, `<layer`, `<data encoding="csv">`,
`<objectgroup`, `<object `, `<property`) y extrae atributos con una búsqueda de
`nombre="valor"`. Cubre exactamente el subconjunto de TMX que este proyecto autora: una
capa de tiles llamada "tiles", una de colisión llamada "collision", ambas con
`encoding="csv"` sin comprimir (el valor por defecto de Tiled), y un `objectgroup` con
rectángulos y una propiedad `script`. Un TMX real exportado por Tiled con ese subconjunto
concreto (sin compresión, sin múltiples tilesets) encaja aquí sin cambios.

**Alternativas descartadas.** Un parser XML general de bolsillo (manejo de anidamiento
arbitrario, entidades, CDATA, atributos multilinea): mucho más código para casos que este
proyecto no necesita — los mapas los autora el propio proyecto con Tiled configurado de
una forma conocida, no se reciben TMX arbitrarios de terceros.

**Consecuencias.** Un bug real apareció durante el desarrollo (y se corrigió antes de
cerrar el hito): buscar la subcadena `"<object"` encontraba `"<objectgroup"` primero (es
un prefijo), haciendo que el primer trigger heredara los atributos de su propio grupo
contenedor en vez de los suyos — detectado por un test (`trigger_at` devolvía la
posición equivocada), corregido buscando `"<object "` (con el espacio) en su lugar. Si
Tiled cambia su formato de exportación por defecto (p. ej. a compresión zlib) en una
versión futura, este escáner no lo entenderá; revisar si eso ocurre.

---

## ADR-0045 — `GameState` v1→v2: `map_id`/`player_x`/`player_y` añadidos al final, migración escrita en el mismo commit

**Fecha:** 2026-09-07
**Hito:** M9
**Estado:** aceptada

**Contexto.** El criterio de M9 "guardar y cargar dentro del mapa funciona" exige que la
posición del jugador y el mapa activo sobrevivan a un guardado — son exactamente el tipo
de dato que el skill `vne-serializable-state` dice que va en `GameState` ("cambia lo que
el jugador ve o puede hacer al cargar la partida"). `GameState` no tenía estos campos
(SPEC.md #8.2 no los preveía, es lógico: MapMode es un hito posterior).

**Decision.** `map_id` (u16, 0 = sin mapa activo), `player_x`/`player_y` (f32) se
añadieron al final de `GameState` (extensión aditiva: no se reordenó nada existente,
así que el mismo desplazamiento (`offsetof`) sirve de frontera entre el layout v1 y v2
sin necesitar una struct `GameStateV1` duplicada). `k_savegame_version` subió de 1 a 2, y
`migrate_v1_to_v2` se escribió en el mismo commit (regla explícita de SPEC.md #8.3: "se
escriben en cuanto se rompe compatibilidad, nunca después"): copia el prefijo v1 sobre un
`GameState{}` nuevo ya puesto a cero, así que los campos nuevos quedan en su valor por
defecto sin necesitar lógica especial.

**Alternativas descartadas.** Insertar los campos nuevos en medio de la struct (p. ej.
junto a `bg_id`, temáticamente más cercano): habría requerido una migración campo a
campo en vez de un simple `memcpy` del prefijo, por una ganancia estética nula (el orden
de los campos no importa a nadie fuera de la propia struct).

**Consecuencias.** Cualquier `.vnsave` de M4-M8 (v1) sigue cargando: se migra
automáticamente y queda como v2 en memoria (no se reescribe a disco solo por cargarse,
solo al volver a guardar). Verificado con un test que fabrica a mano el formato v1 exacto
byte a byte (no hay ningún binario v1 real disponible ya para generar uno).

---

## ADR-0046 — Catálogo de localización horneado a binario, no suelto en texto plano

**Fecha:** 2026-09-07
**Hito:** M10
**Estado:** aceptada — decisión del usuario (SPEC.md #14 la marca explícitamente como
"el agente no debe tomarla solo")

**Contexto.** SPEC.md #14 lista sin resolver: "si el catálogo de localización se
empaqueta o queda suelto para permitir parches de traducción de la comunidad. Decidir en
M10." Dejarlo suelto (un `.csv`/`.json` leído directo en runtime) permitiría que alguien
parcheara una traducción sin recompilar nada; hornearlo a binario es más consistente con
el resto del pipeline de assets (`.vnc`, `.vnm`, atlas) y con "cero parsing en release"
(SPEC.md #9.3), pero exige tener `vne_bake` instalado para tocar una traducción.

**Decision.** El catálogo se hornea a binario. `assets_src/locale/<idioma>.csv` es el
formato de autoría (clave `archivo:linea:hash` + texto, editable a mano o por una
herramienta de traducción); `vne_bake catalog <entrada.csv> <salida.vnl>` lo compila a
`.vnl`, que es lo único que el juego lee en runtime (ver ADR-0047 para el layout).

**Alternativas descartadas.** Catálogo suelto en texto plano leído directo por el juego:
habría permitido parches de comunidad sin `vne_bake`, pero el usuario prefirió
consistencia con el resto del pipeline sobre esa flexibilidad.

**Consecuencias.** Una traducción nueva o corregida requiere ejecutar `vne_bake catalog`
antes de que el juego la vea — no hay forma de parchear una traducción sin las
herramientas de build. Si en el futuro se decide dar soporte a parches de comunidad sin
recompilar, haría falta revisar esta decisión (registrarlo como una nueva ADR que
sustituya a esta, no cambiar el comportamiento en silencio).

---

## ADR-0047 — Clave de catálogo: solo el hash del texto original importa en runtime, no `archivo:linea`

**Fecha:** 2026-09-07
**Hito:** M10
**Estado:** aceptada

**Contexto.** SPEC.md #9.2 especifica la clave como "archivo:linea:hash" pero también
dice: "si el texto original cambia, la clave cambia y la traducción queda marcada como
obsoleta en vez de mostrarse desactualizada" — la parte que garantiza esa propiedad es
solo el hash del texto, no el archivo ni la línea (que pueden cambiar por razones ajenas
a la traducción, p. ej. reordenar líneas de un guion).

**Decision.** El componente que de verdad se usa como clave en runtime
(`Cmd::say.key_hash`, `ChoiceOption.key_hash`, y la búsqueda en `text/catalog.cpp`) es
solo `fnv1a_u32(texto_original)`, un `u32`. La cadena completa "archivo:linea:hash" solo
existe en el catálogo de autoría (`assets_src/locale/*.csv`) para que un traductor pueda
ubicar la línea a simple vista; `vne_bake catalog-compile` extrae el hash del final de
esa cadena y descarta archivo/línea al hornear el `.vnl`.

**Alternativas descartadas.** Usar la clave completa "archivo:linea:hash" como string en
runtime (comparación de strings o un hash de la cadena completa): habría invalidado
todas las traducciones existentes cada vez que alguien moviera una línea de sitio dentro
de un guion, exactamente el problema que SPEC.md #9.2 dice que el hash debe evitar.

**Consecuencias.** Un traductor debe conservar la línea de clave tal cual al traducir
(solo cambia el texto en la línea siguiente); si la reescribe o la recalcula a mano, la
traducción deja de encontrarse. Dos textos originales distintos que por mala suerte
compartan el mismo hash de 32 bits colisionarían (mismo riesgo aceptado ya en ADR-0029/
ADR-0034 para nombres de variable/pista de música) — improbable para el tamaño de
catálogo de este proyecto.

---

## ADR-0048 — Traducción de prueba: placeholder marcado explícitamente, no japonés real

**Fecha:** 2026-09-07
**Hito:** M10
**Estado:** aceptada

**Contexto.** El criterio de M10 ("el juego cambia de español a japonés sin reiniciar")
necesita un catálogo de traducción de verdad para probar el cambio de idioma en
caliente. La regla no negociable #6 de `CLAUDE.md` prohíbe inventar contenido de juego;
fabricar una traducción japonesa de calidad desconocida (sin revisión humana ni acceso a
un traductor real) sería exactamente ese tipo de invención, solo que en otro idioma.

**Decision.** `assets_src/locale/ja.csv` se generó mecánicamente a partir de
`es.csv` anteponiendo el marcador literal `"[JA-placeholder] "` a cada texto (mismas
claves, texto no traducido de verdad). Prueba el mecanismo completo (extracción,
horneado, resolución por hash, cambio en caliente, carga de fuente CJK bajo demanda) sin
pretender ser una traducción real.

**Alternativas descartadas.** Traducir de verdad con el propio conocimiento del modelo:
descartado por la misma razón que cualquier otro contenido de juego inventado — sin
revisión humana, una "traducción" así no es fiable y podría acabar pareciendo contenido
real en vez del placeholder obvio que la regla exige. Dejar `ja.csv` vacío: no
demostraría que el pipeline resuelve claves de verdad (solo el camino "sin traducción,
cae al texto base").

**Consecuencias.** Antes de un lanzamiento real, `assets_src/locale/ja.csv` necesita una
traducción japonesa de verdad hecha por una persona — anotado en "Pendientes
observados". El pipeline en sí (extracción → horneado → resolución → cambio en caliente)
no cambia cuando eso ocurra, solo el contenido del `.csv`.

---

## ADR-0049 — El escáner de TMX se muda a `vne_script_tools` para poder tener tests

**Fecha:** 2026-09-07
**Hito:** revisión posterior a M10
**Estado:** aceptada

**Contexto.** El escáner de TMX de M9 (ADR-0044) vivía entero dentro de
`tools/bake/main.cpp`, mezclado con la lectura y escritura de archivos. Al revisarlo
aparecieron tres bugs reales de parseo — `find` devolviendo `npos` y `npos + 1`
desbordando a 0 (el escaneo reempezaba desde el principio del archivo en vez de fallar),
un `<object/>` autocerrado que se comía el objeto siguiente, y una `<property>` que se
filtraba hacia atrás al objeto anterior. Ninguno se podía cubrir con un test: el código
estaba dentro del `main()` de un ejecutable, y ningún test podía enlazar contra él.
Arreglarlos a ciegas y volver a dejarlos sin cobertura habría repetido exactamente la
situación que los produjo.

**Decisión.** El parseo se separa de la E/S y se muda a `src/script/map_bake.{h,cpp}`,
dentro de la librería `vne_script_tools`: `tmx_parse(xml, ParsedMap*, error*)` es una
función pura sobre una `std::string_view` y `write_vnm(path, ParsedMap)` hace la
escritura. `tools/bake/main.cpp` queda como un envoltorio fino que lee el archivo, llama a
las dos y reporta el error. `tests/test_map_bake.cpp` añade seis tests de regresión, uno
por bug, cada uno documentando el fallo que vigila.

**Alternativas descartadas.** Arreglar los bugs en el sitio y añadir un test de
integración que invoque `vne_bake.exe` sobre TMX de prueba: mucho más lento, depende de
rutas y del directorio de trabajo (ya dio problemas en M8), y no permite comprobar la
estructura resultante, solo el código de salida. Mover todo `tools/bake/main.cpp` a la
librería: innecesario, el resto ya es E/S trivial sin lógica que testear.

**Consecuencias.** `vne_script_tools` deja de ser "solo el compilador de guiones" y pasa a
ser "el código offline que merece tests", que es la razón por la que la librería existe.
El horneado de mapas sigue sin entrar en el juego (`vne_script_tools` no se enlaza en
`vne_game`), así que la regla de cero parsing en release no se toca. El mismo patrón es el
que debería seguir cualquier herramienta offline futura con lógica no trivial.

---

## ADR-0050 — La hoja de ruta se amplía con M11–M15; *verificar* en Linux/macOS pasa a §13

**Fecha:** 2026-09-07
**Hito:** posterior a M10
**Estado:** aceptada

**Contexto.** Con M10 cerrado, los diez hitos numerados de SPEC.md §12 estaban completos, pero
el motor no cumplía la especificación entera. La revisión posterior a M10 destapó tres clases de
deuda distintas: (a) secciones de la especificación que ningún hito pedía explícitamente y que
por eso nunca se implementaron — §7.4 completo (`src/assets/` está literalmente vacío) y el
`game.pak` de §11; (b) simplificaciones aceptadas en su momento con un ADR que se vuelven un
problema real al crecer (ADR-0022 no valida actores, ADR-0029/0034/0047 aceptan colisiones de
hash sin detectarlas, ADR-0040 deja la UI sin ratón); y (c) criterios verificados por la lógica
interna en vez de end-to-end, arrastrados desde M4 por no poder inyectar pulsaciones de teclado
en este entorno. Además, §14 afirmaba como mitigación que "el hilo de IO existe desde M1 y todas
las cargas pasan por él", lo cual es falso: no hay ningún hilo en el proyecto.

**Decisión.** Se añaden cinco hitos a §12 — M11 (sistema de assets y empaquetado), M12
(presentación y jugabilidad completas), M13 (integridad de datos y herramientas offline), M14
(configuración y localización completas) y M15 (interacción y testabilidad de la UI) — cada uno
con criterios medibles al mismo estilo que M0–M10. Se agrupan por área afectada, no por orden de
descubrimiento, de forma que cada hito siga terminando en un ejecutable que funciona.

La portabilidad se propuso inicialmente como un sexto hito ("M16 — Portabilidad real") y el
usuario lo corrigió en dos pasos, porque la propuesta confundía dos cosas distintas. Primero:
**compilar y verificar** en Linux y macOS no puede ser un hito, porque no hay esas máquinas en
el entorno y un hito de §12 tiene que poder empezarse y cerrarse; eso pasa a §13.1 junto con el
backend Metal, la captura de miniatura en GL, `sokol-shdc` y UBSan. Segundo, y más importante:
al moverlo entero yo había degradado **la portabilidad como tal** a trabajo futuro, cuando la
consigna del proyecto es la contraria — §1 la lista como prioridad 2 ("desde el principio; web y
móvil sin reescribir"), es decir, una restricción sobre cómo se escribe cada línea hoy, no una
funcionalidad pendiente. Para que esa distinción no se vuelva a perder, las reglas activas se
escriben explícitamente en §2 ("Cómo se programa la portabilidad": lo específico del SO tras
`platform/`, lo de GPU tras `gfx.h`, todo `#if` con su rama no-Windows escrita, nada que asuma
endianness o separador de rutas) y §13.1 empieza declarando que no exime de nada.

Auditoría hecha al escribir esa sección: la disciplina se había respetado. Solo tres archivos
tienen un `#if` de plataforma (`gfx_backend_d3d11.cpp`, que es el backend por definición, más
`audio/audio.cpp` y `editor/editor.cpp`, ambos con su rama POSIX ya escrita). La única deuda
encontrada es que `audio/audio.cpp` enumera un directorio con la API del sistema en vez de
hacerlo tras `platform/`, pese a que §3 asigna el filesystem a SDL3; queda asignada a M11, que
necesita esa misma operación para el backend de directorio suelto.

Se corrige además la fila falsa de §14 y se marcan como resueltas las dos decisiones de §14 que
ya se tomaron (QOI en ADR-0008, catálogo horneado en ADR-0046), añadiendo una nueva que tampoco
corresponde al agente: si `game.pak` admite archivos sueltos que lo sobrescriban (modding y
parches de traducción).

**Alternativas descartadas.** Dejar la deuda solo en "Pendientes observados": esa lista ya tiene
más de cuarenta entradas de granularidad muy desigual, sin criterios de aceptación ni orden, y
había demostrado no ser accionable — varias entradas resueltas seguían marcadas como abiertas
hitos después. Abrir un hito por cada pendiente: habría dado una veintena de hitos que no
terminan en un ejecutable que funcione, rompiendo la regla que estructura §12. Ampliar el alcance
de los hitos existentes reescribiendo M0–M10: la especificación describe lo que se construyó y
sirve de registro histórico; reescribirla borraría la traza de qué se verificó y cuándo.

**Consecuencias.** El proyecto deja de estar "terminado" tras M10 y pasa a tener cinco hitos por
delante antes de que §13 sea siquiera considerable — la frase "no implementar hasta M10
completo" de §13 se ajusta en consecuencia. Dos dependencias reales condicionan el orden: M15
necesita M11 (no hay arte de UI sin sistema de assets) y M12 se apoya en M11 para las máscaras
de transición; el resto se puede reordenar. M11 es con diferencia el más caro, porque toca cómo
carga sus archivos cada módulo del motor, y además desbloquea el 3D de §13.2, cuya nota de "lo
que ya está preparado" daba por hecho un sistema de assets que no existe.

Al sacar la portabilidad de §12, el criterio de M0 "compila en Windows, Linux y macOS con
`-Werror`" y el punto 1 de §15 quedan permanentemente sin cumplir mientras no haya hardware. Se
leen acotados a las plataformas verificadas, y queda escrito en §13.1 que todo resumen de cierre
de hito debe seguir diciendo explícitamente que Linux y macOS no se comprobaron, en vez de
omitirlo y dar la impresión de que sí.

---

## ADR-0051 — `heap_guard` pasa a `thread_local` en vez de exceptuar al hilo de IO

**Fecha:** 2026-09-08
**Hito:** M11
**Estado:** aceptada

**Contexto.** M11 introduce el primer segundo hilo del motor. `g_frame_alloc_count` (y el
flag de `heap_guard_suspend`) eran variables globales simples: en cuanto el hilo de IO
asignara memoria por su cuenta — leer un archivo a un buffer, por ejemplo — incrementaría
el mismo contador que `heap_guard_check_frame()` revisa al final del frame del hilo
principal. Eso es a la vez una carrera de datos real bajo el modelo de memoria de C++ y
una fuente de falsos positivos: el assert de cero heap por frame saltaría por asignaciones
que no ocurren en ningún frame.

**Decisión.** Ambas variables pasan a `thread_local`. Cada hilo lleva su propio contador;
el del hilo de IO simplemente no se consulta nunca, porque ese hilo no tiene "frame". La
regla de SPEC.md §4 se lee tal cual está escrita: prohíbe asignar *entre `arena_reset` y
`gfx_present`*, y el hilo de IO no está en ese tramo. `heap_guard_suspend/resume` siguen
siendo exclusivas del hilo principal.

**Alternativas descartadas.** Añadir el hilo de IO como cuarta excepción documentada junto
a Lua/audio/subproceso del editor: sería mentir sobre lo que ocurre. Esas tres excepciones
existen porque código de terceros asigna *dentro del frame* y no se puede evitar; el hilo
de IO no está en el frame en absoluto, así que no necesita ninguna dispensa — necesita que
la contabilidad sea por hilo, que es distinto. Proteger el contador con un mutex o hacerlo
atómico: costaría en el camino caliente (cada `operator new` del hilo principal) para
mezclar dos cuentas que no tienen nada que ver entre sí.

**Consecuencias.** El contador que ve el HUD sigue siendo exactamente el mismo número que
antes para el hilo principal, así que ninguna verificación previa cambia de significado.
Si algún día hay más hilos, cada uno queda contabilizado por separado sin tocar nada. El
riesgo que queda es el inverso: una asignación indebida dentro del hilo de IO no la detecta
nadie, porque su contador no se revisa — aceptado, ese hilo no tiene presupuesto de frame
que proteger.

---

## ADR-0052 — El hilo de IO solo lee bytes; decodificar y subir a GPU se queda en el principal

**Fecha:** 2026-09-08
**Hito:** M11
**Estado:** aceptada

**Contexto.** SPEC.md §7.4 pide que "la carga real ocurra en un hilo de IO dedicado". La
pregunta es dónde cortar exactamente: leer del disco, decodificar (QOI, TTF, audio) y subir
la textura a la GPU son tres pasos distintos con restricciones distintas. `sg_make_image` de
sokol_gfx no es thread-safe, y las librerías de terceros (FreeType, miniaudio) tienen sus
propias reglas de reentrada que habría que auditar una por una.

**Decisión.** El hilo de IO hace exclusivamente lo primero: resolver un nombre lógico a
bytes vía `pak_resolve()`. Todo lo demás — decodificar y crear el recurso de GPU — ocurre en
el hilo principal dentro de `assets_process_completed_loads()`. Como corolario, `pak_resolve()`
no toca ninguna arena del proyecto (usa el asignador de SDL), porque las arenas no son
thread-safe.

**Alternativas descartadas.** Decodificar también en el hilo de IO y subir solo la textura
en el principal: ganaría unos milisegundos de CPU por asset, a cambio de auditar la
reentrada de FreeType y miniaudio y de gestionar buffers de píxeles cruzando hilos. No
compensa para el tamaño de assets de una novela visual. Un pool de hilos en vez de uno
solo: SPEC.md §1 excluye explícitamente "job system o paralelismo más allá de un hilo de
carga de assets".

**Consecuencias.** Lo que se elimina es el bloqueo por E/S de disco, que es exactamente el
tirón que la tabla de riesgos de SPEC.md §14 quería evitar. El coste de decodificar sigue
cayendo en el frame: medido en `tests/test_assets.cpp`, integrar el atlas de prueba cuesta
~8 ms en Dev, un 48% del presupuesto de 16.6 ms. Por eso
`assets_process_completed_loads()` integra **una** carga por llamada y no todas las que
haya: dos texturas grandes en el mismo frame se pasarían del presupuesto. Si algún día los
assets crecen hasta que una sola integración no quepa en un frame, habrá que subir el
decode al hilo de IO — y entonces sí tocará la auditoría de reentrada que aquí se evitó.

---

## ADR-0053 — Solo `assets_texture` es asíncrona; `assets_font` y `assets_sound` son síncronas

**Fecha:** 2026-09-08
**Hito:** M11
**Estado:** aceptada

**Contexto.** SPEC.md §7.4 declara cuatro funciones (`assets_texture`, `assets_font`,
`assets_sound`, `assets_process_completed_loads`) y describe el modelo asíncrono ilustrándolo
**solo** con `assets_texture`: "devuelve inmediatamente un handle válido que apunta al
placeholder hasta que la carga termina". No dice qué debe hacer una fuente o un sonido
mientras cargan.

**Decisión.** Únicamente `assets_texture` es asíncrona. `assets_font` y `assets_sound`
resuelven por el mismo backend (así que funcionan igual con `.pak` que con directorio
suelto) pero de forma síncrona, en el hilo que las llama.

**Alternativas descartadas.** Hacerlas asíncronas por simetría: ninguna de las dos tiene
"versión placeholder" que enseñar mientras carga. `text/font.h` ya documentaba desde M2 que
"sin fuente no hay texto que dibujar" — devuelve un handle inválido si falla, no una fuente
de repuesto — y `audio_load` hace lo mismo. Diferirlas obligaría a inventar ese concepto de
placeholder solo para tener algo que devolver, o a que el llamante gestionara un "todavía
no está listo" que hoy no existe en ninguna de sus firmas. Además el coste que se ahorraría
es pequeño: parsear las métricas de un TTF no se parece a decodificar un atlas y subirlo a
la GPU.

**Consecuencias.** El hilo de IO tiene un único tipo de trabajo, lo que mantiene su cola y
su protocolo simples. Si en el futuro se cargan fuentes o bancos de sonido grandes a mitad
de partida y se nota el tirón, habrá que revisitar esto — y entonces la decisión de fondo
que hay que tomar primero es qué mostrar/oír mientras tanto, no cómo hacer el hilo.

---

## ADR-0054 — El `.pak` se lee entero a `g_arena_perm`, no se mapea con `mmap`

**Fecha:** 2026-09-08
**Hito:** M11
**Estado:** aceptada

**Contexto.** SPEC.md §7.4 describe el backend de release como "paquete .pak (builds de
release, **mapeado a memoria**)". SDL3 no expone un `mmap` portable de propósito general, y
escribirlo a mano significaría un `#if` por sistema operativo justo en la capa que M11
existe para unificar.

**Decisión.** `pak_mount()` lee el archivo entero una sola vez a `g_arena_perm` y lo deja
residente para todo el proceso. `pak_resolve()` devuelve punteros directos dentro de ese
bloque, sin copiar.

**Alternativas descartadas.** Un `mmap`/`CreateFileMapping` propio tras `platform/`: es la
implementación literal de lo que pide la especificación, pero añade código específico de
plataforma que nadie puede compilar fuera de Windows hoy (§13.1) a cambio de un beneficio
que este proyecto no puede medir — sus assets ocupan unos 12 MB.

**Consecuencias.** Se cumple la propiedad que de verdad importa (una sola lectura de disco
al arrancar, cero E/S por asset después) pero **no** la letra: no hay paginación perezosa
ni memoria compartida entre procesos, y el `.pak` entero ocupa RAM desde el arranque. Con
12 MB dentro de una arena de 64 MB no es un problema; si los assets crecen a cientos de MB
habrá que implementar el mapeo de verdad. Queda anotado en "Pendientes observados" para que
la divergencia con la especificación no se pierda.

---

## ADR-0055 — Audio empaquetado: decodificar desde memoria y hornear el catálogo de música

**Fecha:** 2026-09-08
**Hito:** M11
**Estado:** aceptada

**Contexto.** Dos problemas aparecieron al llevar el audio al backend empaquetado.
Primero: `ma_sound_init_from_file()` de miniaudio solo acepta una ruta de archivo, y en Ship
no hay archivos sueltos. Segundo: `scan_music_catalog()` construía el catálogo
`{track_id → archivo}` **enumerando** `assets_src/ogg/` en runtime, y un `.pak` no se puede
enumerar por nombre: sus entradas guardan el hash del nombre, no el nombre (que es
justamente lo que permite resolver sin comparar cadenas).

**Decisión.** Dos caminos según el backend activo, no uno solo:
- Backend suelto: `audio_load` conserva **intacto** el camino de M6, con su comprobación
  previa con `fopen` incluida (ADR-0036 depende de ella para esquivar un use-after-free real
  de miniaudio 0.11.21).
- Backend empaquetado: `ma_decoder_init_memory()` + `ma_sound_init_from_data_source()` sobre
  el puntero que ya vive dentro del `.pak` residente. El `ma_decoder` se guarda en el
  `SoundSlot` porque tiene que sobrevivir tanto como el `ma_sound` que lo referencia.
- El catálogo de música lo genera `vne_bake pack` como una entrada más dentro del `.pak`
  (`ogg_catalog.bin`, registros de tamaño fijo). En runtime se lee en vez de escanear.

**Alternativas descartadas.** Un `ma_vfs` propio que le presentara el `.pak` a miniaudio
como si fuera un sistema de archivos: es la solución "correcta" de manual, y también la que
más superficie nueva mete en la librería que ya nos dio el único use-after-free real del
proyecto. Guardar los nombres completos en las entradas del `.pak` para poder enumerarlas:
engordaría el formato fijo de 24 bytes que SPEC.md §11 define, y solo lo necesita el audio.
Extraer los `.ogg` a archivos temporales al arrancar: reintroduce E/S de disco y ensucia el
directorio del jugador.

**Consecuencias.** El camino de audio ya probado no se toca, así que el riesgo se concentra
en código nuevo que solo corre en Ship — y por eso se verificó explícitamente ejecutando
`demo_audio.vns` (que dispara `@bgm`/`@sfx`/`@stopbgm`) desde un `.pak` con los directorios
sueltos renombrados. El precio es tener dos caminos que mantener en `audio_load`: si uno se
arregla, hay que mirar el otro. El catálogo horneado además fija los `track_id` en tiempo de
horneado, lo que es más robusto que depender del orden de enumeración del sistema de
archivos.

---

## ADR-0056 — Polifonía por voces pre-creadas en `audio_load`, no por `ma_sound_init_copy`

**Fecha:** 2026-09-10
**Hito:** M12
**Estado:** aceptada

**Contexto.** M12 exige que el mismo `@sfx` disparado 5 veces en 100 ms produzca 5 voces
simultáneas. Desde M6 un `SoundHandle` tenía **una** instancia `ma_sound`, así que volver a
dispararlo la reiniciaba desde el principio en vez de superponerla.

La solución evidente en miniaudio es `ma_sound_init_copy`, que clona un sonido ya cargado, y
fue la que se planificó. Al ir a implementarla se leyó su código y resultó tener **dos
defectos que la invalidan**, ambos medidos después, no solo leídos:

1. **No funciona en el backend empaquetado**, que es precisamente el de Ship. `init_copy`
   empieza con `if (pExistingSound->pResourceManagerDataSource == NULL) return
   MA_INVALID_OPERATION;`, y ese campo solo lo rellena `ma_sound_init_from_file`. El camino
   empaquetado de M11 (ADR-0055) usa `ma_decoder_init_memory` +
   `ma_sound_init_from_data_source`, que lo deja a `NULL`. Medido sobre un `.pak` real:
   `ma_result = -3` (`MA_INVALID_OPERATION`). En Dev (suelto) devuelve `MA_SUCCESS`. Es
   decir: habría funcionado en desarrollo y estado muerta en el juego distribuido, sin que
   ningún test lo detectara.
2. **Asigna heap en cada reproducción**, o sea dentro del bucle de frame, contra SPEC.md §4.
   Medido: **2 asignaciones por clon** en el backend suelto.

El agravante es que la medición original que respaldaba el plan ("`init_copy` no asigna")
era falsa por un hueco real de `heap_guard`: solo sobrecarga `operator new`/`delete`, y
miniaudio es C y llama a `malloc` directamente, así que el contador **nunca podría** haber
visto esas asignaciones. Ver el pendiente correspondiente más abajo.

**Decisión.** Las voces se crean **todas en `audio_load`**, no por reproducción.

- Un array global `g_voices[128]` de instancias reproducibles independientes, repartido en
  bloques de `k_voices_per_sound = 8` por efecto (16 efectos con polifonía completa). Es un
  bump allocator sin liberación, igual que la caché de sonidos: un sonido cargado vive lo que
  vive el proceso. Coste estático medido: `sizeof(ma_sound) = 952`, `sizeof(ma_decoder) =
  552`, unos 192 KB en total.
- `audio_play` elige la primera voz que no esté sonando y hace `seek(0)` + `start()`. **Cero
  asignaciones**, verificado con el contador nuevo: 0 en 5 disparos, en ambos backends.
- Una voz que llega al final se libera sola: el motor llama a `ma_sound_stop` al detectar
  `ma_sound_at_end` dentro de `ma_engine_node_process`. No hace falta barrer nada por frame.
- Si las 8 están ocupadas se roba la más antigua en round-robin. Perder un efecto del todo se
  nota más que cortar su propia copia más vieja.
- La música (streaming) **no** es polifónica y conserva el camino de M6 intacto: una pista
  solapándose consigo misma no es algo que nadie quiera, y `ma_sound_init_copy` tampoco sabe
  clonar streams.

`voice_id` pasa a referenciar dos espacios distintos (voces de efecto y slots de música), así
que el bit 31 lo etiqueta y la generación baja de 16 a 15 bits — periodo de vuelta de 32768
reproducciones, holgado.

**Alternativas descartadas.** `ma_sound_init_copy` por reproducción, por lo de arriba. Crear
las voces perezosamente la primera vez que un efecto necesita solaparse: amortizado no
asignaría, pero la primera vez sí, dentro del frame, y eso sería una cuarta excepción a la
regla de cero heap — que el skill `vne-memory-model` reserva explícitamente al usuario. La
alternativa elegida **no necesita excepción nueva**: cae dentro de la de ADR-0035, que ya
cubre `audio_load`.

**Consecuencias.** Los efectos se superponen de verdad, en los dos backends, sin asignar en
el frame y sin ampliar la lista de excepciones. A cambio hay un tope duro de 16 efectos
distintos con polifonía completa: pasado ese punto se degrada a menos copias simultáneas con
un `log_warn`, nunca a un fallo. Y `audio_load` es ahora 8 veces más caro para un efecto, lo
que refuerza que la carga debe ocurrir en la transición de escena y no en mitad del diálogo.

---

## ADR-0057 — El contador de asignaciones de audio se mide con los callbacks de miniaudio

**Fecha:** 2026-09-10
**Hito:** M12
**Estado:** aceptada

**Contexto.** Para cerrar ADR-0056 hacía falta poder afirmar "`audio_play` no asigna" con un
número. `heap_guard` no sirve: solo sobrecarga `operator new`/`delete`, y miniaudio es C. La
afirmación habría sido exactamente el tipo de suposición que la regla de cero heap existe
para no tener que hacer.

**Decisión.** `audio_init` instala `ma_allocation_callbacks` propios en el `ma_engine`, que
cuentan cada `malloc`/`realloc` y delegan en el asignador del sistema. El engine los hereda a
su gestor de recursos interno, así que cubren tanto `ma_sound_init_from_file` como
`ma_decoder_*`. `audio_alloc_count()` lo expone y un test comprueba que 5 `audio_play`
seguidos lo dejan igual.

Se compila **siempre**, también en Ship: es una suma sobre un `u64` en un camino que ya va a
llamar a `malloc`, no instrumentación cara, y tenerlo solo en Debug significaría no poder
comprobar en Ship justamente lo que Ship hace distinto.

**Alternativas descartadas.** Interceptar `malloc` globalmente en `heap_guard`: es la
solución general y correcta, pero cambia el mecanismo central de verificación del proyecto y
haría aparecer de golpe asignaciones de FreeType, HarfBuzz, Lua y stb que hoy nadie ve.
Anotado como pendiente para que lo decida el usuario, no aquí de rebote.

**Consecuencias.** El subsistema de audio pasa a tener una verificación real de la regla de
cero heap en lugar de una por simetría de código (que es lo que M6 dejó escrito
explícitamente). El resto de subsistemas de terceros siguen sin ella.

---

## ADR-0058 — `heap_guard` ve por fin a las librerías de terceros, vía sus propios hooks

**Fecha:** 2026-09-10
**Hito:** M12
**Estado:** aceptada

**Contexto.** El criterio de cero asignaciones de heap por frame (SPEC.md §4) se venía dando
por verificado desde M1. **No lo estaba.** `heap_guard` solo sobrecargaba `operator new`/
`delete`, así que únicamente veía el código C++ del motor. Las siete librerías de terceros de
este proyecto están escritas en C y llaman a `malloc` directamente: SDL3, sokol, FreeType,
HarfBuzz, miniaudio, Lua y qoi. Todas invisibles.

Se descubrió en M12 (ADR-0056) al comprobar que una medición previa —"`ma_sound_init_copy` no
asigna"— era falsa: el clon hace 2 asignaciones y el contador marcaba 0. El skill
`vne-memory-model` afirmaba que "se instrumenta `malloc`"; el código nunca lo hizo. El
comentario de cabecera de `heap_guard.h`, en cambio, sí decía la verdad. Ganó el skill, que
era el documento equivocado.

**Decisión.** No se intercepta `malloc` globalmente: no hay forma portable de hacerlo (en
MSVC haría falta `_CrtSetAllocHook` y el CRT de depuración, en glibc sobrescribir el símbolo),
y la portabilidad condiciona cada línea que se escribe hoy (SPEC.md §2). En su lugar, **cada
librería se inicializa con su propio hook de asignación**, que es API pública suya e idéntica
en los tres sistemas operativos. `base/heap_guard_hooks.{h,cpp}` centraliza los asignadores
que cuentan; no contiene ni un solo `#if` de plataforma.

| Librería | Hook | Dónde se instala |
|---|---|---|
| SDL3 | `SDL_SetMemoryFunctions` (antes de `SDL_Init`) | `platform/window.cpp` |
| sokol_gfx | `sg_desc.allocator` | `gfx/gfx.cpp` |
| FreeType | `FT_MemoryRec_` + `FT_New_Library` | `text/font.cpp` |
| miniaudio | `ma_engine_config.allocationCallbacks` | `audio/audio.cpp` |
| qoi | macros `QOI_MALLOC`/`QOI_FREE` | `gfx/texture.cpp` |

HarfBuzz queda fuera porque su hook es de tiempo de compilación. En vez de exceptuarlo, se
**eliminó** su asignación por llamada: `text_layout` reutiliza un `hb_buffer_t` persistente en
lugar de crear y destruir uno en cada composición.

El contador además se desglosa por origen (`HeapSource`), porque "hubo 673 asignaciones" no
sirve para arreglar nada cuando el causante puede ser cualquiera de seis librerías.

**Lo que destapó, todo real y todo invisible hasta ahora:**

1. **673 asignaciones de SDL cada 500 ms** en `hot_reload_update`, al recorrer los directorios
   vigilados. Es herramienta de desarrollo (`#if VN_DEBUG`, inexistente en Ship), de la misma
   familia que el subproceso `vne_bake` del editor: se marca con `heap_guard_suspend/resume`.
2. **1 asignación de qoi + 2 de SDL** al integrar una textura recién cargada. Decisión
   explícita del usuario: es la excepción de ADR-0035 ("primera carga de un sonido: miniaudio
   decodifica al abrir el archivo") generalizada de audio a cualquier asset, no una cuarta
   excepción nueva.
3. **3 asignaciones de SDL, una sola vez**, dentro de `SDL_PollEvent`: inicialización diferida
   de su subsistema de eventos. No se exceptúa nada — se vacía la cola una vez al crear la
   ventana para que ocurra durante el arranque, donde asignar es legítimo.
4. **~108 asignaciones de FreeType** en la primera composición de texto, repartidas entre
   rasterizar glifos nuevos (`glyph_cache.cpp`) y traer métricas durante `hb_shape`
   (`layout.cpp`). Misma extensión de ADR-0035: leer del TTF datos que aún no estaban en
   caché es cargar un asset bajo demanda, y es el diseño que fija el skill `vne-rendering`
   (el CJK se rasteriza bajo demanda porque hornearlo entero no es viable).

**Alternativas descartadas.** Interceptar `malloc` globalmente: no es portable, que es
motivo suficiente. `_CrtSetAllocHook`: solo MSVC y solo con el CRT de depuración.

**Consecuencias.** `heap_allocs_frame_max=0` pasa a significar lo que siempre dijo que
significaba. Tres sitios que violaban la regla desde hace hitos quedan marcados como las
excepciones acotadas que son, y uno (SDL) eliminado del frame por completo. El precio es que
las excepciones de `heap_guard_suspend` ahora también tapan asignaciones *del motor* dentro de
esas ventanas, así que se han dejado lo más estrechas posible —`hb_shape` sola, no
`text_layout` entera—. Y cualquier librería nueva seguirá siendo invisible hasta que se le
instale su hook: al añadir una dependencia hay que acordarse de esta tabla.

---

## ADR-0059 — `heap_guard_suspend` lleva profundidad, no un interruptor

**Fecha:** 2026-09-10
**Hito:** M12 (revisión de cierre)
**Estado:** aceptada

**Contexto.** Revisando lo que acababa de escribir para ADR-0058 apareció que
`heap_guard_suspend`/`resume` usaban un `bool`. Las excepciones **sí se anidan**, y con un
interruptor el `resume` del ámbito interno volvía a encender el contador dejando al externo
desprotegido durante el resto de su ejecución:

- `hot_reload_update` suspende y llama a `rebake_atlas_and_reload()`, que suspende y reanuda
  dentro. Introducido por mí en ADR-0058.
- Ejecutar un `@lua` suspende (ADR-0032) y, si el guion llama a `vn.play_sfx`, acaba en
  `audio_load`, que suspende y reanuda dentro. **Esto existía desde M6** y nunca se detectó,
  porque hasta ADR-0058 las asignaciones de terceros no se contaban: el agujero no podía
  manifestarse en un contador que ya era ciego.

**Decisión.** `g_heap_guard_depth` es un `u32`: `suspend` incrementa, `resume` decrementa, y
solo cuenta cuando la profundidad es 0. Un `resume` sin su `suspend` dispara un assert en vez
de desbordar a `0xFFFFFFFF`, que dejaría el guard suspendido para siempre y convertiría la
regla en decorativa sin que nadie se enterara. `tests/test_heap_guard.cpp` fija el
comportamiento anidado, que era exactamente lo que nadie miraba.

**Consecuencias.** Las excepciones se pueden anidar sin pensarlo, que es lo que ya hacían de
hecho. `heap_guard_reset_frame` deliberadamente **no** toca la profundidad: si la reseteara,
una suspensión que cruzara el límite de frame se perdería en silencio.

---

## ADR-0060 — ImGui también cuenta, y el editor es una excepción declarada

**Fecha:** 2026-09-10
**Hito:** M12 (revisión de cierre)
**Estado:** aceptada

**Contexto.** ADR-0058 dejó dicho que "cualquier librería sin hook sigue siendo invisible".
El caso peor estaba a la vista y se me había pasado: **ImGui**. El editor corre en todos los
frames mientras está abierto, es el camino que más asigna de todo el juego en Dev, y su
asignador nunca se instrumentó. Además no se había medido nunca, porque el editor solo se
activa con F1 y en este entorno no se pueden inyectar pulsaciones: hubo que forzar
`g_active = true` temporalmente para verlo.

Medido así: **54 asignaciones en el primer frame del editor** (contexto y atlas de fuentes de
ImGui) y **1 en cuatro frames más** mientras hace crecer sus draw lists; a partir de ahí 0,
que es el comportamiento esperado de ImGui una vez sus buffers están dimensionados.

**Decisión.** Se instala el hook en los dos sitios que hacen falta —
`ImGui::SetAllocatorFunctions` y `simgui_desc_t.allocator`, que piden exactamente la misma
firma— y se añade `HeapSource::ImGui` al desglose. Y se declara el editor como excepción
acotada con `heap_guard_suspend`, de la misma familia que `hot_reload_update` y el
subproceso `vne_bake`: es herramienta de desarrollo y **no existe en Ship** (ADR-0041 lo
excluye del build entero a nivel de CMake; reverificado con `strings vne_game.exe | grep -i
imgui` sobre el binario Ship real, 0 coincidencias).

**Alternativas descartadas.** Hacer que ImGui asigne de una arena: sus buffers son suyos y
tienen su propio ciclo de vida; forzarlos a una arena de frame los rompería.

**Consecuencias.** El camino del editor pasa a estar declarado en vez de simplemente no
mirado. Queda vivo el mismo residuo de ADR-0058, ahora con un ejemplo concreto de lo fácil
que es olvidarlo: **al añadir una dependencia, instálale su hook**.

---

## ADR-0061 — El atlas es el registro de assets, y la convención de nombres de sprite

**Fecha:** 2026-09-11
**Hito:** M13
**Estado:** aceptada

**Contexto.** M13 pide un "registro de assets real que permita validar actor, pose y fondo en
tiempo de compilación", cerrando ADR-0022. Hacía falta decidir dónde vive ese registro y cómo
se nombra un sprite.

**Decisión.** No hay archivo de registro aparte: **la tabla de nombres del atlas es el
registro**. `atlas_00.bin` sube de v2 a v3 y gana un pool de nombres más entradas ordenadas
por hash. Un segundo formato que dijera lo mismo solo podría desincronizarse del atlas real.

La convención es `actor_<actor>_<pose>` y `bg_<nombre>`, derivada del nombre del archivo PNG
sin directorio ni extensión. El prefijo no es decorativo: el atlas tiene un espacio de nombres
plano, así que sin él un fondo llamado "marta" chocaría con un actor llamado "marta".

`atlas_find` compara el nombre completo tras localizar el hash por bisección, en vez de fiarse
del hash: una colisión no puede devolver el sprite equivocado en silencio, que es justo la
clase de fallo que M13 persigue en otros sitios.

La validación vive en `script/asset_validate.{h,cpp}`, fuera del parser: `parse_script` no
tiene por qué saber que existe un atlas horneado, y separarlo permite testear la regla con un
registro fabricado a mano en vez de depender del contenido de `assets_src/png/`.

**Consecuencias.** M11 había dejado el atlas sin nombres razonando que no había consumidor y
que una API sin llamante es lo que SPEC.md §1 prohíbe; ese razonamiento era correcto entonces
y caducó aquí. Los guiones de demo pasan a depender del atlas en CMake, dependencia que antes
no existía y ahora es real: sin ella `vne_bake script` podía correr antes de que el atlas
existiera y no validar nada.

Como los guiones usaban actores y fondos inexistentes, validar los rompía a todos: `vne_bake
placeholders` genera los seis que faltaban como rectángulos de color con borde y aspa
(CLAUDE.md regla 6), usando `stb_image_write` que ya venía en la dependencia `stb`. La lista
es **explícita y no se deriva de los guiones**: generar un placeholder por cada nombre que
aparece en un `.vns` haría que la validación no pudiera detectar ni un solo typo.

Limitación: los fondos se generan a 256x144 porque van dentro del atlas de 1024x1024, donde
uno a resolución completa no cabe. Se estiran a pantalla completa y se ven toscos, lo cual
para un placeholder es una ventaja. Un fondo de verdad necesitará textura suelta (SPEC.md §11).

---

## ADR-0062 — `.vnc` v5: tablas de nombres, y los ids de interner empiezan en 1

**Fecha:** 2026-09-11
**Hito:** M13
**Estado:** aceptada

**Contexto.** Al ir a dibujar fondos y actores apareció que **no se podía**: `actor_id`,
`pose_id` y `bg_id` son índices secuenciales de un interner local a cada compilación, no
hashes, así que en runtime no había ninguna forma de volver del id al nombre y de ahí al
sprite. El registro de ADR-0061 no servía de nada sin ese eslabón.

**Decisión.** El `.vnc` sube a v5 con tres tablas de `u32` al final, offsets dentro del
`string_pool`, indexadas por `id - 1`. `script_actor_name`/`script_pose_name`/`script_bg_name`
resuelven el nombre, y `VnMode` compone `actor_<a>_<p>` en un buffer de pila (sin `snprintf`
ni asignación) para buscarlo en el atlas.

Se descartó la alternativa de convertir los ids en hashes estables al estilo de ADR-0034
(`bgm_track_id`). Habría hecho que `actors[]` sobreviviera a un guardado entre guiones
distintos, que hoy no es cierto, pero obliga a subir `.vnsave` a v3 con una migración que no
puede migrar nada (los ids viejos no son mapeables) y eso es trabajo de M14. Queda anotado
como pendiente.

**Y un bug real que esto destapó: el interner empezaba en 0.** `ActorSlot` documenta
`actor_id = 0` como "slot vacío" (SPEC.md §8.2), pero el primer actor de cada guion recibía
justamente el id 0, así que era indistinguible de un hueco vacío. Nunca se manifestó porque
nada dibujaba actores; al conectar el dibujado, ese personaje simplemente no habría aparecido,
y el síntoma —"este personaje no sale"— no habría apuntado ni de lejos a la causa. Los ids
empiezan ahora en 1 en los cuatro interners; cuesta un id de 65536 y elimina la ambigüedad.

**Otro que apareció por el mismo camino:** `Show` no ponía `x`/`y`, así que un actor recién
mostrado se quedaba en (0,0). Ahora, **solo al ocupar un slot vacío** (para no deshacer un
`@move` previo), toma una posición por defecto derivada del slot: los ocho repartidos a lo
ancho, `y = 0.75` y el sprite apoyado por su base en esa altura, para que cambiar de pose a
otra de distinto alto no haga saltar al personaje.

**Consecuencias.** `@bg`, `@show`, `@hide` y `@move` por fin se ven. Añadir fondo y actores
**no sube `draw_calls`** (medido: sigue en 4): salen del mismo atlas y entran en el mismo
lote, que es exactamente para lo que existe el atlas. El HUD además deja de mentir: la
etiqueta `sprites` imprimía la constante del banco de pruebas, así que decía 5000 pasara lo
que pasara; ahora es un contador real (`g_gfx_sprite_count_last_frame`), y con la escena
dibujándose marca 5051-5086.

---

## ADR-0063 — Las colisiones de hash se detectan al hornear, y son más probables de lo que parecía

**Fecha:** 2026-09-11
**Hito:** M13
**Estado:** aceptada

**Contexto.** Tres decisiones previas resolvían nombres a ids por `fnv1a % capacidad` sin
tabla de interning y **aceptaron el riesgo de colisión sin ninguna detección**: ADR-0029
(variables y flags), ADR-0034 (pistas de música) y ADR-0047 (claves de catálogo). Una
colisión no da síntoma en el sitio del problema: dos variables comparten hueco y el guion se
comporta como si una escribiera sobre la otra; dos pistas se confunden al restaurar una
partida; dos textos comparten traducción. Es un bug de lógica imposible de rastrear desde el
síntoma.

**Decisión.** `script/hash_collisions.{h,cpp}` (un diccionario id → nombre) se usa en los tres
sitios, todos en herramientas offline: el compilador del DSL para variables, `vne_bake pack`
para pistas de música y `vne_bake catalog-extract` para claves de catálogo. El error nombra
**los dos** nombres que chocan, no solo el segundo: con uno solo, quien lo lee no sabe con qué
ha chocado ni cuál renombrar.

**Y una medición que cambia el peso de todo esto.** La colisión no es un caso teórico: con
`k_max_vars = 512`, la paradoja del cumpleaños sitúa el 50% de probabilidad hacia los **27
nombres distintos**. Medido con una lista de nombres realistas de novela visual
(`tests/test_hash_collisions.cpp`): **`puntos` y `rumor` chocan con solo 25 nombres**, los dos
en el hueco 142. Un juego real con tres docenas de variables es bastante probable que lo
sufra.

Eso significa que esta detección no es una red de seguridad para un caso raro: es la
diferencia entre que el DSL sea usable a escala o no. También sugiere que ADR-0029 se quedó
corto de dimensión — pero `k_max_vars` es un valor que SPEC.md §8.2 fija, y las secciones 4–8
son decisiones tomadas que no se rediseñan por iniciativa propia (CLAUDE.md regla 3). Queda
anotado como pendiente para que lo decida el usuario.

**Limitaciones conocidas.** Las variables que solo se tocan desde Lua (`vn.get_var("x")`) no
se ven: el compilador no interpreta el cuerpo de un `@lua`, y hacerlo sería un parser de Lua
a medias. Los flags tampoco se comprueban todavía porque no hay sintaxis `@flag` en el DSL
(la añade la etapa 5 de este mismo hito); cuando exista, se conecta igual que las variables.

**Consecuencias.** Un guion con dos variables que chocan deja de compilar en vez de
comportarse de forma extraña en runtime. El precio es que un nombre perfectamente razonable
puede ser rechazado por chocar con otro igual de razonable, y el autor tiene que renombrar
uno de los dos sin ninguna razón visible desde su punto de vista. Eso es mejor que el bug
silencioso, pero no es gratis.

---

## ADR-0064 — `vne_bake font` subsetea el TTF con hb-subset, no hornea un atlas de glifos

**Fecha:** 2026-09-11
**Hito:** M13
**Estado:** aceptada

**Contexto.** La tabla de SPEC.md §11 listaba `vne_bake font` produciendo `font_*.atlas` +
métricas, y lo marcaba como lo único del pipeline que no existía. El criterio concreto de M13
es más estrecho: **"el repositorio deja de contener una fuente de 9.5 MB"**. `NotoSansJP.ttf`
cubre el japonés entero y el proyecto usaba una fracción minúscula.

**Decisión.** `vne_bake font <entrada.ttf> <salida.ttf> [archivo_de_texto ...]` produce un
**TTF más pequeño**, no un atlas rasterizado. Usa `hb-subset`, que es parte de HarfBuzz —
**ya en la lista cerrada de SPEC.md §3**, no es dependencia nueva; solo había que enlazar su
segundo target, que el propio CMake de HarfBuzz ya compila por defecto.

Se descartó lo que la tabla decía (rasterizar a un atlas horneado) porque chocaría con el
diseño de rasterizar CJK bajo demanda que fija el skill `vne-rendering`: obligaría a decidir
por adelantado cada glifo **y cada tamaño de punto**. Un TTF reducido conserva ese diseño
intacto y sigue valiendo para cualquier tamaño. La tabla de §11 queda corregida.

El conjunto de glifos es: ASCII imprimible + acentos y signos del español + **kana y
puntuación CJK completos** + cada codepoint que aparezca en los archivos de texto que se le
pasen (guiones, catálogos, y los `.cpp` de tests que llevan kanji literales).

**Los kana están en el conjunto base por una razón medida.** La primera versión del
subconjunto no los incluía —`ja.csv` es un placeholder ASCII (ADR-0048), así que ningún
archivo del proyecto aportaba un solo kana— y `test_kinsoku` y `test_ruby` empezaron a
fallar. Sin kana, las funciones CJK del motor (kinsoku, furigana) quedan muertas aunque el
código siga ahí. Son unos 300 glifos, calderilla frente a los 9,5 MB. Los **kanji** no entran
en el base: son miles y dependen del contenido.

**Resultado medido.** `NotoSansJP.ttf` 9365 KB → 245 KB, `NotoSans.ttf` 2001 KB → 59 KB. El
directorio entero pasa de **11,4 MB a 304 KB**, y 173/173 tests siguen pasando, incluidos los
de kinsoku y furigana.

**Consecuencias.** Un carácter fuera del subconjunto se dibuja como `.notdef` (un cuadrado):
visible e inconfundible, nunca un fallo silencioso, pero hay que saberlo. Añadir diálogo en
japonés de verdad **obliga a regenerar el subconjunto** o saldrán cuadrados.

Y una consecuencia operativa que conviene no aprender por las malas: **subsetear no es
reversible**. De un subconjunto no se puede sacar otro mayor, y las originales ya no están en
el repositorio. `assets_src/ttf/README.md` documenta la procedencia, la licencia y el comando
exacto para regenerar desde una original descargada de nuevo. (Al implementar esto borré las
originales antes de tiempo, hubo que ampliar el conjunto con los kana, y se recuperaron del
historial de git.)

**Licencias.** Las dos originales son OFL 1.1, que permite modificar y redistribuir siempre
que la versión modificada siga entera bajo la misma licencia: los `OFL-*.txt` se quedan en el
repositorio. `NotoSansJP` declara además el *Reserved Font Name* `'Source'` (herencia de
Source Han Sans), que estos archivos no usan. El sufijo `-subset` deja claro que son versiones
modificadas.

---

## ADR-0065 — `@flag` en el DSL, catálogo de mapas, y por qué `.vnm` no se migra

**Fecha:** 2026-09-11
**Hito:** M13
**Estado:** aceptada

Tres decisiones pequeñas de la última etapa de M13, agrupadas porque ninguna da para un ADR
propio.

**`@flag` añade dos valores a `CmdKind`.** SPEC.md §8.1 fija una lista de 22 valores que no
incluye `SetFlag` ni `JumpIfFlag`, y las secciones 4–8 son decisiones tomadas (CLAUDE.md
regla 3). Pero SPEC.md §12 pide `@flag` explícitamente para M13 ("para no tener que bajar a
Lua solo para leer una flag"), así que la propia especificación se contradice consigo misma y
gana la instrucción concreta: se añaden los dos valores y **la lista de §8.1 queda
actualizada**, igual que se hizo en M12 cuando `Cmd` creció a 20 bytes.

Sintaxis: `@flag <nombre> on|off` (se aceptan también `true/false` y `1/0`) y, en las
condiciones, `@if flag <nombre>` / `@if not flag <nombre>`. El `flag_id` es
`fnv1a % k_max_flags`, **exactamente el mismo cálculo que `script/lua_bindings.cpp`**: si los
dos caminos usaran hashes distintos, `@flag` y `vn.set_flag` verían banderas diferentes con el
mismo nombre y se contradirían sin que nada lo avisara. Hay un test que lo fija.

Una opción de `@choice` **no** puede condicionarse por bandera: `ChoiceOption` tiene un layout
fijo en el `.vnc` (`var_id`/`op`/`rhs`, ADR-0030) sin sitio para eso. Se rechaza con un mensaje
que lo dice y propone la alternativa, en vez de compilarlo mal en silencio.

**El catálogo de mapas es el de música otra vez (ADR-0034).** `GameState.map_id` es un `u16`
fijo por SPEC.md §8.2 que debe sobrevivir a un guardado, así que no puede ser un índice en una
tabla que dependa del orden del sistema de archivos. `map_id = fnv1a(nombre) % 65536`, con el
catálogo escaneado de `assets_baked/` en backend suelto y horneado a `map_catalog.bin` en
empaquetado. Hasta aquí `map_id` estaba puesto a `1` a mano en `main.cpp` con el comentario
"cualquier valor distinto de 0 basta": guardar y cargar funcionaba **solo porque siempre se
cargaba el mismo mapa pasara lo que pasara**.

**`.vnm` no tiene función de migración, y es deliberado.** M13 listaba "migración de versión de
`.vnm`, que tiene `k_vnm_version` pero ninguna función de migración" como algo que faltaba. Al
mirarlo de cerca la premisa no se sostiene: un `.vnm` es un artefacto **generado** desde su
`.tmx` con `vne_bake map`, igual que un `.vnc` desde su `.vns`. M12 ya decidió que los
generados se **rechazan y se regeneran**, nunca se migran; solo `.vnsave` merece migración
porque es lo único que no se puede reconstruir. Escribir `migrate_vnm_v1_to_v2` contradiría
esa decisión y además no hay ninguna v1 obsoleta de la que migrar.

Lo que sí faltaba era el mensaje: el cargador juntaba magic y versión en un
`"no es un .vnm valido (magic/version)"` que no dice cuál de las dos falló ni qué hacer. Ahora
son dos mensajes y el de versión dice qué versión trae, cuál se esperaba y que hay que volver
a ejecutar `vne_bake map`.

**Indentación irregular.** El lexer divide los espacios iniciales entre 4 redondeando hacia
abajo, así que indentar con 2 espacios da nivel 0 y el cuerpo de un `@if` se queda vacío; el
error que salía era `'@if' sin '@end' correspondiente`, que manda a buscar el problema al
sitio equivocado — el `@end` está donde debe. `SourceLine` guarda ahora los espacios sin
dividir, y cuando un bloque se queda sin cerrar se mira si la línea que lo rompió tiene una
indentación que no es múltiplo de 4: si lo es, ese es el mensaje, y apunta a **esa** línea, no
a la cabecera del bloque.

---

## ADR-0066 — Presupuesto de por vida para la inicialización diferida de SDL, en vez de un punto ciego

**Fecha:** 2026-09-11
**Hito:** M13 (revisión de cierre)
**Estado:** aceptada

**Contexto.** ADR-0058 dejó dicho que SDL asigna de forma diferida dentro de su subsistema de
eventos, y lo "arregló" vaciando la cola al crear la ventana. **No bastaba.** En la revisión
de cierre de M13 apareció que el juego fallaba el assert de cero heap por frame de forma
intermitente, solo arrancando en `MapMode`. Medido: **9 asignaciones de SDL dentro de
`platform_poll_events`, una sola vez, en un frame variable** (36, 84…), porque los eventos que
las disparan —foco de ventana, entrada del ratón, cambio de pantalla— los manda el sistema
operativo cuando quiere, muchos frames después de crear la ventana.

Es peor que un fallo constante: pasaba casi siempre y fallaba de vez en cuando.

**Decisión.** No se suspende el guard en `poll_events` sin más: eso cegaría un camino que
corre en **todos** los frames, que es justo lo que ADR-0058 dice que no se haga. En su lugar,
SDL tiene un **presupuesto de por vida** de 64 asignaciones dentro de `poll_events`. Mientras
no lo agote, las suyas no cuentan; en cuanto lo agote, cuentan y el assert salta. Una fuga de
verdad dentro del manejo de input reventaría 64 en unos pocos frames y se vería exactamente
igual que antes.

Para poder medirlo hizo falta un contador **de por vida** por origen
(`heap_guard_lifetime_count`) que sube esté el guard suspendido o no — el contador por frame
no sirve, porque mientras está suspendido no registra nada y el presupuesto nunca se
consumiría.

**Un intento fallido que conviene no repetir.** La primera versión miraba el contador de por
vida de SDL *global*, no el de dentro de `poll_events`. SDL asigna a espuertas durante
`SDL_Init` y la creación de la ventana, así que cualquier presupuesto razonable ya estaba
agotado antes del primer frame y el guard no se suspendía nunca: el resultado fue que el fallo
intermitente pasó a ser constante. El presupuesto tiene que contar **solo lo que ocurre dentro
de la llamada que se quiere tolerar**.

**Consecuencias.** El arranque deja de fallar de forma intermitente (verificado en cinco
ejecuciones seguidas, 0 infracciones) sin perder la vigilancia sobre el camino de input. El
mecanismo sirve para cualquier otra librería con inicialización diferida: es una herramienta
nueva además de un arreglo puntual.

---

## ADR-0067 — Una tabla de símbolos del proyecto: el nombre deja de perderse al fabricar el id

**Fecha:** 2026-09-12
**Hito:** M14
**Estado:** aceptada

**Contexto.** Al cerrar M13 quedaban dos pendientes que parecían independientes: `k_max_vars =
512` se quedaba corto (medido: `puntos` y `rumor` colisionan con solo **25 nombres
realistas**), y `actors[]` no sobrevivía a un guardado entre guiones distintos. Al mirarlos
juntos resultó que son **el mismo problema**, y que había **seis** sitios con él:

| Sitio | Cómo fabricaba el id | Qué fallaba |
|---|---|---|
| variables, flags | `fnv1a(nombre) % capacidad` | dos nombres en el mismo hueco |
| pistas de música, mapas | `fnv1a(nombre) % 65536` | igual, con menos probabilidad |
| actores, poses, fondos, hablantes | interner local a **cada compilación** | el id 3 significaba cosas distintas en dos guiones |

Los dos sabores tienen la misma raíz: **al fabricar el id se tira el nombre**. De ahí salen
las colisiones (dos nombres, un id), la inestabilidad entre guiones (un id, dos significados)
y la imposibilidad de dibujar un actor (un id, ningún nombre) que M13 tuvo que parchear
metiendo tablas de nombres dentro de cada `.vnc`.

**Decisión.** Una **tabla de símbolos del proyecto**, construida al hornear por
`vne_bake symbols` a partir de **todos** los guiones y compartida por todos. El id es el
índice en esa tabla, asignado en orden de aparición.

Lo que se sigue de ahí, y por lo que es una solución y no una mitigación:

- **Las colisiones no se detectan: no pueden ocurrir.** El detector de M13 (ADR-0063) se
  retira para variables y flags. El problema se elimina en vez de vigilarse.
- **`k_max_vars = 512` pasa a significar lo que aparenta.** Como espacio de hash valía unos
  27 nombres; como cuenta, vale 512. Pasarse es un error al hornear con mensaje, no una
  corrupción probabilística. **No hizo falta tocar SPEC.md §8.2.**
- **Los ids son los mismos en todos los guiones**, así que `GameState` sobrevive a un
  guardado aunque se cargue con otro guion.
- **El nombre se recupera**, así que las tres tablas por guion que M13 metió en el `.vnc` v5
  sobran: el formato **baja** a v6 con menos secciones, no más.
- **Lua y el DSL no pueden divergir.** Antes eran dos copias de `fnv1a % capacidad` que tenían
  que coincidir por disciplina; ahora los dos preguntan a la misma tabla. Y de regalo, un
  nombre que no existe se puede **detectar**: `vn.get_var("typo")` avisa en vez de caer en un
  hueco cualquiera y comportarse como si valiera cero.

El id **0 está reservado en todas las clases** y no se asigna a ningún nombre. SPEC.md §8.2 lo
usa para "slot vacío" y "sin fondo"; reservarlo en todas por igual evita tener que recordar en
cuál sí y en cuál no, que en M13 costó un bug real (el primer actor de cada guion era
indistinguible de un hueco vacío).

**Por qué es su propio comando.** `vne_bake script` ve un guion cada vez y no puede dar ids
estables entre todos, así que la tabla se construye en un paso aparte que recibe todos los
guiones — mismo patrón que `catalog-extract`, que ya hacía eso por la misma razón. En CMake,
cada `.vnc` depende del `.vnsym`.

**Lua también declara nombres.** Un `vn.set_var("oro", 10)` tiene que entrar en la tabla igual
que un `@set oro = 10`, o esa variable no existiría. Se recoge con un escaneo de subcadenas
sobre el cuerpo de los `@lua`, aceptando comillas simples y dobles (Lua permite las dos; buscar
solo las dobles habría dejado sin recoger la mitad de los nombres **en silencio**). Un nombre
calculado en runtime no se puede ver, y eso se detecta donde toca: `var_id_of` avisa.

**Migración de `.vnsave` a v4, y esta sí es una migración de verdad.** Los ids de variable y
bandera cambian, así que un v3 tiene sus valores en huecos que ya no significan lo mismo. Pero
la tabla tiene todos los nombres y la fórmula vieja es conocida, así que para cada nombre se
recalcula dónde estaba y se copia a donde va. Solo se pierden los valores de nombres que ya no
usa ningún guion — justo lo que debería perderse. (Contrasta con v2 → v3, donde los ids no eran
reconstruibles y hubo que limpiar.)

**Alternativas descartadas.** Ampliar `k_max_vars`: es el parche: no arregla la
inestabilidad entre guiones, no permite recuperar el nombre, y solo empuja la colisión más
lejos. Mantener las tablas por guion de M13 y hacerlas del proyecto: eso **es** esta decisión,
solo que llamándola de otra forma.

**Consecuencias.** Un `.vnsym` desincronizado (horneado de un conjunto de guiones distinto del
que se compila) es un error nuevo posible; se reporta como lo que es, un problema de pipeline.
Y añadir un guion al proyecto obliga a regenerar la tabla, cosa que CMake ya hace solo.

---

## Pendientes observados

Anota aquí cosas detectadas fuera del alcance del hito actual, para no perderlas ni
desviarte.

- ~~**`k_max_vars = 512` se queda corto**~~ — disuelto en M14 (ADR-0067), y **sin tocar
  SPEC.md §8.2**. No se amplió el array: se quitó el hash. Con la tabla de símbolos del
  proyecto, 512 deja de ser un espacio de hash (que por la paradoja del cumpleaños valía unos
  27 nombres, medido: `puntos` y `rumor` colisionaban con 25) y pasa a ser lo que aparenta,
  una cuenta de 512 variables distintas. La segunda salida que se había anotado —"darle a las
  variables una tabla de nombres como la que M13 le dio a actores y fondos"— era exactamente
  esta, generalizada a las seis clases de símbolo en vez de a una.

- ~~**`heap_guard` no ve las asignaciones de las librerías de terceros, que son C.**~~ —
  resuelto en M12 (ADR-0058): cada librería se inicializa con su propio hook de asignación.
  Destapó y dejó arreglados cuatro sitios que violaban la regla desde hacía hitos. Queda el
  residuo de que **una dependencia nueva sigue siendo invisible hasta que se le instale su
  hook**: al añadir una, hay que acordarse de la tabla de ADR-0058.
- ~~**Nada dibuja fondos ni actores.**~~ — resuelto en M13; el resto que quedaba (`actors[]`
  entre guiones distintos) lo cerró M14 con la tabla de simbolos (ADR-0067): los ids son ahora
  del proyecto, asi que significan lo mismo en todos los guiones. Texto original:
  - ~~**Nada dibuja fondos ni actores.**~~ — resuelto en M13 (ADR-0061 y ADR-0062): el atlas
  gana tabla de nombres, el `.vnc` v5 permite volver del id al nombre, y `VnMode::render`
  dibuja fondo y actores. Queda vivo un resto: `actors[]` sigue sin sobrevivir a un guardado
  **entre guiones distintos**, porque los ids son de un interner local a cada compilacion.
  Convertirlos en hashes estables al estilo de ADR-0034 obliga a subir `.vnsave` a v3, que es
  trabajo de M14. Texto original:
  - **Nada dibuja fondos ni actores.** Detectado en la revisión de cierre de M12, y es el hueco
  más grande del proyecto ahora mismo. `@bg`, `@show`, `@hide` y `@move` mantienen estado
  correctamente en `GameState` (`bg_id`, `actors[]`), ese estado se serializa, sobrevive a un
  guardado y se puede inspeccionar en el editor — pero **ningún código lo convierte en
  sprites**. `VnMode::render()` dibuja la transición, el cuadro de diálogo y el texto, y se
  acabó. Lo único que llega a las capas `Background`/`Actors` es la rejilla de tiles y el
  cuadrado del jugador de `MapMode`, más la malla de 5000 sprites que es el banco de pruebas
  de M1. Verificado buscando en todo `src/` quién lee `bg_id` y `actors[]`: solo `vm.cpp`
  (escribe), `save.cpp` (serializa) y `editor.cpp` (muestra).
  Que los hitos se hayan podido cerrar así no es un despiste: cada criterio de SPEC.md §12 se
  refiere a otra cosa (draw calls, tiempos, voces, bytes), y ninguno dice "se ve a un
  personaje en pantalla". Está bloqueado por dos cosas reales: no hay arte de personajes ni
  fondos (y no se puede inventar, regla 6 de CLAUDE.md), y no hay registro de assets que
  traduzca `actor_id`/`pose_id`/`bg_id` a una región del atlas (ADR-0022: los nombres se
  internan sin validar contra nada). **M13 construye ese registro**, así que es el momento
  natural de abordarlo; hasta entonces, no des por hecho que `@show` hace algo visible.
- El desglose por origen (`HeapSource`) solo distingue librerías, no sitios de llamada. Para
  localizar *dónde* dentro del frame asigna una librería hubo que instrumentar a mano fase
  por fase en `main.cpp`. Si vuelve a pasar, valdría la pena un modo que registre el
  contador en cada fase del bucle en vez de tener que añadir sondas temporales.
- El tope de polifonía es de 16 efectos distintos con 8 voces cada uno (ADR-0056). Suficiente
  para los guiones de prueba actuales; si un juego real carga más efectos, `g_voices` se
  queda corto y los últimos suenan con menos copias simultáneas (avisado con `log_warn`). La
  salida sería asignar voces por demanda real en vez de un bloque fijo por sonido.

La mayoría de las entradas abiertas de esta lista quedaron asignadas a un hito concreto de
SPEC.md §12 al ampliar la hoja de ruta con M11–M15 (ADR-0050): el sistema de assets y el
`.pak` a M11; `Move`/`Transition`, `{b}`/`{w=}`/`{speed=}`, la polifonía de audio, el modo
auto proporcional y la colisión AABB a M12; la validación de actores, las colisiones de
hash, `vne_bake font`, el subconjunto de glifos, el escáner de TMX, la migración de `.vnm`,
el catálogo de mapas y `@flag` a M13; `config.ini`, el backlog relocalizable y la tabla
idioma→fuente a M14; y el ratón, el arte de UI, el visor de atlas y todo lo verificado sin
pulsaciones reales a M15.

Lo relacionado con Linux y macOS (compilar el backend GL, escribir Metal, la captura de
miniatura en GL, `sokol-shdc` y UBSan) **no** es un hito: por decisión del usuario vive en
SPEC.md §13.1, porque está bloqueado por falta de máquinas y no por falta de trabajo. Ojo a
la distinción: lo aplazado es *verificar* la portabilidad, no *programarla* — escribir el
código de forma portable es la prioridad 2 de §1 y aplica a todo lo que se escriba desde
hoy, con las reglas concretas en §2. Lo que sigue aquí sin destino es lo que no es trabajo
de programación (la traducción real de `ja.csv` necesita una persona) o lo que solo es una
nota de contexto.

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
- ~~`glyph_cache_init()`/`glyph_cache_shutdown()` existen pero no los llama nadie~~ —
  resuelto en la revision posterior a M10: se cablearon en `main.cpp` y en
  `tests/test_main.cpp`, justo despues de `gfx_init` y justo antes de `gfx_shutdown`.
  **No** en `gfx_init`/`gfx_shutdown` como decia esta nota original: `text/` depende de
  `gfx/` y nunca al reves, asi que incluir `text/glyph_cache.h` desde `gfx.cpp` habria
  invertido las capas. De paso se arreglo `glyph_cache_shutdown()`, que solo ponia
  `g_page_count = 0` y dejaba la tabla de entradas marcada como usada apuntando a paginas
  de atlas ya destruidas.
- ~~`{b}` se parsea correctamente pero no tiene ningun efecto visual~~ — resuelto en M12:
  negrita sintetica con `FT_GlyphSlot_Embolden` (no hay ningun TTF en negrita entre los
  assets). Medido: ancho de tinta 75.0 -> 82.0.
- ~~`{w=n}` y `{speed=n}` se reconocen y se descartan sin efecto~~ — resuelto en M12:
  `TypewriterEvent` en `TextLayout` los aplica de verdad. Medido: `{w=0.5}` retrasa 0.525 s.
- `GlyphQuad` en `text/layout.h` extiende el struct ilustrativo de SPEC.md §7.2 con
  `atlas_page`, `color` e `is_ruby` — necesarios para que el atlas multi-pagina y el
  marcado `{color=}`/furigana funcionen de verdad (ver comentario en `layout.h`). Si esto
  choca con algo mas adelante, es la primera pista a revisar.
- ~~La fuente de prueba `NotoSansJP.ttf` (~9.5 MB) infla el repositorio~~ — resuelto en M13
  (ADR-0064): `vne_bake font` subsetea con hb-subset. El directorio pasa de 11,4 MB a 304 KB.
  Se eligio el subconjunto y no Git LFS: LFS habria escondido el peso, no quitado.
- ~~`parse_script` es linea a linea (ADR-0024): necesita reescritura real cuando lleguen
  `@if`/`@choice` en M5~~ — resuelto en M5: parser reescrito con pila de bloques e
  indentacion significativa (`SourceLine.indent`, `parse_block`/`parse_if`/`parse_choice`).
- ~~Validacion de identificadores desconocidos limitada a etiquetas (ADR-0022)~~ — resuelto
  en M13 (ADR-0061): el registro real es la tabla de nombres del atlas, y `@show`/`@bg` con
  un nombre desconocido fallan la compilacion con archivo y linea.
- ~~`Say` no bloquea esperando input (ADR-0023): revisar en cuanto exista `VnMode` (M7)~~
  — resuelto en M7: `Say` bloquea de verdad esperando confirmacion del jugador (ADR-0039).
- Al usar `CHECK()`/`REQUIRE()` de doctest sobre un `std::string`/`std::string_view`, el
  STL de MSVC dispara C4530 (excepcion usada sin `/EHsc`) dentro de su propio
  `basic_ostream::operator<<`; se silencio con `/wd4530` solo en `vne_tests` (mismo patron
  que C5285 de doctest+`std::tuple`, ver tests/CMakeLists.txt). Si aparece en un contexto
  nuevo, es el mismo problema, no uno distinto.
- ~~Rollback: falta capturar tambien en `CmdKind::Choice` cuando M5 lo añada~~ — resuelto:
  `cmd_start` captura en `Choice` igual que en `Say` (ver vm.cpp, cierra ADR-0027).
- No hay ningun `@flag` en la sintaxis del DSL (SPEC.md #9.1 no lo tiene): las flags de
  `GameState.flags` solo se pueden leer/escribir desde Lua (`vn.get_flag`/`vn.set_flag`,
  ADR-0029). Si un hito futuro quiere condicionar el DSL a una flag directamente (no via
  variable), hara falta anadir esa sintaxis.
- ~~`vn.play_sfx()` (SPEC.md #9.4) es un no-op que solo hace `log_info`~~ — resuelto en la
  revision posterior a M10: se quedo como no-op durante M6-M10 pese a que `Sfx` ya existia
  desde M6. Ahora carga y reproduce de verdad con la misma convencion que el comando
  `@sfx` del DSL (nombre con extension, resuelto contra `assets_src/ogg/`).
- El riesgo de colision de hash de ADR-0029 (nombres de variable/flag distintos cayendo
  en el mismo `var_id`/`flag_id`) no tiene ninguna deteccion automatica todavia. Si algun
  guion futuro se comporta de forma rara con una variable, es la primera sospecha antes
  de asumir un bug logico.
- El parser de M5 (`script/parser.cpp`) exige que `@else`/`@end` esten exactamente al
  mismo nivel de indentacion que su `@if`/`@choice` de apertura; una indentacion irregular
  (3 espacios en vez de 4, tabs mezclados con espacios) no da un error claro todavia, solo
  hace que la linea no encaje en ningun nivel esperado y el guion falle a parsear con un
  mensaje generico ("inesperado aqui"). Mejorar el mensaje si llega a confundir en la
  practica.
- Las teclas F5 (guardar)/F9 (cargar)/flechas (rollback) cableadas en `main.cpp` para M4 se
  verificaron por tests automatizados (round-trip byte a byte, deshacer/rehacer, limite de
  64 pasos) pero no se probaron pulsando las teclas de verdad en la ventana interactiva en
  este entorno: no hay forma de inyectar pulsaciones de teclado real contra una ventana
  SDL desde aqui. Si algo en el cableado de `input.key_pressed[...]` especifico de M4
  estuviera mal (a diferencia de la logica que envuelve, que si esta probada), no se
  detectaria hasta una prueba manual real.
- Miniaudio 0.11.21 tiene un bug real (use-after-free al cargar un archivo inexistente,
  ADR-0036): revisar si sigue presente si se actualiza la version en un hito futuro; la
  comprobacion previa con `fopen` que lo evita puede quedarse de todas formas.
  Tambien no fue probado si el mismo problema aparece al cargar un archivo que existe
  pero esta corrupto/no es audio valido — el `fopen` previo no lo detectaria, solo
  ausencia del archivo.
- ~~`audio_stop`/`audio_play` identifican una voz por `voice_id = handle.index + 1` sin
  comprobar la generacion del `Pool`~~ — resuelto en la revision posterior a M10: el
  `voice_id` ahora empaqueta la generacion en los 16 bits altos y el indice+1 en los
  bajos (`voice_id_pack`), y `audio_stop` resuelve por `voice_resolve`, que valida rango y
  generacion igual que `pool_resolve`. Un `voice_id` de un slot ya reutilizado se ignora
  en vez de parar el sonido equivocado. Cubierto por dos tests en `test_audio.cpp`.
- El polifonismo de un mismo `SoundHandle` esta limitado a una instancia sonando a la vez
  (repetir `audio_play` sobre el mismo handle lo reinicia desde el principio en vez de
  superponer una segunda copia, ver `audio_play` en audio.h). Si un guion futuro dispara
  el mismo `@sfx` muy seguido (p. ej. pasos rapidos), se oira como si se cortara en vez de
  superponerse. Revisar si esto molesta en la practica antes de construir un pool de voces
  real.
- No se verifico con un contador de asignaciones real que `heap_guard_suspend/resume` de
  `audio_load()` (ADR-0035) efectivamente evite que el criterio de cero heap por frame
  falle cuando un `@bgm`/`@sfx` se dispara por primera vez dentro de una partida
  interactiva real (los guiones de demo que se ejecutan en la ventana interactiva de
  `main.cpp` no disparan audio todavia, solo `demo_audio.vns` via `--autoplay-script`,
  que no tiene bucle de frame). La correccion se apoya en la simetria de codigo con
  ADR-0032 (ya probado), no en una medicion directa de este caso concreto.
- Sin soporte de raton todavia (ADR-0040): toda la interaccion de M7 es solo teclado.
  Anadir posicion/clic real a `platform/input.h` cuando se necesite de verdad.
- La captura de miniatura (`gfx_backend_capture_thumbnail`, ADR-0038) solo esta
  implementada en D3D11: revisar cuando exista una maquina Linux real para escribir la
  contraparte GL con `glReadPixels` contra un FBO (mismo hueco que el resto del backend
  GL desde M1, ADR-0009).
- `SaveLoadMode`, `BacklogMode` y `MenuMode` no se probaron con pulsaciones de teclado
  reales en la ventana interactiva en este entorno (misma limitacion que F5/F9 de M4,
  ver Pendientes de esa epoca): solo se verifico que compilan, que el juego arranca sin
  crashear con ellos cableados, y su logica interna vía los tests de `mode_stack` y de
  `save.h`. El flujo completo de "abrir menu con M, bajar volumen con flechas, cerrar con
  ESC" (por ejemplo) no se ha visto funcionar de verdad.
- `BacklogMode`/`MenuMode`/`SaveLoadMode` dibujan paneles solidos con `gfx_white_texture()`
  en vez de arte real de UI (no hay pipeline de assets de UI todavia, fuera de alcance de
  M7): placeholders deliberados, obvios visualmente (skill vne-milestone-workflow).
- El modo auto de VnMode usa un tiempo de espera fijo (`k_auto_hold_seconds = 1.2s`) sin
  ajustar por la longitud del texto mostrado; SPEC.md no exige mas que "modo auto" exista,
  pero un temporizador proporcional a la longitud de la linea seria mas natural. Anotado
  para revisar si molesta en la practica.
- El criterio de rendimiento del modo skip (SPEC.md #12: "1000 comandos en menos de un
  segundo") se verifico con `vm_skip_current` directamente en un test (444us de mediana en
  Debug+ASan, 65us en Ship — muy por debajo del limite), no con el modo skip real de
  VnMode corriendo dentro de la ventana interactiva (misma limitacion de no poder pulsar
  teclas aqui). El mecanismo es identico (VnMode::update en modo skip llama exactamente a
  vm_skip_current en bucle), asi que el numero medido deberia trasladarse igual, pero no
  se confirmo end-to-end.
- El watcher de recarga de scripts de M8 solo vigila `assets_src/scripts/demo.vns` (fijo
  a un solo archivo, no un directorio entero): SPEC.md #7.4 describe un watcher generico
  de mtimes para "texturas, fuentes, shaders y guiones", que no existe todavia como
  sistema unificado. Ampliar a un directorio completo (o a los otros tipos de asset)
  cuando exista el modulo `assets/` formal.
- Los paneles del editor (M8) son minimos: inspector de `GameState` (pc, actores, 16
  variables editables), salto a comando arbitrario, contador de allocs/grafico de frame
  time, y el estado del watcher de recarga. No hay visor de atlas visual (mostrar la
  textura en si) porque `gfx.h`/`texture.h` mantienen deliberadamente los tipos de
  sokol_gfx fuera de su API publica (para no acoplar el core a un backend), y exponer un
  `ImTextureID` desde ahi habria requerido romper esa frontera o meter sokol_imgui en
  `gfx.cpp` (que si se compila en Ship). Se dejo como texto (numero de sprites) en vez de
  imagen; revisar si se necesita un visor visual real mas adelante.
- No hay tests automatizados para el watcher de recarga de scripts ni para los paneles
  del editor (ImGui no se presta a tests unitarios sencillos sin un backend de captura de
  pantalla): se verifico end-to-end a mano una vez (modificar demo.vns mientras
  vne_game.exe de Dev corria, confirmando en el log que se recompilo y recargo sin
  reiniciar, con heap_allocs_frame_max en 0 durante todo el proceso). No hay
  verificacion automatizada que impida una regresion futura aqui.
- El editor solo vigila y recarga `demo.vns`; no hay forma de cambiarlo a otro guion
  desde la UI del editor todavia (seria trivial de anadir, un campo de texto mas, pero no
  se hizo por no ampliar el alcance de M8 mas de lo que pedia el criterio de aceptacion).
- El escaner de TMX (ADR-0044) es un subconjunto deliberado: una sola capa de tiles y una
  de colision con encoding="csv" sin comprimir, un solo tileset implicito, objetos solo
  rectangulos. Si un mapa futuro necesita mas capas (fondo+decoracion), compresion zlib
  (Tiled la usa por defecto en exportaciones recientes segun la version), o multiples
  tilesets, el escaner actual no lo entendera y hay que ampliarlo primero.
- El formato `.vnm` (ADR-0043) no tiene migracion de version escrita (`k_vnm_version`
  existe pero solo hay una version). Si crece a mas de una capa de tiles, escribir la
  migracion en el mismo commit que rompa el formato (mismo principio que `.vnsave`).
- MapMode trata al jugador como un punto (sin AABB real) para la colision: cada eje se
  prueba por separado contra un solo tile de destino, lo que permite deslizarse a lo
  largo de una pared pero no detecta colision si el jugador es mas grande que un tile.
  Sin motor de fisicas (SPEC.md #10 lo dice explicito), asi que esto es intencional, no
  un descuido — anotado por si un mapa futuro con pasillos estrechos lo hace notorio.
- El flujo completo de M9 (caminar, pisar el trigger, jugar la escena de VN, volver al
  mapa con la posicion correcta) se verifico con tests automatizados sobre la logica de
  MapMode (carga, colision, deteccion de trigger) y con un smoke test de arranque sin
  crashear (heap_allocs_frame_max en 0), pero no se probo pulsando WASD de verdad en la
  ventana interactiva en este entorno (misma limitacion que el resto de UI desde M4): no
  hay forma de inyectar input real contra una ventana SDL aqui.
- Solo hay un mapa (`demo_map.tmx`, `map_id=1` fijo a mano en `main.cpp`): no existe
  todavia un catalogo de mapas por id como el de musica de M6 (ADR-0034). Si un hito
  futuro necesita mas de un mapa, hara falta resolver `map_id` a una ruta `.vnm` de la
  misma forma que `Bgm.track_id` se resuelve a un archivo de audio.
- `assets_src/locale/ja.csv` es un placeholder mecanico, no una traduccion real
  (ADR-0048): antes de cualquier lanzamiento hace falta que una persona lo traduzca de
  verdad, conservando las claves tal cual.
- El idioma activo no persiste entre sesiones (vive solo en memoria, MenuMode lo resetea
  a español cada vez que arranca el proceso): el skill vne-serializable-state dice que
  una preferencia de idioma va en `config.ini`, aparte de `GameState`, pero ese archivo
  de configuracion todavia no existe en el proyecto (ningun hito hasta ahora lo ha
  necesitado). Anotado para cuando exista.
- El backlog (M4) no se relocaliza al cambiar de idioma: las lineas ya dichas se quedan
  en el idioma en el que se dijeron (`BacklogEntry` no tiene `key_hash`, solo `text_id`
  del guion). Cambiar esto exigiria anadir un campo a `BacklogEntry`, que ademas se
  serializa en `.vnsave` (M4) — el mismo tipo de migracion de version que
  `GameState` v1->v2 (ADR-0045), no se hizo por alcance: M10 solo pedia que el dialogo en
  curso cambiara de idioma, no el historial.
- Solo hay una fuente CJK (`NotoSansJP.ttf`) y se asume que cualquier idioma no-español
  la necesita (`MenuMode::update`, comentario "un unico caso especial"): si se anade un
  tercer idioma con un alfabeto distinto (p. ej. coreano), hay que ampliar esa logica a
  una tabla idioma->fuente en vez de un booleano.
- `atlas.bin` sigue sin nombres logicos ni sub-paginas (ADR-0025, y M11 lo mencionaba en
  su descripcion): los sprites se indexan por posicion en el array. NO se implemento a
  proposito, no por olvido: hoy no hay ningun consumidor que pida un sprite por nombre --
  VnMode y MapMode dibujan con `gfx_white_texture()` y el unico lector del manifiesto es
  el stress test de M1, que recorre por indice. Anadir la tabla de nombres ahora seria una
  API sin llamante, justo lo que SPEC.md #1 dice que no se hace ("cada funcionalidad
  existe porque el juego la necesita"). El hito que lo necesitara de verdad es M15 (arte
  de UI real).
- El `.pak` se lee entero a memoria en vez de mapearse con `mmap` (ADR-0054): con ~12 MB
  de assets es irrelevante, pero es una divergencia real con la letra de SPEC.md #7.4
  ("mapeado a memoria"). Si los assets llegan a cientos de MB, hay que implementar el
  mapeo de verdad tras `platform/`.
- `assets_process_completed_loads()` integra una sola carga por llamada porque decodificar
  el atlas cuesta ~8 ms de los 16.6 ms de un frame (ADR-0052). Si en algun momento un solo
  asset no cabe en un frame, no bastara con bajar mas el limite: habra que subir el decode
  al hilo de IO, con la auditoria de reentrada de FreeType/miniaudio que ADR-0052 evito.
- El hot reload de fuentes deja huecos en el atlas de glifos: `glyph_cache_invalidate_font`
  marca las entradas como libres pero el empaquetador shelf no reaprovecha ese espacio.
  Recargar la misma fuente muchas veces en una sesion larga de desarrollo acabaria llenando
  las paginas. Solo afecta a Debug/Dev; si molesta, lo que hace falta es reempaquetar el
  atlas de glifos, no un free por glifo.
- El hot reload de texturas solo cubre el atlas (`assets_src/png/` -> `atlas_00.qoi`), que
  es el unico origen de textura que existe hoy. Cuando haya texturas sueltas (fondos
  grandes, SPEC.md #11), el watcher necesitara una tabla origen->nombre logico en vez de
  la regla fija de `hot_reload.cpp`.
- ~~`audio/audio.cpp` enumera `assets_src/ogg/` incluyendo `<windows.h>`/`<dirent.h>`
  directamente~~ — resuelto en M11: el listado vive ahora en `platform/files.h` sobre
  `SDL_EnumerateDirectory`, y `audio.cpp` ya no tiene ningun `#if` de plataforma.
  `tools/bake/main.cpp` conserva el suyo a proposito (enlaza sin SDL3, ver su CMakeLists).
- `@move` aparece en el ejemplo de sintaxis de SPEC.md #9.1 y en el skill
  `vne-script-dsl`, pero no existe: ni en `CmdKind` ni en el parser (escribirlo da
  "comando desconocido"). `Transition` tampoco tiene sintaxis asignada. Documentado como
  hueco conocido en `docs/SCRIPT_LANGUAGE.md`. Cuando se implemente `@move`, sus tres
  `f32` haran crecer `sizeof(Cmd)` de 16 a 20 bytes (el valor que SPEC.md #8.1 fija), lo
  que obliga a subir la version del `.vnc`.
- El archivo intermedio de catalogo que produce `vne_bake catalog-extract` se llama
  `.csv` por costumbre pero no es CSV: son dos lineas por entrada (clave, texto). Se
  eligio asi para no implementar escapado de comas/comillas sobre dialogo arbitrario. Si
  algun dia se quiere abrir en una hoja de calculo, hara falta un formato de verdad y una
  conversion.
- El escaner de TMX no valida que `width`/`height` del `<map>` cuadren con el numero de
  celdas del CSV de cada capa: un TMX inconsistente produciria un `.vnm` con menos tiles
  de los que la rejilla dice tener. No ocurre con archivos que exporta Tiled, solo con
  archivos editados a mano; anotado tras los arreglos de ADR-0049 por si conviene añadir
  la comprobacion.
- El cambio de idioma en caliente (M10) se verifico con tests automatizados sobre
  `text/catalog.cpp` (carga, resolucion por hash, generacion que fuerza relayout) pero no
  pulsando las flechas en el `MenuMode` real dentro de la ventana interactiva en este
  entorno (misma limitacion de siempre): no se vio el texto cambiar de espanol a
  "[JA-placeholder]" en pantalla de verdad, solo que la maquinaria que lo haria funciona.
