---
name: vne-memory-model
description: Modelo de memoria del motor vne — arenas lineales, handles con generación, pools de recursos y la regla de cero asignaciones por frame. Consulta este skill SIEMPRE que vayas a reservar memoria, crear o almacenar un recurso (textura, fuente, sonido, malla), devolver un puntero desde una API, guardar una referencia a algo, escribir un contenedor, o cuando necesites un buffer temporal dentro del bucle de frame. Úsalo también si estás a punto de escribir new, malloc, std::vector, unique_ptr o cualquier forma de asignación dinámica.
---

# Modelo de memoria de vne

Dos ideas cubren el 95% de los casos: **la memoria temporal sale de una arena** y **los
recursos se referencian por handle, nunca por puntero**.

## Las tres arenas

Creadas en `main`, en este orden, y nunca más.

| Arena | Tamaño | Se resetea |
|---|---|---|
| `g_arena_perm` | 64 MB | nunca |
| `g_arena_scene` | 256 MB | al cambiar de capítulo o de mapa |
| `g_arena_frame` | 8 MB | al inicio de cada frame |

```cpp
struct Arena {
    u8*   base;
    usize size;
    usize used;
    const char* name;
};

Arena arena_create(usize size, const char* name);
void  arena_destroy(Arena* a);
void* arena_alloc(Arena* a, usize size, usize align = 16);
void  arena_reset(Arena* a);

template <typename T>
T* arena_alloc_n(Arena* a, usize count);
```

Un bump allocator: `used` se alinea, se devuelve el puntero, `used` avanza. No hay `free`
individual. En `VN_DEBUG`, `arena_reset` rellena con `0xCD`.

Si una arena se queda sin espacio: `log_error` con el nombre de la arena y devuelve
`nullptr`. El llamante debe comprobarlo. No crece automáticamente.

## Cómo elegir arena

- ¿Vive lo que dura el proceso? → `g_arena_perm`.
- ¿Vive lo que dura la escena o el mapa? → `g_arena_scene`.
- ¿Se usa y se descarta dentro de este mismo frame? → `g_arena_frame`.

Si la respuesta es "depende", casi siempre es `g_arena_frame`. Ejemplos típicos de frame:
el array de sprites a ordenar, el `TextLayout` del cuadro de diálogo, strings formateados
para el HUD de debug, listas temporales de colisión.

**Nunca guardes un puntero a memoria de `g_arena_frame` más allá del frame actual.**

## Handles, no punteros

Ninguna API de recursos devuelve un puntero crudo.

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

`gen == 0` significa handle nulo. Al liberar un slot se incrementa `gen`, de modo que un
handle viejo deja de coincidir y la búsqueda devuelve el recurso placeholder en vez de
memoria basura. Esto convierte una clase entera de bugs en un rectángulo magenta visible.

Los handles son POD de 8 bytes y se serializan directamente al archivo de guardado. Un
puntero, no.

## Pools

Cada tipo de recurso vive en un pool: un array contiguo de tamaño fijo más una lista de
índices libres.

```cpp
template <typename T, u32 N>
struct Pool {
    T   items[N];
    u32 gens[N];
    u32 free_list[N];
    u32 free_count;
};
```

La resolución de handle a puntero es una comprobación de rango, una comparación de `gen` y
una indexación. Sin hash maps, sin indirección.

El puntero devuelto por la resolución es **válido solo dentro del ámbito actual**. Nunca lo
almacenes en un struct.

## La regla de cero asignaciones

Entre `arena_reset(&g_arena_frame)` y `gfx_present()` no debe ocurrir ni una sola llamada al
asignador del sistema.

Se verifica, no se asume. En builds `Debug` y `Dev` se sobrecargan `operator new`,
`operator delete`, y se instrumenta `malloc`, para incrementar un contador global. Al final
de cada frame:

```cpp
VN_ASSERT(g_frame_alloc_count == 0, "asignacion de heap dentro del frame");
```

El contador se muestra en el HUD de debug. Si sube de cero, es un bug de prioridad alta, no
una optimización pendiente.

Consecuencia práctica: `std::vector` está permitido en inicialización y en herramientas
offline, pero **no** dentro del frame. Para arrays dinámicos por frame, usa
`arena_alloc_n<T>(&g_arena_frame, count)` con un tamaño calculado por adelantado, o un
array de capacidad fija con `VN_ASSERT` sobre el límite.

### Las tres excepciones documentadas

La regla tiene tres excepciones acotadas, todas por código de terceros que asigna por su
cuenta y que no se puede reescribir. Se marcan con `heap_guard_suspend()` / `heap_guard_resume()`
alrededor de la llamada, nunca más ancho que eso:

| Dónde | Por qué | ADR |
|---|---|---|
| Ejecución de un `LuaCall` | El intérprete de Lua asigna al evaluar. | ADR-0032 |
| Primera carga de un sonido | miniaudio decodifica al abrir el archivo. | ADR-0035 |
| Lanzar `vne_bake` desde el editor | `system()` asigna; solo en builds `Dev`. | M8 |

La primera la decidió el usuario tras pararse a preguntar, porque era un conflicto real entre
dos reglas del proyecto. **No amplíes esta lista por tu cuenta**: si encuentras un cuarto caso,
párate y pregunta, igual que se hizo con el primero. Suspender el guard para tapar una
asignación propia sería exactamente el abuso que la regla existe para impedir.

## Hilos

**Hoy el motor es de un solo hilo.** El hilo de carga de assets que describe SPEC.md §7.4 no
existe todavía: `src/assets/` está vacío y toda carga es síncrona. Lo construye M11, así que
hasta entonces no hay ninguna cola ni `assets_process_completed_loads()` al que llamar, por
mucho que otras partes de la documentación lo den por hecho.

Cuando exista, el diseño es: solo dos hilos, el principal y el de carga. Cada uno con su propia
arena de scratch — **las arenas no son thread-safe y no se comparten**. El hilo de IO comunica
resultados por una cola con un mutex y el principal los integra en
`assets_process_completed_loads()`, que es el único punto donde un asset cargado entra en el
mundo del juego.
