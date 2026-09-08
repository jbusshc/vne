---
name: vne-serializable-state
description: Reglas del estado serializable de vne — la estructura GameState, el guardado en cualquier punto, el rollback por instantáneas y el versionado del formato de partida. Consulta este skill SIEMPRE que vayas a añadir o modificar cualquier dato que deba sobrevivir a un guardado, tocar GameState o VmState, trabajar en save/load, implementar rollback o backlog, decidir dónde vive una variable nueva del juego, o exponer algo a Lua. Es la sección más crítica del proyecto: un error aquí no se puede arreglar después sin rediseñar.
---

# Estado serializable

Este es el invariante central del motor. Si se rompe, el guardado y el rollback dejan de ser
posibles y no hay parche que lo arregle sin reescribir.

> **Todo el estado mutable del juego vive en una única estructura trivialmente copiable.**

## GameState

```cpp
constexpr u32 k_max_actor_slots = 8;
constexpr u32 k_max_vars        = 512;
constexpr u32 k_max_call_depth  = 16;
constexpr u32 k_max_flags       = 2048;

struct ActorSlot {
    u16 actor_id;   // 0 = slot vacio
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
    u8  cmd_phase;          // 0 = sin iniciar, 1 = en curso
    u8  _pad0[2];           // explicito, ver ADR-0028
    f32 cmd_timer;
    u32 visible_glyphs;
    u8  waiting_for_input;
    u8  _pad1[3];
};

struct GameState {
    u32       version;      // 2 hoy
    VmState   vm;
    ActorSlot actors[k_max_actor_slots];
    u16       bg_id;
    u8        _pad0[2];
    i32       vars[k_max_vars];
    u8        flags[k_max_flags / 8];
    u32       rng_state;
    u16       bgm_track_id;
    u8        _pad1[2];
    f32       bgm_position;
    f32       bus_volume[4];
    char      player_name[32];
    u32       playtime_seconds;
    // M9 (MapMode), anadidos al final: una v1 solo necesita rellenarlos con su valor
    // por defecto en migrate_v1_to_v2, sin reordenar nada existente.
    u16       map_id;
    u8        _pad2[2];
    f32       player_x;
    f32       player_y;
};

static_assert(std::is_trivially_copyable_v<GameState>);
```

Los campos `_pad` **no son decorativos**: bajo MSVC el relleno de alineación implícito no se
preserva de forma fiable a través de copias y escrituras parciales, así que dos estados
lógicamente iguales producían bytes distintos y el test obligatorio de M4 fallaba de forma
intermitente. Convertir cada hueco en un campo real con valor por defecto lo arregló
(ADR-0028). Al añadir un campo aquí, deja el hueco explícito tú, no el compilador.

## Prohibido dentro de GameState

Punteros. Handles a memoria transitoria. `std::string`, `std::vector` o cualquier contenedor
de tamaño dinámico. Referencias. Tipos con constructor no trivial. Padding sin inicializar
(pon `_pad` explícito si hace falta, y haz `memset` a cero al crear el estado).

Los textos **nunca** se copian al estado. Viven en el pool de strings del guion compilado y
se referencian por `u32 text_id`.

## Dónde poner un dato nuevo

Preguntas en orden:

1. ¿Cambia lo que el jugador ve o puede hacer al cargar la partida? → **va en `GameState`**.
2. ¿Es contenido inmutable del juego (textos, poses, tilemaps)? → va en el asset horneado,
   referenciado por ID.
3. ¿Es efímero de este frame (interpolaciones visuales, hover del ratón)? → variable local o
   `g_arena_frame`.
4. ¿Es preferencia del jugador que no afecta a la partida (idioma, pantalla completa)? → va
   en `config.ini`, aparte, no en `GameState`.

Si el dato no cabe en las capacidades fijas, **amplía la constante y sube la versión del
formato**. No lo conviertas en un contenedor dinámico.

## El estado de Lua no se serializa

Restricción crítica. Las funciones Lua deben ser efectivamente sin estado. Cualquier dato que
deba sobrevivir a un guardado tiene que estar en `GameState.vars` o `GameState.flags`, y Lua
lo lee y escribe a través de `vn.get_var` / `vn.set_var` / `vn.get_flag` / `vn.set_flag`.

Lua tampoco puede usar `math.random`: usa `vn.random()`, que avanza `GameState.rng_state`.
De lo contrario el rollback produce resultados distintos y el jugador lo nota.

## Guardado

```
Formato .vnsave
  0       4   magic 'VNSV'
  4       4   version (u32), 2 hoy
  8       4   sizeof(GameState) (validacion)
  12      4   CRC32 del bloque de estado
  16      N   GameState (volcado crudo)
  16+N    4   tamano de la miniatura
  20+N    M   miniatura QOI 384x216
  20+N+M      Backlog (ver seccion Backlog)
```

La miniatura es **QOI, no PNG** como decía SPEC.md §8.3: no hay codificador PNG en runtime y
QOI ya estaba en el stack para las texturas horneadas, así que añadir uno solo para guardar
una miniatura no se justificaba. Fue decisión del usuario (ADR-0037). La captura del
framebuffer está implementada solo en el backend D3D11 (ADR-0038).

Guardar es un `memcpy` más una captura del framebuffer. Cargar es la operación inversa más
validación de magic, versión, tamaño y checksum. Si algo no cuadra, el archivo se rechaza con
un mensaje claro; nunca se carga a medias.

## Versionado

Al romper compatibilidad: incrementa `k_savegame_version` y **escribe la función de migración
en el mismo commit**, nunca después.

```cpp
bool migrate_v3_to_v4(const void* in, usize in_size, GameState* out);
```

Las migraciones se encadenan: v2 → v3 → v4. Mantén una partida de ejemplo de cada versión
histórica en `tests/saves/` y un test que verifique que todas cargan.

## Rollback

Buffer circular de 64 instantáneas de `GameState`, reservado en `g_arena_perm` al arrancar.

- Se captura una instantánea **justo antes** de ejecutar un comando `Say` y un comando
  `Choice`, no en cada frame.
- Retroceder es copiar una instantánea de vuelta sobre el estado actual.
- El rollback **no reejecuta nada**. Por eso el motor no necesita ser determinista. Esta
  simplificación es deliberada: no introduzcas lógica de replay.
- Tras un rollback hay que resincronizar los servicios no serializados: reiniciar la pista de
  música en `bgm_position`, recolocar los sprites según `actors[]`, recalcular el layout del
  texto. Concentra eso en una única función `vm_resync_after_state_change()`.

## Backlog

Array circular de 200 entradas `{speaker_id, text_id, voice_id}`. Vive **fuera** de
`GameState` porque no afecta a la lógica, pero se serializa a continuación en el mismo
archivo de guardado.

## Test obligatorio

Existe desde M4 y no se puede romper: ejecutar un guion de prueba comando a comando,
guardando y recargando en **cada** comando, y verificar que el estado final es idéntico al de
una ejecución sin interrupciones. Si este test falla, nada más importa.
