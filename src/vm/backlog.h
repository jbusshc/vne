#pragma once
#include <type_traits>

#include "base/types.h"

// Historial de dialogo (SPEC.md #8.4): vive fuera de GameState porque no afecta a la
// logica del juego, pero se serializa a continuacion en el mismo archivo de guardado.
// Trivialmente copiable por el mismo motivo que GameState (skill vne-serializable-state).

constexpr u32 k_backlog_capacity = 200;

struct BacklogEntry {
    u16 speaker_id = 0xFFFFu;  // 0xFFFF = sin hablante
    // Relleno explicito (skill vne-serializable-state: "padding sin inicializar" esta
    // prohibido). El hueco implicito que el compilador metería aqui para alinear text_id
    // no se preserva de forma fiable como cero a traves de copias y escrituras parciales;
    // convertirlo en un campo real evita que dos backlogs con el mismo contenido logico
    // difieran en un memcmp o al guardarse a disco (test obligatorio de M4).
    u8  _pad0[2]   = {};
    u32 text_id    = 0;
    u16 voice_id   = 0xFFFFu;  // 0xFFFF = sin voz (no hay audio hasta M6)
    u8  _pad1[2]   = {};  // relleno final explicito, mismo motivo.
};

struct Backlog {
    BacklogEntry entries[k_backlog_capacity];
    u32          count = 0;  // cuantas entradas validas hay (<= capacidad)
    u32          head  = 0;  // indice fisico donde se escribiria la proxima entrada
};

static_assert(std::is_trivially_copyable_v<Backlog>);

// Unica instancia del proceso, igual que las arenas globales de base/arena.h.
extern Backlog g_backlog;

void backlog_reset(Backlog* b);
void backlog_push(Backlog* b, u16 speaker_id, u32 text_id, u16 voice_id);

// Para serializar: `out` debe tener espacio para b.count entradas. Se listan de la mas
// vieja a la mas nueva, sin exponer el indice fisico del ring buffer (irrelevante fuera
// de este modulo).
void backlog_get_ordered(const Backlog& b, BacklogEntry* out);

// Reconstruye un Backlog desde una lista ordenada (la que escribio backlog_get_ordered),
// reutilizando backlog_push para no duplicar la logica del ring buffer.
void backlog_load_ordered(Backlog* b, const BacklogEntry* ordered, u32 count);
