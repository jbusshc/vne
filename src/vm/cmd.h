#pragma once
#include <type_traits>

#include "base/types.h"

// Comandos del guion como tagged union (SPEC.md #8.1). Sin vtables, sin asignacion,
// tamano fijo: el guion compilado es un array contiguo de Cmd que se vuelca a disco tal
// cual.
//
// M3 solo declara el subconjunto que este hito necesita (Nop, Say, Show, Hide, Bg, Wait,
// Jump, Label, End), no los 22 valores finales de SPEC.md #8.1: cada hito futuro (M5
// Choice/SetVar/etc, M6 Sfx/Bgm) anade su propio valor cuando le toca, para que los
// switch sin `default` del interprete sigan obligando a cubrir exactamente lo que existe
// ahora, no comandos de hitos que todavia no llegan. El static_assert de tamano crecera
// con cada comando nuevo (ver docs/DECISIONS.md).
//
// Anadir un comando toca exactamente cuatro sitios: un valor aqui, un struct en la
// union, un caso en el parser, y un caso en cada uno de los tres switch de vm.cpp.

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
};

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
    };
};

static_assert(std::is_trivially_copyable_v<Cmd>);
// 16 bytes con el subconjunto de M3. Crecera con cada comando que anadan M5 (Choice,
// SetVar, JumpIf, Call, Return, LuaCall) y M6 (Sfx, Bgm, StopBgm); no es el 20 final de
// SPEC.md #8.1 todavia (ver docs/DECISIONS.md).
static_assert(sizeof(Cmd) == 16);
