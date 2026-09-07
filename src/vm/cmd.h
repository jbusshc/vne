#pragma once
#include <type_traits>

#include "base/types.h"

// Comandos del guion como tagged union (SPEC.md #8.1). Sin vtables, sin asignacion,
// tamano fijo: el guion compilado es un array contiguo de Cmd que se vuelca a disco tal
// cual.
//
// M3 declaro el subconjunto de dialogo/flujo lineal (Nop, Say, Show, Hide, Bg, Wait,
// Jump, Label, End). M5 anade ramificacion (SetVar, AddVar, JumpIf, Choice, ChoiceEnd,
// Call, Return, LuaCall), no los 22 valores finales de SPEC.md #8.1 todavia: M6 anadira
// Sfx/Bgm/StopBgm/Move/Transition cuando le toque. Los switch sin `default` del
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
};

// Operadores de comparacion de JumpIf y de las condiciones opcionales de @choice
// (SPEC.md #9.1: "if valor > 2"). Solo comparaciones simples var-OP-valor: es lo unico
// que aparece en los ejemplos de la especificacion, no hay expresiones compuestas.
enum class CmpOp : u8 { Eq, Ne, Lt, Le, Gt, Ge };

struct Cmd {
    CmdKind kind;
    u8      _pad[3] = {};
    union {
        struct { u16 speaker_id; u32 text_id; }                   say;
        struct { u16 actor_id; u16 pose_id; u8 slot; f32 fade; }  show;
        struct { u8 slot; f32 fade; }                             hide;
        struct { u16 bg_id; f32 fade; }                           bg;
        struct { f32 seconds; }                                   wait;
        struct { u32 target_pc; }                                 jump;
        struct { u32 name_hash; }                                 label;
        struct { u16 var_id; i32 value; }                         set_var;
        struct { u16 var_id; i32 value; }                         add_var;
        // u8 _pad explicito antes de rhs (ADR-0028: relleno de alineacion implicito no
        // se preserva de forma fiable a traves de copias bajo MSVC).
        struct { u16 var_id; CmpOp op; u8 _pad; i32 rhs; u32 target_pc; } jump_if;
        struct { u32 first_option; u8 option_count; }             choice;
        struct { u32 target_pc; }                                 call;
        struct { u32 fn_id; }                                     lua_call;
    };
};

static_assert(std::is_trivially_copyable_v<Cmd>);
// Sigue en 16 bytes: aunque M5 anade jump_if (el miembro mas grande de la union, con
// var_id+op+rhs+target_pc), su relleno de alineacion cabe en el mismo tamano que ya
// ocupaba show en M3. No es el 20 final de SPEC.md #8.1 todavia (M6 anadira Move con
// tres f32, que si lo hara crecer). Verificado compilando, no asumido (ver
// docs/DECISIONS.md).
static_assert(sizeof(Cmd) == 16);

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
};

static_assert(std::is_trivially_copyable_v<ChoiceOption>);
