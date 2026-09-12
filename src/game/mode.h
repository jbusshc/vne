#pragma once
#include "core/types.h"
#include "platform/input.h"

// Pila de estados de juego (SPEC.md #10). Decision explicita de la especificacion usar
// `virtual` aqui pese a la regla general de "sin virtual en nada que se itere por
// elemento" (skill vne-cpp-style): la pila tiene 2-4 modos como mucho, no miles de
// elementos por frame, y la especificacion manda sobre el skill en caso de conflicto
// (CLAUDE.md). Solo el modo del tope recibe `update`; el renderizado recorre la pila de
// abajo a arriba respetando `blocks_render_below`.

struct Mode {
    virtual ~Mode() = default;
    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual void update(const InputState& input, f32 dt) = 0;
    virtual void render() = 0;
    virtual bool blocks_update_below() const { return true; }
    virtual bool blocks_render_below() const { return false; }
};

constexpr u32 k_max_mode_stack = 8;

// Pila de punteros no propietaria: quien empuja un Mode sigue siendo dueno de su
// almacenamiento (arena de escena o permanente, nunca heap — skill vne-memory-model).
// No hay asignacion dinamica aqui, solo un array fijo.
struct ModeStack {
    Mode* modes[k_max_mode_stack] = {};
    u32   count                    = 0;
};

inline void mode_stack_push(ModeStack* s, Mode* m) {
    if (s->count >= k_max_mode_stack) {
        return;
    }
    s->modes[s->count] = m;
    s->count += 1;
    m->on_enter();
}

inline void mode_stack_pop(ModeStack* s) {
    if (s->count == 0) {
        return;
    }
    s->count -= 1;
    s->modes[s->count]->on_exit();
}

inline void mode_stack_update(ModeStack* s, const InputState& input, f32 dt) {
    for (u32 i = s->count; i > 0; --i) {
        Mode* m = s->modes[i - 1];
        m->update(input, dt);
        if (m->blocks_update_below()) {
            break;
        }
    }
}

inline void mode_stack_render(ModeStack* s) {
    // De abajo arriba: encuentra desde donde empezar a dibujar respetando
    // blocks_render_below (un modo que bloquea el render de abajo hace que no haga falta
    // dibujar nada por debajo de el).
    u32 start = 0;
    for (u32 i = s->count; i > 0; --i) {
        if (s->modes[i - 1]->blocks_render_below()) {
            start = i - 1;
            break;
        }
    }
    for (u32 i = start; i < s->count; ++i) {
        s->modes[i]->render();
    }
}
