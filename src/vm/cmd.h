#pragma once
#include <type_traits>

#include "base/types.h"

// Comandos del guion como tagged union (SPEC.md #8.1). Sin vtables, sin asignacion,
// tamano fijo: el guion compilado es un array contiguo de Cmd que se vuelca a disco tal
// cual.
//
// M3 declaro el subconjunto de dialogo/flujo lineal (Nop, Say, Show, Hide, Bg, Wait,
// Jump, Label, End). M5 anadio ramificacion (SetVar, AddVar, JumpIf, Choice, ChoiceEnd,
// Call, Return, LuaCall). M6 anade audio (Sfx, Bgm, StopBgm). M12 cierra los dos ultimos
// valores que faltaban de SPEC.md #8.1: Move y Transition. Los switch sin `default` del
// interprete siguen obligando a cubrir exactamente lo que existe ahora.
//
// Anadir un comando toca exactamente cuatro sitios: un valor aqui, un struct en la
// union, un caso en el parser/compilador, y un caso en cada uno de los tres switch de
// vm.cpp.

enum class CmdKind : u8 {
    Nop,
    Say,
    Show,
    Hide,
    Bg,
    Wait,
    Jump,
    Label,
    End,
    SetVar,
    AddVar,
    JumpIf,
    Choice,
    ChoiceEnd,
    Call,
    Return,
    LuaCall,
    Sfx,
    Bgm,
    StopBgm,
    Move,
    Transition,
    // M13: @flag en el DSL. SPEC.md #8.1 no los listaba, pero SPEC.md #12 los pide
    // explicitamente para M13 ("@flag en el DSL, para no tener que bajar a Lua solo para
    // leer una flag"); la lista de #8.1 queda actualizada. Ver ADR-0065.
    SetFlag,
    JumpIfFlag,
};

// Operadores de comparacion de JumpIf y de las condiciones opcionales de @choice
// (SPEC.md #9.1: "if valor > 2"). Solo comparaciones simples var-OP-valor: es lo unico
// que aparece en los ejemplos de la especificacion, no hay expresiones compuestas.
enum class CmpOp : u8 { Eq, Ne, Lt, Le, Gt, Ge };

// Las tres transiciones de pantalla completa que soporta @transition (SPEC.md #12: "la
// misma ruta de codigo para fade, wipe y disolucion"). Ver gfx/shaders.h para la formula
// unica que las tres comparten via una mascara distinta cada una.
enum class TransitionKind : u8 { Fade, Wipe, Dissolve };

struct Cmd {
    CmdKind kind;
    u8      _pad[3] = {};
    union {
        // key_hash: fnv1a_u32 del texto original en español (SPEC.md #9.2, "clave
        // estable... hash"), para resolver una traduccion en runtime (M10, ver
        // text/catalog.h) sin depender de un text_id que solo tiene sentido dentro del
        // string_pool de ESTE guion compilado. text_id sigue siendo el texto base
        // (fallback si no hay traduccion o el catalogo activo es el mismo idioma en que
        // se autoro el guion).
        struct { u16 speaker_id; u32 text_id; u32 key_hash; }     say;
        struct { u16 actor_id; u16 pose_id; u8 slot; f32 fade; }  show;
        struct { u8 slot; f32 fade; }                             hide;
        struct { u16 bg_id; f32 fade; }                           bg;
        struct { f32 seconds; }                                   wait;
        struct { u32 target_pc; }                                 jump;
        struct { u32 name_hash; }                                 label;
        struct { u16 var_id; i32 value; }                         set_var;
        // M13. Relleno explicito (ADR-0028): estos structs viajan en el .vnc, que lo
        // escribe una herramienta y lo lee el juego.
        struct { u16 flag_id; u8 value; u8 _pad_f[1]; }           set_flag;
        struct { u16 flag_id; u8 expected; u8 _pad_jf[1]; u32 target_pc; } jump_if_flag;
        struct { u16 var_id; i32 value; }                         add_var;
        // u8 _pad explicito antes de rhs (ADR-0028: relleno de alineacion implicito no
        // se preserva de forma fiable a traves de copias bajo MSVC).
        struct { u16 var_id; CmpOp op; u8 _pad; i32 rhs; u32 target_pc; } jump_if;
        struct { u32 first_option; u8 option_count; }             choice;
        struct { u32 target_pc; }                                 call;
        struct { u32 fn_id; }                                     lua_call;
        // text_id: indice en el string_pool con la ruta del archivo (p. ej.
        // "assets_src/ogg/puerta_cierra.ogg"). A diferencia de actor/pose/fondo (interning
        // numerico) o de var/flag (hash, ADR-0029), un efecto de sonido no necesita
        // sobrevivir a un guardado ni resolverse sin el guion que lo emitio, asi que
        // reusa la misma infraestructura que Say en vez de anadir una tabla nueva al
        // .vnc (ver ADR de M6).
        struct { u32 text_id; }                                   sfx;
        // track_id: SPEC.md #8.2 fija GameState.bgm_track_id en u16, asi que a
        // diferencia de sfx esto NO es un text_id de este guion (no sobreviviria a
        // cargar una partida guardada por un guion distinto): es fnv1a_u32(nombre) %
        // 65536, resuelto contra el catalogo de audio_load_track() en runtime (ver ADR
        // de M6).
        struct { u16 track_id; f32 fade; }                        bgm;
        struct { f32 fade; }                                      stop_bgm;
        // Interpola actors[slot].x/y linealmente desde su posicion actual hasta (x, y) en
        // "seconds" (mismo patron cmd_timer que el fade de Show/Hide, ver vm.cpp). Sin
        // consumidor visual todavia -- ni Show ni Hide lo tienen tampoco (VnMode/MapMode
        // no dibujan actors[], ver Pendientes observados) -- verificado sobre GameState,
        // no en pantalla.
        // _pad explicito (ADR-0028): sin el, el relleno de alineacion que el compilador
        // mete entre slot y x no se preserva de forma fiable a traves de copias bajo MSVC.
        struct { u8 slot; u8 _pad[3]; f32 x, y, seconds; }        move;
        // Unico valor de CmdKind que hace crecer sizeof(Cmd) de 16 a 20: es justo lo que
        // fija SPEC.md #8.1 como tamano final. transition_kind decide que mascara usa el
        // shader de gfx/shaders.h; el umbral (0..1) lo conduce cmd_timer/seconds, igual
        // que cualquier otro fade.
        struct { TransitionKind transition_kind; u8 _pad[3]; f32 seconds; } transition;
    };
};

static_assert(std::is_trivially_copyable_v<Cmd>);
// Tamano verificado compilando, no asumido (ver docs/DECISIONS.md): Move es el primer
// campo que de verdad hace falta que sea mas grande que los 12 bytes que ya cabian desde
// M5, y lleva Cmd a los 20 bytes finales de SPEC.md #8.1 (M12).
static_assert(sizeof(Cmd) == 20);

// Una opcion de un comando Choice (SPEC.md #9.1: texto, condicion opcional, etiqueta
// destino). No es parte de la tagged union de Cmd (su cardinalidad es variable por
// choice), asi que vive en una tabla aparte del .vnc indexada por
// Cmd::choice.first_option / option_count (extension del formato de SPEC.md #9.3, ver
// ADR de M5 en docs/DECISIONS.md: el formato documentado solo lista Cmd[]/string_pool/
// Label[], no hay tabla de opciones prevista).
// Relleno explicito en los huecos de alineacion (ADR-0028): sin esto, dos .vnc
// compilados a partir del mismo guion podrian diferir en bytes que no importan pero que
// arruinarian una comparacion o un hash del archivo compilado.
struct ChoiceOption {
    u32   text_id;              // indice en el string_pool, igual que Say
    u32   target_pc;            // a donde saltar si se elige esta opcion
    u8    has_condition = 0;    // 0 = opcion siempre visible
    u8    _pad0[1]       = {};
    u16   cond_var_id    = 0;
    CmpOp cond_op        = CmpOp::Eq;
    u8    _pad1[3]        = {};
    i32   cond_rhs        = 0;
    u32   key_hash        = 0;  // M10, ver comentario en Cmd::say
};

static_assert(std::is_trivially_copyable_v<ChoiceOption>);
