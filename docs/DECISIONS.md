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

## Pendientes observados

Anota aquí cosas detectadas fuera del alcance del hito actual, para no perderlas ni
desviarte.

- ~~Instalar el componente "C++ AddressSanitizer" del VS Installer~~ — resuelto: instalado
  el 2026-09-06 con permisos de administrador. Ver ADR-0007.
- UBSan no tiene equivalente en MSVC/Windows. Solo se puede verificar compilando en Linux o
  macOS con GCC/Clang.
- No se compiló ni verificó en Linux ni en macOS por no haber esas plataformas disponibles en
  este entorno. Falta esa verificación antes de considerar M0 completamente cerrado según
  SPEC.md §15.1.
