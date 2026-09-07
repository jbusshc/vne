#include "vm/backlog.h"

Backlog g_backlog;

void backlog_reset(Backlog* b) {
    *b = Backlog{};
}

void backlog_push(Backlog* b, u16 speaker_id, u32 text_id, u16 voice_id) {
    // BacklogEntry tiene relleno entre sus campos (u16, u32, u16); construir con
    // value-init primero (`entry{}`) antes de rellenar los campos garantiza que ese
    // relleno quede en cero en vez de heredar basura de la pila del llamador — si no, dos
    // Backlog con el mismo contenido logico pueden diferir en memcmp (SPEC.md #8.4, test
    // obligatorio de vne-serializable-state).
    BacklogEntry entry{};
    entry.speaker_id    = speaker_id;
    entry.text_id       = text_id;
    entry.voice_id      = voice_id;
    b->entries[b->head] = entry;
    b->head             = (b->head + 1) % k_backlog_capacity;
    if (b->count < k_backlog_capacity) {
        b->count += 1;
    }
}

void backlog_get_ordered(const Backlog& b, BacklogEntry* out) {
    u32 oldest = (b.head + k_backlog_capacity - b.count) % k_backlog_capacity;
    for (u32 i = 0; i < b.count; ++i) {
        out[i] = b.entries[(oldest + i) % k_backlog_capacity];
    }
}

void backlog_load_ordered(Backlog* b, const BacklogEntry* ordered, u32 count) {
    backlog_reset(b);
    for (u32 i = 0; i < count; ++i) {
        backlog_push(b, ordered[i].speaker_id, ordered[i].text_id, ordered[i].voice_id);
    }
}
