---
name: vne-cpp-style
description: Dialecto de C++, convenciones de nombres, estructura de headers y construcciones prohibidas del motor vne. Consulta este skill SIEMPRE antes de crear o editar cualquier archivo .h o .cpp del proyecto, antes de añadir un include, y antes de decidir si algo debe ser una clase, una función libre o un struct POD. Úsalo también cuando dudes si puedes usar virtual, plantillas, excepciones, std::function, smart pointers o cualquier feature de la librería estándar.
---

# Estilo de C++ en vne

El objetivo es código plano, legible y sin abstracciones que escondan coste. Si dudas entre
dos opciones, elige la que se pueda leer de arriba abajo sin saltar a otro archivo.

## Dialecto

C++20, compilado con `-fno-exceptions -fno-rtti`.

Los errores son valores de retorno explícitos:

```cpp
enum class LoadResult : u8 { Ok, NotFound, BadFormat, OutOfMemory };

LoadResult texture_load(const char* path, TextureHandle* out);
```

Un asset que falla se sustituye por el placeholder magenta y el juego continúa. No hay
`abort` en release salvo corrupción de memoria detectada.

## Prohibido

| Construcción | Alternativa |
|---|---|
| `throw` / `try` | valor de retorno enumerado |
| `dynamic_cast`, `typeid` | tagged union con `switch` |
| `std::shared_ptr`, `std::unique_ptr` de recursos | `Handle<T>` |
| `std::function` en rutas por frame | puntero a función crudo o `switch` |
| `virtual` en tipos que se iteran por elemento | tagged union |
| Herencia de más de un nivel | composición |
| Plantillas fuera de contenedores/handles | funciones normales |
| Macros que no sean guardas `#if` | `constexpr`, `inline` |
| `std::string` en estructuras persistentes | `char[N]` o `u32 text_id` |
| `iostream` | `log_info` / `log_warn` / `log_error` |
| `new` / `malloc` dentro del bucle de frame | `arena_alloc(&g_arena_frame, ...)` |

## Permitido y recomendado

`std::vector` (con allocator del proyecto), `std::span`, `std::string_view`, `constexpr`,
inicializadores designados, `[[nodiscard]]`, `static_assert` en abundancia.

Un `virtual` está permitido en un solo sitio: la interfaz `Mode` de `src/modes/mode.h`, que
se llama tres o cuatro veces por frame. Nada más.

## Nombres

```
Tipos                PascalCase        struct SpriteBatch
Funciones y vars     snake_case        void gfx_draw_sprite()
Constantes           k_snake_case      constexpr u32 k_max_actors = 32;
Enums                PascalCase        enum class CmdKind { Say, Show };
Archivos             snake_case        sprite_batch.cpp
Globales             g_snake_case      Arena g_arena_frame;
```

Cada módulo prefija sus funciones libres con su nombre: `gfx_`, `text_`, `audio_`, `vm_`,
`assets_`, `arena_`, `script_`. Esto sustituye a los namespaces anidados, que no se usan.

## Tipos base

Nunca uses `int`, `unsigned`, `long` ni `float` directamente. Usa los alias de
`src/base/types.h`: `u8 u16 u32 u64 i8 i16 i32 i64 f32 f64 usize`.

## Estructura de un header

```cpp
#pragma once
#include "base/types.h"

// Comentario de una línea: qué hace este módulo y qué NO hace.

struct Foo { ... };

// API pública, funciones libres con prefijo de módulo.
void foo_init();
void foo_shutdown();
```

Sin `#include` de conveniencia. Cada archivo incluye exactamente lo que usa. Prohibido
incluir headers de una capa superior desde una inferior (ver jerarquía en `docs/SPEC.md` §5):
`gfx` no puede incluir nada de `vm`, `modes` ni `editor`.

## Structs de datos

Por defecto, `struct` con miembros públicos y sin constructores. Solo escribe un constructor
si hay un invariante real que mantener. Nada de getters y setters triviales.

Añade `static_assert(std::is_trivially_copyable_v<T>)` a cualquier struct que vaya a formar
parte de `GameState` o de un formato en disco.

## Comentarios

Comenta el **porqué**, nunca el qué. Un comentario que repite el nombre de la función es
ruido. Los comentarios valiosos en este proyecto son los que explican una decisión de
rendimiento o un invariante no obvio:

```cpp
// Se relayoutea solo al cambiar el texto, no por frame: el efecto de máquina de
// escribir usa visible_glyphs sobre el layout ya calculado.
```

## Antes de dar por terminado un archivo

- ¿Compila con `-Wall -Wextra -Werror`?
- ¿Hay algún `new`, `malloc`, `std::string` o `virtual` que no debería estar?
- ¿Todos los `switch` sobre enums cubren todos los casos, sin `default`, para que
  `-Wswitch` avise al añadir uno nuevo?
- ¿Los includes respetan la dirección de las capas?
