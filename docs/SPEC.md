# Especificación del proyecto — Motor de novela visual

**Nombre en clave:** `vne` (visual novel engine)
**Versión del documento:** 1.0
**Destinatario:** agente de IA encargado de la implementación

---

## 0. Cómo usar este documento

Este documento es la fuente de verdad del proyecto. Contiene decisiones ya tomadas, no
sugerencias. Antes de escribir código, lee las secciones 1 a 8 completas.

**Reglas para el agente:**

1. **No introduzcas dependencias que no estén en la sección 3.** Si crees que hace falta una,
   párate y pregunta.
2. **No cambies las decisiones de arquitectura de las secciones 4 a 8.** Si encuentras un
   problema real que las invalida, párate, explica el problema y propón la alternativa. No
   improvises un rediseño.
3. **Trabaja hito a hito** (sección 12). No empieces el hito N+1 hasta que el hito N cumpla
   todos sus criterios de aceptación.
4. **Todo hito debe compilar limpio** con `-Wall -Wextra -Werror`. En la práctica eso es MSVC:
   no hay máquinas Linux ni macOS en el entorno, así que compilar con GCC y Clang está
   aplazado (§13.1). Escribir el código de forma portable **no** lo está: ver §2.
5. **Registra cada decisión no trivial** en `docs/DECISIONS.md` con fecha, contexto y
   alternativas descartadas.
6. Si algo de este documento es ambiguo, elige la opción **más simple** y anótalo en
   `docs/DECISIONS.md`.

---

## 1. Objetivo

Construir un motor propio para desarrollar novelas visuales con secciones de exploración 2D
tipo RPG, en C++ moderno, con foco en eficiencia y portabilidad.

El motor se construye **a partir de un juego real**, no en abstracto. Cada funcionalidad
existe porque el juego la necesita.

### Prioridades, en orden

1. **Eficiencia** — frame times planos, cero asignaciones dinámicas en el bucle de frame,
   tiempos de carga imperceptibles.
2. **Portabilidad** — Windows, Linux y macOS desde el principio; web y móvil sin reescribir.
3. **Simplicidad** — el código lo mantiene una persona. Preferir 200 líneas legibles a 50
   líneas ingeniosas.
4. **Extensibilidad** — nada debe bloquear la incorporación futura de 3D simple.

### Alcance

**Dentro:**
- Modo novela visual: fondos, sprites de personaje, cuadro de diálogo, elecciones, variables.
- Modo mapa 2D tipo RPG: tilemaps, movimiento del jugador, triggers de eventos.
- Guardado y carga en cualquier punto, más rollback (retroceso) del historial.
- Scripting de guion (DSL propio) y de lógica (Lua).
- Editor de herramientas integrado en el propio ejecutable.
- Localización multi-idioma, con soporte técnico para japonés y chino.

**Fuera (explícitamente):**
- Motor de físicas.
- Sistema de entidades y componentes (ECS).
- Sistema de reflexión en tiempo de ejecución.
- Job system o paralelismo más allá de un hilo de carga de assets.
- Networking.
- 3D (planificado para más adelante; ver sección 13).

---

## 2. Plataformas objetivo

| Plataforma | Prioridad | Backend gráfico |
|---|---|---|
| Windows 10+ (x64) | 1 | D3D11 |
| Linux (x64) | 1 | OpenGL 3.3 |
| macOS 11+ (arm64, x64) | 2 | Metal |
| Web (WASM) | 3 | WebGL2 |
| Android / iOS | 4 (futuro) | GLES3 / Metal |

Requisito mínimo de hardware: GPU compatible con OpenGL 3.3 o D3D11 nivel 10_0.

### Cómo se programa la portabilidad

La prioridad 2 de §1 ("desde el principio; web y móvil sin reescribir") es una restricción sobre
cómo se escribe el código **ahora**, no una tarea futura. En la práctica significa:

- Todo lo específico del sistema operativo vive detrás de `platform/`, que es la única capa que
  habla con SDL3 para ventana, input, audio, **filesystem**, hilos y tiempo (§3). Ningún otro
  módulo incluye `<windows.h>` ni `<dirent.h>`.
- Todo lo específico de la GPU vive detrás de `gfx.h`. El resto del motor no sabe qué backend
  hay debajo.
- Un `#if` de plataforma lleva **siempre** escrita su rama no-Windows, aunque nadie la compile
  todavía. Código que solo existe para Windows no se acepta con la excusa de que las otras
  plataformas se verán más adelante.
- Nada asume endianness, tamaño de `long`, separador de rutas ni orden de enumeración de
  directorios. Los formatos en disco (`.vnc`, `.vnsave`, `.vnm`, `.vnl`, `.pak`) se leen y
  escriben con tipos de tamaño fijo.

Estado real: se ha cumplido. Tras M11, el filesystem vive detrás de `platform/files.h` (sobre
SDL3) y `audio/audio.cpp` ya no tiene ningún `#if` de plataforma. Quedan dos en el código de
runtime — `gfx_backend_d3d11.cpp`, que es el backend por definición, y `editor/editor.cpp`, con
su rama POSIX escrita — más el de `tools/bake/main.cpp`, deliberado: esa herramienta enlaza sin
SDL3 a propósito (lo explica su propio bloque en `CMakeLists.txt`), así que conserva su listado
de directorio mínimo en vez de depender de `platform/`.

Lo que **no** se puede hacer aquí es compilar y verificar fuera de Windows: ver §13.1.

---

## 3. Stack tecnológico (cerrado)

| Componente | Librería | Uso |
|---|---|---|
| Plataforma | **SDL3** | Ventana, input, gamepad, dispositivo de audio, filesystem, hilos, tiempo |
| RHI gráfico | **sokol_gfx** | Abstracción de GL / GLES3 / D3D11 / Metal / WebGL2 |
| Shaders | ~~**sokol-shdc**~~ | Previsto para compilar GLSL offline, **no se usa**: ADR-0010 los escribe a mano por backend (`src/gfx/shaders.h`). Reconsiderar al escribir MSL para Metal (§13.1). |
| Editor / herramientas | **Dear ImGui** (rama docking) | Toda la UI de desarrollo |
| Rasterizado de fuentes | **FreeType** | Glifos a bitmap |
| Shaping de texto | **HarfBuzz** | Kerning, ligaduras, escrituras complejas |
| Audio | **miniaudio** | Decodificación (OGG/WAV/FLAC) y mezcla |
| Scripting | **Lua 5.4** + **sol2** | Lógica de juego |
| Matemática | **HandmadeMath** | vec2/vec3/vec4/mat4/quat |
| Imágenes | **stb_image** | Solo en herramientas offline, nunca en runtime |
| Tests | **doctest** | Solo en compilaciones de test |

Nada más. No se usa `SDL_GPU` (su backend de WebGPU aún no está estabilizado y descarta
hardware antiguo), ni Vulkan directo, ni glm, ni EnTT, ni una librería de JSON en runtime.

**Nota sobre sol2:** debe quedar confinado a **una única unidad de traducción**
(`src/script/lua_bindings.cpp`). Sus plantillas son la única fuente real de riesgo de tiempos
de compilación del proyecto.

---

## 4. Convenciones de código

### Dialecto

- **C++20**, sin excepciones (`-fno-exceptions`) y sin RTTI (`-fno-rtti`).
- Errores como valores de retorno explícitos. Un asset que falla al cargar se sustituye por el
  placeholder rosa magenta y el juego continúa.
- Prohibido: `std::shared_ptr`, `std::function` en rutas por frame, herencia de más de un nivel,
  plantillas fuera de contenedores y handles, `dynamic_cast`, macros que no sean guardas de
  compilación condicional.
- Permitido: `std::vector` (con allocator propio), `std::span`, `std::string_view`,
  `constexpr`, inicializadores designados, `static_assert` en abundancia.

### Nombres

```
Tipos                PascalCase       struct SpriteBatch
Funciones y vars     snake_case       void gfx_draw_sprite()
Miembros privados    snake_case       u32 vertex_count
Constantes           k_snake_case     constexpr u32 k_max_actors = 32;
Enums                PascalCase       enum class CmdKind { Say, Show };
Archivos             snake_case.cpp   sprite_batch.cpp
Prefijo de módulo    en funciones libres: gfx_, text_, audio_, vm_, assets_
```

### Tipos base

Definidos en `src/base/types.h`, usados en todo el proyecto:

```cpp
using u8 = uint8_t;   using i8  = int8_t;
using u16 = uint16_t; using i16 = int16_t;
using u32 = uint32_t; using i32 = int32_t;
using u64 = uint64_t; using i64 = int64_t;
using f32 = float;    using f64 = double;
using usize = size_t;
```

### Flags de compilación

```
Debug    -O0 -g -fsanitize=address,undefined -DVN_DEBUG=1
Dev      -O2 -g -DVN_DEBUG=1 -DVN_EDITOR=1
Ship     -O3 -DNDEBUG -DVN_SHIPPING=1
Todos    -Wall -Wextra -Werror -fno-exceptions -fno-rtti
```

En `Ship`, todo el código de `src/editor/` queda excluido del build.

---

## 5. Arquitectura

### Capas

Las capas superiores conocen a las inferiores. Nunca al revés. Esta regla no se rompe.

```
  modes/      vn, map, menu             (pila de estados)
     |
  vm/ script/ intérprete + estado serializable
     |
  assets/ gfx/ text/ audio/            (servicios)
     |
  sokol_gfx                            (RHI)
     |
  SDL3                                 (plataforma)
```

`gfx` no sabe qué es un personaje. `vm` no sabe qué es una textura: solo maneja handles.

La regla de capas se ha respetado, con dos precisiones aprendidas sobre la marcha: la pila de
estados vive en `src/game/`, no en `modes/` (que quedó vacío), y dentro de la fila de servicios
**`text/` depende de `gfx/` y nunca al revés** — cablear el caché de glifos desde `gfx_init`
parece natural y crearía una dependencia circular, así que va en la capa de aplicación.

### Estructura de carpetas

```
vne/
  CMakeLists.txt
  CPM.cmake
  docs/
    DECISIONS.md
    SCRIPT_LANGUAGE.md
  src/
    base/          types.h, arena.h/.cpp, handle.h, pool.h, log.h/.cpp, math.h, str.h
    platform/      window.h/.cpp, input.h/.cpp, clock.h, files.h/.cpp
    gfx/           gfx.h/.cpp, sprite_batch.h/.cpp, texture.h/.cpp, render_target.h
    text/          font.h/.cpp, glyph_cache.h/.cpp, layout.h/.cpp
    audio/         audio.h/.cpp, bus.h
    assets/        assets.h/.cpp, pak.h/.cpp, hot_reload.h/.cpp
    vm/            cmd.h, vm.h/.cpp, state.h, save.h/.cpp, rollback.h/.cpp
    script/        lexer.h/.cpp, parser.h/.cpp, compiler.h/.cpp, lua_bindings.cpp
    modes/         mode.h, vn_mode.h/.cpp, map_mode.h/.cpp, menu_mode.h/.cpp
    editor/        editor.h/.cpp, panel_*.cpp
    main.cpp
  tools/
    bake/          main.cpp, atlas.cpp, font_bake.cpp, tmx.cpp, script_bake.cpp
  shaders/         sprite.glsl, mesh.glsl
  assets_src/      png/, ttf/, ogg/, scripts/, maps/
  assets_baked/    (generado, en .gitignore)
  tests/
```

Este árbol es el plan original. Dónde difiere hoy la realidad, para que nadie busque en vano:

| En el plan | En el repositorio |
|---|---|
| `modes/` | Vacío. Los modos y `mode.h` están en `src/game/`, junto a `map_format.h`. |
| `assets/` | Vacío. §7.4 no se implementó; lo construye M11. |
| `shaders/sprite.glsl` | Vacío. Los shaders se escriben a mano por backend en `src/gfx/shaders.h` (ADR-0010). |
| `tools/bake/` con cinco `.cpp` | Solo `main.cpp`. El horneado de mapas vive en `src/script/map_bake.cpp` para que los tests lo alcancen (ADR-0049), y `font_bake` no existe (M13). |
| `CPM.cmake` en la raíz | En `cmake/CPM.cmake`. |
| `assets_src/` sin `locale/` | Existe `assets_src/locale/` desde M10. |

### Objetivos de CMake

- `vne_base` — biblioteca estática con `base/`, `platform/`, `gfx/`, `text/`, `audio/`,
  `assets/`, `vm/`, `script/`.
- `vne_game` — ejecutable, enlaza `vne_base` + `modes/` + `editor/` (condicional).
- `vne_bake` — ejecutable de herramientas offline.
- `vne_tests` — ejecutable de tests con doctest.

Falta en esa lista `vne_script_tools`, una biblioteca estática con el lexer, el parser, el
compilador del DSL y el horneado de mapas. Existe para que ese código **nunca** entre en
`vne_game` (SPEC §9.3, cero parsing en release) y a la vez sea alcanzable por los tests; `vne_bake`
y `vne_tests` enlazan contra ella. En la práctica `vne_game` enlaza `vne_base` + `main.cpp` +
`editor/`, y los modos ya están dentro de `vne_base` al vivir en `src/game/`.

Dependencias traídas con CPM.cmake. Sin vcpkg ni Conan.

---

## 6. Fundamentos: memoria, handles, datos

### 6.1 Arenas

Tres arenas creadas al arrancar. **Dentro del bucle de frame no se llama a `malloc` ni a
`new` ni una sola vez.**

```cpp
struct Arena {
    u8*   base;
    usize size;
    usize used;
    const char* name;   // solo para el HUD de debug
};

Arena arena_create(usize size, const char* name);
void  arena_destroy(Arena* a);
void* arena_alloc(Arena* a, usize size, usize align = 16);
void  arena_reset(Arena* a);

template <typename T>
T* arena_alloc_n(Arena* a, usize count) {
    return static_cast<T*>(arena_alloc(a, sizeof(T) * count, alignof(T)));
}
```

| Arena | Tamaño inicial | Ciclo de vida |
|---|---|---|
| `g_arena_perm` | 64 MB | Nunca se resetea |
| `g_arena_scene` | 256 MB | Se resetea al cambiar de capítulo o mapa |
| `g_arena_frame` | 8 MB | Se resetea al inicio de cada frame |

En build `Debug`, `arena_reset` rellena la memoria con `0xCD` para detectar usos posteriores.

### 6.2 Handles

Ningún subsistema devuelve punteros crudos a recursos. Devuelve handles.

```cpp
template <typename Tag>
struct Handle {
    u32 index = 0;
    u32 gen   = 0;
    bool valid() const { return gen != 0; }
    bool operator==(const Handle&) const = default;
};

using TextureHandle = Handle<struct TextureTag>;
using FontHandle    = Handle<struct FontTag>;
using SoundHandle   = Handle<struct SoundTag>;
using ScriptHandle  = Handle<struct ScriptTag>;
```

Los recursos viven en pools de arrays contiguos con lista de índices libres. `gen` se
incrementa al liberar un slot, de modo que un handle caducado se detecta y devuelve el
recurso placeholder en vez de corromper memoria.

`static_assert(sizeof(TextureHandle) == 8)` — los handles son POD y se serializan directos.

### 6.3 Sin polimorfismo por elemento

**Prohibido** `virtual` en cualquier estructura que se itere por elemento: sprites, comandos,
glifos, entidades del mapa. Ahí se usan tagged unions y arrays.

**Permitido** `virtual` únicamente en las costuras que se llaman una vez por frame: la
interfaz `Mode` (`update`, `render`, `on_enter`, `on_exit`). Son tres o cuatro llamadas por
frame y el coste es irrelevante frente a la legibilidad que aporta.

### 6.4 Sistema de coordenadas

- Resolución virtual fija: **1920 x 1080**. Todo el contenido se autora en ese espacio.
- Origen arriba a la izquierda, **Y hacia abajo**.
- El juego renderiza a un render target de 1920x1080 y se presenta escalado con letterbox
  o pillarbox según el aspecto de la ventana.
- La matemática es 3D desde el inicio (`vec3`, `mat4`), aunque en modo 2D todo tenga `z` como
  mera clave de ordenación.

### 6.5 Bucle de frame

```
1. arena_reset(&g_arena_frame)
2. platform_poll_events()          -> rellena InputState
3. dt = clock_tick(), clamp a 0.1s
4. assets_process_completed_loads() -> integra cargas del hilo de IO
5. modes_update(dt)                 -> solo el modo del tope de la pila
6. modes_render()                   -> encola sprites y texto
7. gfx_flush()                      -> ordena por clave y emite draw calls
8. editor_render()                  -> solo si VN_EDITOR
9. gfx_present()
```

Sin timestep fijo. El VN no necesita determinismo por replay porque el rollback usa
instantáneas completas, no reejecución (ver 8.3).

---

## 7. Servicios

### 7.1 gfx

Batching por clave de ordenación. Objetivo: **una draw call por atlas de textura por frame**.

```cpp
struct Sprite {
    TextureHandle tex;
    f32 src_x, src_y, src_w, src_h;   // píxeles dentro del atlas
    f32 dst_x, dst_y, dst_w, dst_h;   // espacio virtual 1920x1080
    f32 rotation;                      // radianes, pivote en el centro
    u32 color;                         // RGBA8 premultiplicado
    u16 layer;                         // capa lógica
    u16 order;                         // dentro de la capa
};

void gfx_init();
void gfx_begin_frame();
void gfx_draw_sprite(const Sprite& s);
void gfx_flush();
void gfx_present(i32 window_w, i32 window_h);
```

Implementación: los sprites se acumulan en un array en `g_arena_frame`. En `gfx_flush` se
construye una clave `u64` (`layer << 48 | order << 32 | tex.index`), se ordena con radix sort,
y se emiten lotes contiguos. Vértices en un buffer dinámico único con
`sg_append_buffer`.

Capas fijas, definidas en `gfx.h`:

```
0  Background
1  BackgroundOverlay
2  Actors
3  Foreground
4  DialogueBox
5  DialogueText
6  UI
7  Transition
```

### 7.2 text

La parte más subestimada del proyecto. Se implementa bien desde el principio.

```cpp
FontHandle text_load_font(const char* path, u32 px_size);

struct GlyphQuad { f32 x, y, w, h; f32 u0, v0, u1, v1; };
struct TextLayout {
    GlyphQuad* quads;
    u32        count;
    f32        width, height;
    u32        line_count;
};

// Hace shaping con HarfBuzz, aplica word-wrap y devuelve quads listos para dibujar.
// Se asigna en la arena que se le pase (normalmente g_arena_frame).
TextLayout text_layout(FontHandle f, std::string_view utf8, f32 max_width, Arena* a);
void       text_draw(const TextLayout& l, f32 x, f32 y, u32 color, u32 visible_glyphs);
```

Requisitos:

- Caché de glifos en un atlas dinámico de páginas de 1024x1024, R8.
- Los glifos latinos se hornean offline; los CJK se rasterizan bajo demanda y se cachean.
- Word-wrap correcto para latín (por espacios) y para CJK (reglas de kinsoku básicas: no
  empezar línea con `。、」』）` ni terminarla con `「『（`).
- Soporte de furigana / ruby text en el layout desde el diseño inicial. No se puede
  retrofitear.
- Marcado inline en el texto del guion: `{b}negrita{/b}`, `{color=#ff0000}rojo{/color}`,
  `{ruby=かんじ}漢字{/ruby}`, `{w=0.5}` (pausa), `{speed=2}`.
- El efecto máquina de escribir se implementa con el parámetro `visible_glyphs`, **nunca**
  rehaciendo el layout cada frame.

### 7.3 audio

```cpp
enum class Bus : u8 { Master, Music, Sfx, Voice, Count };

void        audio_init();
SoundHandle audio_load(const char* path, bool streaming);
u32         audio_play(SoundHandle s, Bus bus, f32 volume, bool loop);
void        audio_stop(u32 voice_id, f32 fade_seconds);
void        audio_set_bus_volume(Bus b, f32 v);
void        audio_crossfade_music(SoundHandle next, f32 seconds);
```

La música se reproduce en streaming, los efectos se cargan enteros. El estado de audio
relevante para el guardado (pista actual, posición, volúmenes) forma parte de `GameState`.

### 7.4 assets

```cpp
void          assets_init(const char* pak_or_dir);
TextureHandle assets_texture(const char* logical_name);
FontHandle    assets_font(const char* logical_name, u32 px);
SoundHandle   assets_sound(const char* logical_name);
void          assets_process_completed_loads();
```

- Dos backends con la misma interfaz: **directorio suelto** (builds de desarrollo, con hot
  reload) y **paquete .pak** (builds de release, mapeado a memoria).
- La carga real ocurre en un hilo de IO dedicado con una cola. `assets_texture` devuelve
  inmediatamente un handle válido que apunta al placeholder hasta que la carga termina.
- Hot reload: en `Debug` y `Dev`, un watcher comprueba mtimes cada 500 ms y recarga
  texturas, fuentes, shaders y scripts en caliente.
- **En release, el juego no parsea ni un solo archivo de texto.**

---

## 8. Núcleo: VM, estado y guardado

Esta es la sección más importante del documento. El diseño del estado determina si el
guardado y el rollback funcionan.

### 8.1 Comandos como tagged union

```cpp
enum class CmdKind : u8 {
    Nop, Say, Show, Hide, Move, Bg, Wait, Sfx, Bgm, StopBgm,
    SetVar, AddVar, Jump, JumpIf, Choice, ChoiceEnd, Call, Return,
    LuaCall, Transition, Label, End,
};

struct Cmd {
    CmdKind kind;
    u8      _pad[3];
    union {
        struct { u16 speaker_id; u32 text_id; }                       say;
        struct { u16 actor_id; u16 pose_id; u8 slot; f32 fade; }      show;
        struct { u8 slot; f32 fade; }                                 hide;
        struct { u8 slot; f32 x, y, seconds; }                        move;
        struct { u16 bg_id; f32 fade; }                               bg;
        struct { f32 seconds; }                                       wait;
        struct { u16 sound_id; }                                      sfx;
        struct { u16 track_id; f32 fade; }                            bgm;
        struct { u16 var_id; i32 value; }                             set_var;
        struct { u32 target_pc; }                                     jump;
        struct { u16 var_id; i32 rhs; u8 op; u32 target_pc; }         jump_if;
        struct { u32 first_option; u8 option_count; }                 choice;
        struct { u32 fn_id; }                                         lua_call;
        struct { u16 kind; f32 seconds; }                             transition;
    };
};

static_assert(sizeof(Cmd) == 20);
static_assert(std::is_trivially_copyable_v<Cmd>);
```

El intérprete es un `switch` sobre `kind`, compilado con `-Wswitch` para que el compilador
avise si se olvida un caso. Sin vtables, sin asignación dinámica, tamaño fijo, y el guion
completo es un array contiguo que se puede volcar a disco sin transformación.

**Todo comando implementa tres operaciones**, como funciones libres en el intérprete, no como
métodos:

- `start` — se ejecuta una vez al llegar al comando.
- `update(dt)` — devuelve `true` cuando termina.
- `skip_to_end` — completa el efecto instantáneamente. Esta es la función que hace que el
  modo skip sea instantáneo en vez de un parche.

### 8.2 Estado serializable

**Regla absoluta:** todo el estado mutable del juego vive en una única estructura trivialmente
copiable. Nada de punteros, nada de `std::string`, nada de contenedores de tamaño dinámico.

```cpp
constexpr u32 k_max_actor_slots = 8;
constexpr u32 k_max_vars        = 512;
constexpr u32 k_max_call_depth  = 16;
constexpr u32 k_max_flags       = 2048;

struct ActorSlot {
    u16 actor_id;   // 0 = vacío
    u16 pose_id;
    f32 x, y;
    f32 alpha;
    f32 scale;
};

struct VmState {
    u32 script_id;
    u32 pc;
    u32 call_stack[k_max_call_depth];
    u8  call_depth;
    u8  cmd_phase;      // 0 = sin iniciar, 1 = en curso
    f32 cmd_timer;
    u32 visible_glyphs;
    u8  waiting_for_input;
};

struct GameState {
    u32       version;
    VmState   vm;
    ActorSlot actors[k_max_actor_slots];
    u16       bg_id;
    i32       vars[k_max_vars];
    u8        flags[k_max_flags / 8];
    u32       rng_state;
    u16       bgm_track_id;
    f32       bgm_position;
    f32       bus_volume[4];
    char      player_name[32];
    u32       playtime_seconds;
};

static_assert(std::is_trivially_copyable_v<GameState>);
```

Los textos nunca se copian al estado. Viven en el pool de strings del guion compilado y se
referencian por `u32 text_id`.

### 8.3 Guardado y rollback

**Guardar** es escribir un encabezado más un volcado binario de `GameState`, más una miniatura
PNG del framebuffer. Nada más.

```
Formato .vnsave
  offset  tamaño  contenido
  0       4       magic 'VNSV'
  4       4       version (u32)
  8       4       tamaño de GameState (u32, para validación)
  12      4       checksum CRC32 del bloque de estado
  16      N       GameState (volcado crudo)
  16+N    4       tamaño de la miniatura
  20+N    M       miniatura PNG 384x216
```

Al cargar: si `version` no coincide con la actual, se pasa por una cadena de funciones de
migración `migrate_v1_to_v2(...)`. Estas funciones se escriben en cuanto se rompe
compatibilidad, nunca después.

**Rollback:** un buffer circular de 64 instantáneas de `GameState` en `g_arena_perm`. Se
captura una instantánea justo antes de cada comando `Say` y de cada `Choice`. Retroceder es
copiar una instantánea de vuelta. Con `sizeof(GameState)` en el orden de 4 KB, 64
instantáneas ocupan 256 KB. No hay razón para optimizar esto.

El rollback **no reejecuta nada**, por lo que el motor no necesita ser determinista. Esta
simplificación es deliberada.

### 8.4 Backlog

Array circular separado de 200 entradas con `{speaker_id, text_id, voice_id}`. Vive fuera de
`GameState` porque no afecta a la lógica del juego, pero se serializa junto a él en el archivo
de guardado.

---

## 9. Lenguaje de guion (DSL)

Dos lenguajes, por diseño. El DSL cubre el diálogo y el flujo. Lua cubre la lógica compleja.

### 9.1 Sintaxis

```
# Comentario de línea

:: capitulo_1                       # etiqueta de destino de salto

@bg mansion_noche fade 0.5
@show marta neutral slot 1 fade 0.3
@bgm tema_tenso fade 1.0

marta: Buenas noches. {w=0.4} No esperaba visita.
narrador: La puerta se cerró a su espalda.
"Una línea sin hablante."

@wait 0.5
@sfx puerta_cierra
@move slot 1 to 0.7 0.5 in 0.4
@hide slot 1 fade 0.3

@set confianza = 0
@add confianza 1

@if confianza >= 3
    marta: Te estaba esperando.
@else
    marta: ¿Quién eres tú?
@end

@choice
    "Entrar en la casa" -> entrar_casa
    "Dar media vuelta" if valor > 2 -> marcharse
@end

@lua give_item("llave_oxidada")

@call escena_flashback
@return

@jump capitulo_2
@end
```

### 9.2 Reglas

- Codificación UTF-8 obligatoria. Indentación con 4 espacios, significativa solo dentro de
  bloques `@if` y `@choice`.
- Una línea que contiene `identificador:` al principio es una línea de diálogo con hablante.
- Una línea entre comillas es diálogo sin hablante.
- Los comandos empiezan por `@`.
- Los identificadores (`marta`, `mansion_noche`, `confianza`) se resuelven a IDs numéricos en
  tiempo de compilación. Un identificador desconocido es **error de compilación**, no un
  fallo en runtime.
- Los textos de diálogo se extraen a un catálogo de localización con clave estable
  `archivo:linea:hash`.

### 9.3 Compilación

`vne_bake` traduce cada `.vns` a un `.vnc`:

```
Formato .vnc
  magic 'VNCS' (4)
  version (4)
  cmd_count (4)
  string_pool_size (4)
  label_count (4)
  Cmd[cmd_count]
  string_pool (bytes UTF-8, terminados en \0, indexados por offset)
  Label[label_count]  { u32 name_hash; u32 pc; }
```

El runtime carga el `.vnc` con un solo `read` y apunta punteros a las regiones. Cero parsing.

### 9.4 Lua

Lua se usa para lógica que el DSL no cubre: inventario, sistemas de afinidad, minijuegos.

API expuesta a Lua (definida en `src/script/lua_bindings.cpp`, única unidad de traducción que
incluye sol2):

```
vn.get_var(name) / vn.set_var(name, value)
vn.get_flag(name) / vn.set_flag(name, bool)
vn.jump(label)
vn.play_sfx(name)
vn.random()                   -- usa rng_state de GameState, no math.random
```

**Restricción crítica:** el estado de Lua no se serializa. Cualquier dato que deba sobrevivir a
un guardado tiene que vivir en `GameState.vars` o `GameState.flags`. Las funciones Lua deben
ser efectivamente sin estado. Documentar esto de forma prominente.

---

## 10. Modos de juego

Pila de estados, no un `switch`. Abrir el menú apila `MenuMode` sobre `VNMode` sin
destruirlo.

```cpp
struct Mode {
    virtual ~Mode() = default;
    virtual void on_enter() {}
    virtual void on_exit()  {}
    virtual void update(f32 dt) = 0;
    virtual void render() = 0;
    virtual bool blocks_update_below() const { return true; }
    virtual bool blocks_render_below() const { return false; }
};
```

Solo el modo del tope recibe `update`. El renderizado recorre la pila de abajo arriba,
respetando `blocks_render_below`.

Modos previstos: `VnMode`, `MapMode`, `MenuMode`, `BacklogMode`, `SaveLoadMode`.

### MapMode

- Tilemaps autorados en **Tiled**, exportados a TMX/JSON y horneados a binario por `vne_bake`.
- Colisión por rejilla de bits, un bit por tile. Sin motor de físicas.
- Los triggers de evento son rectángulos con una etiqueta de guion asociada. Al pisarlos se
  apila `VnMode` con ese guion.
- El movimiento del jugador es a nivel de píxel con snapping a la rejilla de colisión.

---

## 11. Pipeline de assets

| Origen (`assets_src/`) | Herramienta | Destino (`assets_baked/`) | Estado |
|---|---|---|---|
| `png/*.png` | `vne_bake atlas` | `atlas_NN.qoi` + `atlas.bin` | parcial: `atlas.bin` sigue sin nombres ni sub-páginas (ADR-0025). M11 lo dejó así a propósito: no hay ningún consumidor que pida un sprite por nombre todavía. Lo necesita M15 |
| `ttf/*.ttf` | `vne_bake font` | `font_*.atlas` + métricas | **no existe**: las fuentes se rasterizan en runtime. M13 |
| `scripts/*.vns` | `vne_bake script` | `*.vnc` | hecho (M3) |
| `maps/*.tmx` | `vne_bake map` | `*.vnm` | hecho (M9) |
| `locale/*.csv` | `vne_bake catalog-compile` | `*.vnl` | hecho (M10, ADR-0046). No estaba en esta tabla |
| `shaders/*.glsl` | ~~`sokol-shdc`~~ | ~~`*.glsl.h`~~ | **no se usa**: escritos a mano en `src/gfx/shaders.h` (ADR-0010); `shaders/` está vacío |
| `ogg/*.ogg` | copia directa | `*.ogg` | hecho (M6) |

Todo se empaqueta en `game.pak` con `vne_bake pack <out.pak> <assets_baked_dir> <assets_src_dir>`
(M11). Las builds `Ship` lo montan y no leen ni un archivo suelto; `Debug`/`Dev` montan el
directorio, que es lo que permite la recarga en caliente. El `.pak` se lee entero a memoria al
montar en vez de mapearse con `mmap` (ADR-0054):

```
Formato .pak
  magic 'VNPK' (4)
  version (4)
  entry_count (4)
  Entry[entry_count] { u64 name_hash; u64 offset; u32 size; u8 type; u8 _pad[3]; }
  datos concatenados, alineados a 16 bytes
```

Los fondos grandes se dejan como texturas individuales fuera del atlas. El atlas es para
sprites de personaje, UI y glifos.

---

## 12. Hitos

Cada hito termina con un ejecutable que funciona. No se avanza sin cumplir los criterios.

### M0 — Esqueleto

Implementar `base/` (tipos, arena, handle, pool, log, math) y `platform/` (ventana SDL3,
input, reloj). CMake con CPM. Ventana que abre y limpia a un color.

**Criterios:** compila en Windows, Linux y macOS con `-Werror`. 60 fps estables. Un contador
de debug demuestra **cero asignaciones de heap** entre `arena_reset` y `gfx_present`. Tests de
la arena pasan bajo ASan.

### M1 — Renderizado 2D

sokol_gfx inicializado desde SDL3. Carga de texturas, sprite batcher con ordenación por clave,
render target virtual de 1920x1080 con letterbox.

**Criterios:** 5000 sprites en pantalla desde 1 atlas se emiten en **una sola draw call** a más
de 300 fps en hardware modesto. El letterbox es correcto en 16:9, 16:10, 4:3 y 21:9.

### M2 — Texto

FreeType + HarfBuzz. Caché de glifos, layout con word-wrap, marcado inline, efecto máquina de
escribir vía `visible_glyphs`.

**Criterios:** un párrafo de 500 caracteres se relayoutea en menos de 1 ms. El word-wrap CJK
respeta kinsoku. El efecto máquina de escribir no llama a `text_layout` por frame. Furigana se
renderiza correctamente.

### M3 — VM y DSL

`Cmd`, intérprete con `start`/`update`/`skip_to_end`, lexer, parser y compilador del DSL.
Comandos: `Say`, `Show`, `Hide`, `Bg`, `Wait`, `Jump`, `Label`, `End`.

**Criterios:** un guion de 200 líneas se ejecuta completo. Un identificador desconocido produce
error de compilación con archivo y línea. `skip_to_end` completa cualquier comando de forma
instantánea.

### M4 — Guardado, carga y rollback

`GameState`, serialización, cadena de migración, buffer circular de instantáneas.

**Criterios:** guardar en un punto arbitrario, cerrar el proceso, cargar y continuar con estado
idénticamente equivalente. Rollback de 64 pasos hacia atrás y hacia adelante sin divergencias.
Un test automatizado ejecuta un guion, guarda en cada comando, recarga y verifica que el
estado final coincide.

### M5 — Ramificación

`Choice`, `SetVar`, `AddVar`, `JumpIf`, `Call`, `Return`. Integración de Lua con sol2.

**Criterios:** un guion con 3 ramas y 2 finales se recorre completo. Las variables sobreviven
al guardado. Lua puede leer y escribir variables del `GameState`.

### M6 — Audio

miniaudio, buses, streaming de música, crossfade. Estado de audio en `GameState`.

**Criterios:** cargar una partida restaura la pista de música y su posición aproximada. Sin
clicks al hacer crossfade. Los volúmenes de bus persisten.

### M7 — UI de novela visual

Cuadro de diálogo, backlog, skip, auto, menú de configuración, pantalla de guardado con
miniaturas, historial.

**Criterios:** la experiencia es completa de principio a fin. El modo skip recorre 1000
comandos en menos de 1 segundo.

### M8 — Editor

Dear ImGui integrado, activable con F1. Paneles: inspector de `GameState`, salto a comando
arbitrario, visor de atlas, gráfico de frame time, contador de asignaciones, recarga de
scripts.

**Criterios:** editar un `.vns` y ver el cambio sin reiniciar. La build `Ship` no contiene
símbolos de ImGui.

### M9 — MapMode

Carga de Tiled, colisión por rejilla, movimiento del jugador, triggers que apilan `VnMode`.

**Criterios:** caminar por un mapa, pisar un trigger, jugar una escena de VN, volver al mapa
con la posición correcta. Guardar y cargar dentro del mapa funciona.

### M10 — Localización

Extracción del catálogo, cambio de idioma en caliente, fuentes CJK bajo demanda.

**Criterios:** el juego cambia de español a japonés sin reiniciar. Todos los textos vienen del
catálogo.

---

M0–M10 dejaron el motor funcionando de principio a fin, pero con tres clases de deuda: partes
de esta especificación que ningún hito pedía explícitamente y por eso nunca se implementaron
(§7.4 entero, el `.pak` de §11), simplificaciones aceptadas con un ADR que hay que revertir
cuando el proyecto crezca, y criterios que se verificaron por la lógica interna en vez de
end-to-end. Los hitos siguientes cierran esa deuda. Se pueden reordenar salvo por dos
dependencias reales: **M15 necesita M11** (no hay arte de UI sin sistema de assets) y **M12 se
apoya en M11** para las máscaras de transición. Compilar y verificar en Linux y macOS **no** es
uno de estos hitos: está bloqueado por falta de hardware, no por falta de trabajo, así que vive
en §13.1 — lo que no exime de escribir todo el código de estos hitos de forma portable, según
las reglas de §2.

### M11 — Sistema de assets y empaquetado

`src/assets/` está vacío: §7.4 nunca se implementó y hoy cada módulo abre sus archivos por su
cuenta con rutas relativas al directorio de build. Implementar `assets_init`/`assets_texture`/
`assets_font`/`assets_sound`/`assets_process_completed_loads`, el hilo de IO con cola, los dos
backends de §7.4 (directorio suelto para desarrollo y `game.pak` de §11 para release),
`atlas.bin` con nombres lógicos y sub-páginas (cierra ADR-0025) y el watcher genérico de mtimes
por tipo de asset, que sustituye al de un solo archivo fijo de M8.

**Criterios:** `assets_texture` de un asset aún no cargado devuelve en menos de 0.1 ms un handle
válido que dibuja el placeholder magenta, y la textura real aparece más tarde sin que ningún
frame supere 16.6 ms. Una build `Ship` arranca y se juega completa desde `game.pak` con el
directorio `assets_src/` renombrado. Un nombre lógico inexistente dibuja el placeholder y no
crashea nunca. Tocar un `.png`, un `.ttf` y un `.vns` en `Dev` recarga los tres en caliente.

### M12 — Presentación y jugabilidad completas

`Move` y `Transition` son los dos únicos valores de `CmdKind` de §8.1 que nunca se
implementaron; `Move` lleva `sizeof(Cmd)` de 16 a 20 bytes, lo que obliga a subir el `.vnc` a v4
con su migración. Las transiciones se implementan una sola vez como shader de pantalla completa
sobre la capa 7 con textura de máscara y umbral animado — la misma ruta de código para fade,
wipe y disolución, nunca un sistema por comando. `{w=n}` y `{speed=n}` pasan de reconocerse y
descartarse a tener efecto real sobre la máquina de escribir, y `{b}` a dibujarse con una
`FontHandle` de negrita de verdad. Modo auto proporcional a la longitud de la línea en vez del
temporizador fijo de 1.2 s. Polifonía real de efectos: hoy repetir `audio_play` sobre el mismo
handle reinicia el sonido en vez de superponer una segunda voz. Colisión por AABB en MapMode, no
por punto.

**Criterios:** un guion de demo ejecuta fade, wipe y disolución con la misma ruta de código sin
que `draw_calls` suba más de 1. `sizeof(Cmd) == 20` y una partida guardada con un `.vnc` v3 se
migra y carga. `{w=0.5}` retrasa el texto 0.5 s ±50 ms medidos. El modo skip sigue por debajo de
1 s por cada 1000 comandos. El mismo `@sfx` disparado 5 veces en 100 ms produce 5 voces
simultáneas.

### M13 — Integridad de datos y herramientas offline

Registro de assets real que permita validar actor, pose y fondo en tiempo de compilación —
cierra ADR-0022, que desde M3 deja pasar cualquier nombre mal escrito porque solo se validan
etiquetas. Detección de colisiones de hash al hornear para variables, flags, pistas de música y
claves de catálogo: ADR-0029, ADR-0034 y ADR-0047 aceptaron ese riesgo sin ninguna detección, y
una colisión hoy se manifestaría como un bug de lógica imposible de rastrear. `vne_bake font` de
§11, que no existe (las fuentes se rasterizan enteras en runtime). Subconjunto de glifos, para
dejar de arrastrar los 9.5 MB de `NotoSansJP.ttf` en el repositorio. Escáner TMX: validar
`width`/`height` del `<map>` contra las celdas reales del CSV, y rechazar con un mensaje que
nombre la causa lo que no entiende (compresión zlib, varios tilesets) en vez de producir un
`.vnm` silenciosamente incorrecto. Migración de versión de `.vnm`, que tiene `k_vnm_version`
pero ninguna función de migración. Catálogo de mapas por `map_id`, hoy fijo a mano en
`main.cpp`. `@flag` en el DSL, para no tener que bajar a Lua solo para leer una flag. Mensaje de
error útil ante indentación irregular en vez del genérico "inesperado aquí".

**Criterios:** `@show` con un actor que no está en el registro falla la compilación con archivo y
línea, igual que ya hace una etiqueta desconocida. Dos nombres de variable que colisionan en el
mismo `var_id` producen un error al hornear. Un TMX con capas comprimidas se rechaza nombrando
la compresión como causa. El repositorio deja de contener una fuente de 9.5 MB.

### M14 — Configuración y localización completas

`config.ini` no existe, pese a que el modelo de estado exige que las preferencias del jugador
vivan fuera de `GameState`: idioma, pantalla completa y volúmenes de bus deben persistir entre
sesiones. El backlog no se relocaliza porque `BacklogEntry` guarda `text_id` y no `key_hash`;
añadirlo sube `.vnsave` de v2 a v3, con la migración escrita en el mismo commit. Tabla
idioma→fuente en lugar del booleano actual, que asume que cualquier idioma que no sea español es
japonés.

**Criterios:** cambiar el idioma, cerrar el proceso y volver a abrirlo mantiene el idioma
elegido. Las entradas del backlog cambian de idioma junto con el diálogo en curso. Las tres
versiones de guardado (v1, v2, v3) cargan correctamente desde `tests/saves/` en un test.

### M15 — Interacción y testabilidad de la UI

Soporte de ratón en la UI de novela visual: ADR-0040 lo dejó fuera y M8 lo añadió solo para el
editor. Grabación y reproducción de input (`--record-input` / `--replay-input`), que cierra de
una vez la limitación arrastrada desde M4 — F5/F9, el rollback, `SaveLoadMode`, `BacklogMode`,
`MenuMode`, el movimiento con WASD y el cambio de idioma se han verificado siempre por su lógica
interna, nunca pulsando teclas de verdad. Arte de UI real en lugar de los rectángulos sólidos de
`gfx_white_texture()`. Visor de atlas visual en el editor, que es un criterio de M8 que quedó
sin cumplir. Elegir desde el editor qué guion vigilar, en vez del `demo.vns` fijo.

**Criterios:** una partida completa se juega de principio a fin solo con el ratón. Una sesión
grabada que abre el menú, baja un volumen, guarda, carga, hace rollback y cambia de idioma se
reproduce dentro de la suite de tests y termina con un `GameState` byte a byte igual al
esperado. El visor de atlas muestra la textura real, no un recuento de sprites.

---

## 13. Trabajo futuro

No implementar hasta que los hitos de §12 estén completos. Reservado aquí para que las
decisiones previas no lo bloqueen.

### 13.1 Verificación en Linux y macOS

**La portabilidad no es trabajo futuro: es la prioridad 2 de §1 y se aplica a cada línea que se
escribe hoy.** Lo único que está aplazado es *comprobarla*, porque no hay máquinas Linux ni
macOS en el entorno de desarrollo. Esa distinción es la que decide si algo se puede hacer ahora
o no, y no debe difuminarse: escribir código solo-Windows nunca está justificado por esta
sección. Las reglas activas están en §2.

Bloqueado por falta de hardware, no por esfuerzo — por eso no es un hito de §12, que exige
poder empezarse y cerrarse:
- Compilar y verificar el backend GL 3.3, escrito desde M1 y nunca compilado.
- Escribir el backend Metal, que no existe (ADR-0009). Es lo único de esta lista que además es
  trabajo de programación real y no solo verificación, y necesita una Mac para probarse.
- `gfx_backend_capture_thumbnail` en GL, hoy implementado solo en D3D11 (ADR-0038).
- Migrar los shaders a `sokol-shdc`: escribir MSL a mano para Metal es exactamente el coste que
  ADR-0010 aplazó y que la herramienta existe para evitar.
- Verificar con UBSan, que MSVC no tiene.

Consecuencia mientras tanto: el criterio de M0 "compila en Windows, Linux y macOS" y el punto 1
de §15 no se pueden cumplir tal cual. Se leen acotados a las plataformas verificadas, y todo
resumen de cierre de hito debe decir explícitamente que Linux y macOS no se comprobaron en vez
de darlos por buenos.

### 13.2 3D

El objetivo estético es PlayStation 2: baja resolución de textura, filtrado point, iluminación
por vértice, sin sombras dinámicas. Técnicamente es **más simple** que el 2D moderno.

Lo que hace falta cuando llegue el momento:
- `MeshHandle` y un formato de malla horneado.
- Un vertex format con normales y un shader de mesh.
- Una cámara con matriz de proyección perspectiva.
- Ordenación por profundidad con depth buffer para geometría opaca.

Lo que ya está preparado: la matemática es `vec3`/`mat4` desde M0 y el RHI abstrae el backend.
El sistema de assets con handles genéricos que esto da por supuesto **no existe todavía**: lo
construye M11 (§7.4 nunca se implementó), así que 3D depende de ese hito además de los demás.

---

## 14. Riesgos y decisiones pendientes

| Riesgo | Mitigación |
|---|---|
| Tiempos de compilación por sol2 | Confinado a una unidad de traducción. Si aun así molesta, sustituir por bindings a mano sobre la API C de Lua. |
| Furigana y CJK retrofiteados | Se diseñan en M2, no después. Es el punto de no retorno del sistema de texto. |
| `GameState` de capacidad fija se queda corto | Las constantes están en un solo header. Ampliarlas es un cambio de versión de guardado, no un rediseño. |
| sokol_gfx sin soporte de consolas | Si algún día importa, el RHI está aislado tras `gfx.h` y se puede portar. |
| Cargas síncronas causando tirones | **Riesgo materializado, no mitigado.** Esta fila afirmaba que "el hilo de IO existe desde M1 y todas las cargas pasan por él": es falso, `src/assets/` está vacío y no hay ningún hilo de IO en el proyecto. Hoy toda carga es síncrona. Lo corrige M11. |

**Decisiones que aún no se han tomado y que el agente NO debe tomar solo:**
1. ~~Formato final de compresión de texturas (QOI vs. BCn/ASTC). Decidir en M1.~~ Decidido en
   M1: QOI (ADR-0008). Reconsiderar solo si el tamaño de los assets llega a importar.
2. ~~Si el catálogo de localización se empaqueta o queda suelto para permitir parches de
   traducción de la comunidad. Decidir en M10.~~ Decidido por el usuario en M10: horneado a
   binario (ADR-0046).
3. Estrategia de firmado o verificación de archivos de guardado. Decidir cuando exista un
   riesgo real.
4. Si `game.pak` (§11, M11) debe permitir cargar archivos sueltos que lo sobrescriban, que es
   lo que haría posible el modding y los parches de traducción de la comunidad. Tiene las
   mismas implicaciones que la decisión 2 y por eso tampoco es del agente. Decidir en M11.

---

## 15. Definición de terminado

Un hito está terminado cuando:

1. Compila limpio con `-Wall -Wextra -Werror` en las tres plataformas de prioridad 1 y 2.
2. Pasa bajo AddressSanitizer y UndefinedBehaviorSanitizer sin reportes.
3. Cumple todos los criterios de aceptación listados en su sección.
4. Sus tests están en `tests/` y pasan.
5. `docs/DECISIONS.md` está actualizado.
6. El contador de asignaciones por frame sigue en cero.
