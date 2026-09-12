#include "vm/rollback.h"

#include "core/arena.h"

RollbackBuffer g_rollback;

void rollback_init(RollbackBuffer* rb) {
    rb->snapshots = arena_alloc_n<GameState>(&g_arena_perm, k_rollback_capacity);
    rb->count     = 0;
    rb->start     = 0;
    rb->cursor    = 0;
}

void rollback_capture(RollbackBuffer* rb, const GameState& state) {
    if (rb->count > 0 && rb->cursor + 1 < rb->count) {
        // Se habia retrocedido antes: este nuevo presente descarta el futuro pendiente.
        rb->count = rb->cursor + 1;
    }

    u32 physical;
    if (rb->count < k_rollback_capacity) {
        physical = (rb->start + rb->count) % k_rollback_capacity;
        rb->count += 1;
    } else {
        // Buffer lleno: la instantanea mas vieja se descarta para hacer sitio.
        physical  = rb->start;
        rb->start = (rb->start + 1) % k_rollback_capacity;
    }
    rb->snapshots[physical] = state;
    rb->cursor              = rb->count - 1;
}

bool rollback_back(RollbackBuffer* rb, GameState* out) {
    if (rb->cursor == 0) {
        return false;
    }
    rb->cursor -= 1;
    *out = rb->snapshots[(rb->start + rb->cursor) % k_rollback_capacity];
    return true;
}

bool rollback_forward(RollbackBuffer* rb, GameState* out) {
    if (rb->count == 0 || rb->cursor + 1 >= rb->count) {
        return false;
    }
    rb->cursor += 1;
    *out = rb->snapshots[(rb->start + rb->cursor) % k_rollback_capacity];
    return true;
}
